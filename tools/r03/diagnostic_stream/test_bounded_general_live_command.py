#!/usr/bin/env python3
"""Command/receipt tests only. Native compiler and VM inputs are not qualified."""
from __future__ import annotations
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SOURCE=Path(__file__).with_name('run_bounded_general_live.py')

def load():
    spec=importlib.util.spec_from_file_location('p1_live_command_under_test',SOURCE)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

class NativeCommandReceipts(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve()
        self.mod=load()
        self.commands=[]

    def receipt(self):
        record=json.loads((self.root/'case.command.json').read_bytes())
        self.assertEqual(self.commands,[record])
        for stream in ('stdout','stderr'):
            raw=(self.root/('case.'+stream+'.bin')).read_bytes()
            self.assertEqual(record[stream+'Sha256'],hashlib.sha256(raw).hexdigest())
            self.assertEqual(record[stream+'CapturedBytes'],len(raw))
        return record

    def test_real_success_preserves_binary_streams(self):
        cmd=[sys.executable,'-B','-c',"import os;os.write(1,b'out\\x00\\xff');os.write(2,b'err\\n')"]
        result=self.mod.run(cmd,self.root,'case',self.commands)
        self.assertEqual(result,b'out\0\xff')
        record=self.receipt()
        self.assertEqual(record['exit'],0)
        self.assertEqual(record['outputCompleteness'],'CompleteDirectChildStreams')
        self.assertFalse(record['commandLifetimeUnverified'])

    def test_real_nonzero_preserves_receipt_and_failure(self):
        cmd=[sys.executable,'-B','-c',"import os;os.write(2,b'expected failure');raise SystemExit(7)"]
        with self.assertRaisesRegex(RuntimeError,'exit=7'):
            self.mod.run(cmd,self.root,'case',self.commands)
        self.assertEqual(self.receipt()['exit'],7)
        self.assertEqual((self.root/'case.stderr.bin').read_bytes(),b'expected failure')

    def test_real_timeout_preserves_prefix_and_no_invented_exit(self):
        original=subprocess.run
        def bounded(*args,**kwargs):
            kwargs['timeout']=0.8
            return original(*args,**kwargs)
        cmd=[sys.executable,'-B','-c',"import os,time;os.write(1,b'out-before');os.write(2,b'err-before');time.sleep(30)"]
        with patch.object(self.mod.subprocess,'run',side_effect=bounded):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.mod.run(cmd,self.root,'case',self.commands)
        record=self.receipt()
        self.assertIsNone(record['exit'])
        self.assertTrue(record['timedOut'])
        self.assertTrue(record['commandLifetimeUnverified'])
        self.assertEqual(record['outputCompleteness'],'CapturedPrefix')
        self.assertEqual((self.root/'case.stdout.bin').read_bytes(),b'out-before')
        self.assertEqual((self.root/'case.stderr.bin').read_bytes(),b'err-before')

    def test_timeout_without_capture_is_unavailable_not_full_empty_output(self):
        with patch.object(self.mod.subprocess,'run',side_effect=subprocess.TimeoutExpired(['mock'],180)):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.mod.run(['mock'],self.root,'case',self.commands)
        record=self.receipt()
        self.assertFalse(record['stdoutAvailable'])
        self.assertFalse(record['stderrAvailable'])
        self.assertEqual(record['outputCompleteness'],'CapturedPrefix')
        self.assertIsNone(record['exit'])

    def test_interruption_records_before_propagating(self):
        with patch.object(self.mod.subprocess,'run',side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                self.mod.run(['mock'],self.root,'case',self.commands)
        record=self.receipt()
        self.assertTrue(record['interrupted'])
        self.assertTrue(record['commandLifetimeUnverified'])
        self.assertEqual(record['outputCompleteness'],'Unavailable')

    def test_real_missing_executable_is_a_recorded_launch_failure(self):
        with self.assertRaises(FileNotFoundError):
            self.mod.run([str(self.root/'does-not-exist')],self.root,'case',self.commands)
        record=self.receipt()
        self.assertIsNone(record['exit'])
        self.assertFalse(record['stdoutAvailable'])
        self.assertIn('FileNotFoundError',record['exception'])

    def test_unknown_execution_exception_is_not_assumed_quiescent(self):
        with patch.object(self.mod.subprocess,'run',side_effect=RuntimeError('unknown outcome')):
            with self.assertRaises(RuntimeError):
                self.mod.run(['mock'],self.root,'case',self.commands)
        self.assertTrue(self.receipt()['commandLifetimeUnverified'])

    def test_existing_sanitizer_and_environment_policy_is_preserved(self):
        fake=subprocess.CompletedProcess(['mock'],0,b'',b'')
        with patch.object(self.mod.subprocess,'run',return_value=fake) as command:
            self.mod.run(['mock'],self.root,'case',self.commands)
        call=command.call_args.kwargs
        self.assertEqual(call['timeout'],180)
        self.assertTrue(call['capture_output'])
        self.assertEqual(call['env']['UBSAN_OPTIONS'],'halt_on_error=1:print_stacktrace=1')
        self.assertEqual(call['env']['ASAN_OPTIONS'],
            'abort_on_error=1:detect_leaks='+('0' if sys.platform=='darwin' else '1'))
        self.assertNotIn('start_new_session',call)

class NativeSummaryFailureReceipts(unittest.TestCase):
    def exercise(self,mode):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td).resolve();native=root/'native';native.mkdir()
            source=native/'test-source.txt';source.write_text('MOCK VM source, not production proof\n')
            output=root/'result';mod=load()
            args=['run','--native',str(native),'--output',str(output)]
            def command(argv,out,name,commands):
                commands.append({'name':name,'exit':0,'commandLifetimeUnverified':False})
                if name=='sanitized-run' and mode=='source-removed':source.unlink()
                return ('\n'.join('GROUP:'+g for g in mod.GROUPS)+'\nP1 REAL GENERAL LIVE SOURCE METHOD HOST PASS\n').encode()
            error=None
            with patch.object(sys,'argv',args),patch.object(mod,'FILES',('test-source.txt',)), \
                 patch.object(mod,'provenance',return_value=('MOCK SOURCE','MOCK FIXTURE')), \
                 patch.object(mod.shutil,'which',return_value=None if mode=='missing-compiler' else '/mock/compiler'), \
                 patch.object(mod,'run',side_effect=command),contextlib.redirect_stdout(io.StringIO()):
                try:mod.main()
                except (RuntimeError,SystemExit) as exc:error=exc
            report=json.loads((output/'RESULT.json').read_bytes())
            self.assertIsNotNone(error)
            self.assertEqual(report['result'],'FailedOrIncomplete')
            self.assertTrue((output/'MANIFEST.json').is_file())
            self.assertFalse(report['runtimeAcceptance'])
            return report

    def test_missing_compiler_still_writes_failure_summary(self):
        report=self.exercise('missing-compiler')
        self.assertEqual(report['commands'],[])
        self.assertFalse(report['compiledExactProductionSource'])
        self.assertFalse(report['compiledActualBoundedSourcesAndSerializer'])
        self.assertIn('Missing C++ compiler',report['firstError'])

    def test_postflight_source_loss_preserves_failure_summary(self):
        report=self.exercise('source-removed')
        self.assertFalse(report['sourcePreserved'])
        self.assertIn('FileNotFoundError',report['postflightError'])
        self.assertEqual(len(report['commands']),4)

if __name__=='__main__':unittest.main(verbosity=2)
