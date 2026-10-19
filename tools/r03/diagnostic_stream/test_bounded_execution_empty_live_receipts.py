#!/usr/bin/env python3
"""Native zero-class host runner's real failure-receipt and lifetime contract.

These tests execute the current Python runner with exact source files, but
compiler/launch outcomes are selectively injected. No Unity or Player invoked.
"""
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

HERE=Path(__file__).resolve().parent
RUNNER=HERE/'run_bounded_execution_empty_live.py'
NATIVE=HERE.parents[2]


def load():
    spec=importlib.util.spec_from_file_location('actual_empty_live_receipt_runner',RUNNER)
    mod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class CommandReceipts(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.mod=load()
        self.receipts=[]

    def call(self,effect=None,return_value=None):
        with patch.object(self.mod.subprocess,'run',side_effect=effect,return_value=return_value):
            with self.assertRaises(RuntimeError):
                self.mod.run(['owned-mock'],self.root,'probe',{},self.receipts,timeout=2)
        self.assertEqual(len(self.receipts),1)
        row=self.receipts[0]
        self.assertEqual(row,json.loads((self.root/'probe.json').read_text()))
        for stream in ('stdout','stderr'):
            raw=(self.root/('probe.'+stream+'.bin')).read_bytes()
            self.assertEqual(row[stream+'Sha256'],hashlib.sha256(raw).hexdigest())
            self.assertEqual(row[stream+'CapturedBytes'],len(raw))
        return row

    def test_timeout_keeps_partial_streams_null_exit_and_uncertainty(self):
        error=subprocess.TimeoutExpired(['owned-mock'],2,output=b'prefix-out',stderr=b'prefix-err')
        row=self.call(effect=error)
        self.assertEqual(row['exit'],None)
        self.assertTrue(row['commandLifetimeUnverified'])
        self.assertTrue(row['timedOut'])
        self.assertEqual(row['outputCompleteness'],'CapturedPrefix')
        self.assertEqual((self.root/'probe.stdout.bin').read_bytes(),b'prefix-out')
        self.assertEqual((self.root/'probe.stderr.bin').read_bytes(),b'prefix-err')

    def test_timeout_without_streams_keeps_availability_false(self):
        row=self.call(effect=subprocess.TimeoutExpired(['owned-mock'],2))
        self.assertFalse(row['stdoutAvailable'])
        self.assertFalse(row['stderrAvailable'])
        self.assertIsNone(row['exit'])
        self.assertTrue(row['commandLifetimeUnverified'])

    def test_interruption_is_explicitly_unverified_and_preserved(self):
        row=self.call(effect=KeyboardInterrupt())
        self.assertTrue(row['interrupted'])
        self.assertTrue(row['commandLifetimeUnverified'])
        self.assertIsNone(row['exit'])
        self.assertEqual(row['outputCompleteness'],'Unavailable')

    def test_known_spawn_failure_is_failed_but_not_unknown_lifetime(self):
        row=self.call(effect=FileNotFoundError('mock missing executable'))
        self.assertFalse(row['commandLifetimeUnverified'])
        self.assertIsNone(row['exit'])
        self.assertEqual(row['status'],'Failed')

    def test_completed_nonzero_retains_known_exit_and_complete_streams(self):
        row=self.call(return_value=subprocess.CompletedProcess(['mock'],7,b'out',b'err'))
        self.assertEqual(row['exit'],7)
        self.assertFalse(row['commandLifetimeUnverified'])
        self.assertEqual(row['outputCompleteness'],'CompleteDirectChildStreams')

    def test_unknown_subprocess_exception_is_unverified(self):
        row=self.call(effect=RuntimeError('unexpected subprocess state'))
        self.assertTrue(row['commandLifetimeUnverified'])
        self.assertEqual(row['status'],'Failed')


class NativeSummaryContracts(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.output=self.root/'unused-native-output'
        self.mod=load()
        self.assertTrue(NATIVE.is_dir())
        self.assertTrue((NATIVE/self.mod.INL).is_file())

    def execute_main(self,side_effect=None,cxx='/test/cxx'):
        argv=[str(RUNNER),'--native',str(NATIVE),'--output',str(self.output)]
        with patch.object(sys,'argv',argv), patch.object(self.mod.shutil,'which',return_value=cxx), \
             patch.object(self.mod.subprocess,'run',side_effect=side_effect), \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:self.mod.main()
        summary=json.loads((self.output/'RESULT.json').read_bytes())
        manifest=json.loads((self.output/'MANIFEST.json').read_bytes())
        for item in manifest['files']:
            target=self.output/item['path']
            self.assertEqual(item['sha256'],hashlib.sha256(target.read_bytes()).hexdigest())
        self.assertFalse(summary['runtimeAcceptance'])
        self.assertFalse(summary['officialUnitySdkCompiled'])
        return failure.exception.code, summary, manifest

    def test_missing_compiler_emits_failed_summary_with_no_compile_claim(self):
        code,summary,manifest=self.execute_main(cxx=None)
        self.assertEqual(code,1)
        self.assertEqual(summary['commands'],[])
        self.assertFalse(summary['actualProductionHeaderAndMethodCompiled'])
        self.assertTrue(summary['sourcePreserved'])
        self.assertFalse(summary['commandLifetimeUnverified'])
        self.assertEqual(manifest['inventoryScope'],'CompletedHostFiles')
        self.assertIn('compiler',summary['firstError'])

    def test_failed_compiler_version_never_claims_method_compiled(self):
        code,summary,manifest=self.execute_main(side_effect=[
            subprocess.CompletedProcess(['cxx','--version'],4,b'',b'cannot query version')])
        self.assertEqual(code,1)
        self.assertEqual(len(summary['commands']),1)
        self.assertFalse(summary['actualProductionHeaderAndMethodCompiled'])
        self.assertEqual(summary['successfulSourceCompileProfiles'],[])
        self.assertFalse(summary['commandLifetimeUnverified'])
        self.assertTrue(summary['sourcePreserved'])

    def test_strict_compile_nonzero_does_not_claim_compilation(self):
        effects=[subprocess.CompletedProcess(['cxx','--version'],0,b'version',b''),
                 subprocess.CompletedProcess(['cxx','-c'],8,b'',b'compile error')]
        code,summary,manifest=self.execute_main(side_effect=effects)
        self.assertEqual(code,1)
        self.assertEqual(len(summary['commands']),2)
        self.assertFalse(summary['actualProductionHeaderAndMethodCompiled'])
        self.assertFalse(summary['commandLifetimeUnverified'])
        self.assertTrue(summary['sourcePreserved'])
        self.assertEqual(manifest['inventoryScope'],'CompletedHostFiles')

    def test_compile_timeout_propagates_lifetime_and_skips_source_postflight(self):
        effects=[subprocess.CompletedProcess(['cxx','--version'],0,b'version',b''),
                 subprocess.TimeoutExpired(['cxx','-c'],2,output=b'compile-prefix',stderr=b'diagnostic-prefix')]
        code,summary,manifest=self.execute_main(side_effect=effects)
        self.assertEqual(code,1)
        self.assertTrue(summary['commandLifetimeUnverified'])
        self.assertFalse(summary['sourcePreserved'])
        self.assertEqual(summary['sourceAfter'],{})
        self.assertIsNotNone(summary['postflightError'])
        self.assertFalse(summary['actualProductionHeaderAndMethodCompiled'])
        self.assertEqual(manifest['inventoryScope'],'UnsealedObservedFiles')
        self.assertTrue(manifest['commandLifetimeUnverified'])
        self.assertEqual(summary['commands'][-1]['exit'],None)
        self.assertEqual((self.output/'strict-compile.stdout.bin').read_bytes(),b'compile-prefix')
        self.assertEqual((self.output/'strict-compile.stderr.bin').read_bytes(),b'diagnostic-prefix')

    def test_keyboard_interrupt_propagates_failstop_and_exits_130(self):
        effects=[subprocess.CompletedProcess(['cxx','--version'],0,b'version',b''),KeyboardInterrupt()]
        code,summary,manifest=self.execute_main(side_effect=effects)
        self.assertEqual(code,130)
        self.assertTrue(summary['commandLifetimeUnverified'])
        self.assertTrue(summary['commands'][-1]['interrupted'])
        self.assertFalse(summary['sourcePreserved'])
        self.assertFalse(summary['actualProductionHeaderAndMethodCompiled'])
        self.assertEqual(manifest['inventoryScope'],'UnsealedObservedFiles')

    def test_unknown_subprocess_exception_propagates_failstop(self):
        effects=[subprocess.CompletedProcess(['cxx','--version'],0,b'version',b''),RuntimeError('indeterminate state')]
        code,summary,manifest=self.execute_main(side_effect=effects)
        self.assertEqual(code,1)
        self.assertTrue(summary['commandLifetimeUnverified'])
        self.assertFalse(summary['actualProductionHeaderAndMethodCompiled'])
        self.assertFalse(summary['sourcePreserved'])
        self.assertEqual(manifest['inventoryScope'],'UnsealedObservedFiles')


if __name__=='__main__':unittest.main(verbosity=2)
