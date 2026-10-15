#!/usr/bin/env python3
"""Exact legacy/general serializer -> bounded native capture -> V2 interoperability.

The native general formatter is actual product source. Input VM snapshots are
synthetic/prebuilt. Execution JSON comes from the actual ORIGINAL function under
explicit VM mocks; its unbounded acquisition is not a newly integrated stream.
No Unity, new Player transport or filesystem durability claim is made here.
"""
from __future__ import annotations
import argparse, hashlib, importlib.util, io, json, os, re, shutil, subprocess, sys, unittest
from pathlib import Path

OLD_BLOB='226b01273d3e30d190422c4c4c740c49a8d957f0'
CODEC_BLOB='174766c40235216d8c954107dd5e026116a510fa'
REF_BLOB='0866b07f758a20284d2a5d5d933b2be577b20b50'
HERE=Path(__file__).resolve().parent
OUT=None; DEMO=None; NATIVE=None; REF=None; DRIVER=None; EXECUTION=None; PROFILE=''; SEQ=0; RECORDS=[]
IDENTITY=dict(runId='serializer-run',caseId='baseline',processId=1234,requestSha256='a'*64,
 buildSha256='b'*64,demoCommit='1'*40,hybridclrCommit='2'*40,packageCommit='3'*40,nativeCommit='4'*40)
INLINE={'reportTransport':'NativeBoundedSnapshotV2','status':'Failed','preReflection':42,'preDelegate':42}

def sha(d):return hashlib.sha256(d).hexdigest()
def blob(d):return hashlib.sha1(b'blob '+str(len(d)).encode()+b'\0'+d).hexdigest()
def need(ok,message):
 if not ok:raise RuntimeError(message)
def run(argv,prefix,timeout=120):
 env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS=('detect_leaks=0:abort_on_error=1' if sys.platform=='darwin' else 'detect_leaks=1:abort_on_error=1'),UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 r=subprocess.run([str(x) for x in argv],capture_output=True,env=env,timeout=timeout)
 prefix.parent.mkdir(parents=True,exist_ok=True)
 prefix.with_suffix('.stdout').write_bytes(r.stdout);prefix.with_suffix('.stderr').write_bytes(r.stderr)
 rec=dict(argv=[str(x) for x in argv],exit=r.returncode,stdoutSha256=sha(r.stdout),stderrSha256=sha(r.stderr),record=str(prefix.relative_to(OUT)),asan=env['ASAN_OPTIONS'],ubsan=env['UBSAN_OPTIONS'])
 prefix.with_suffix('.command.json').write_text(json.dumps(rec,indent=2)+'\n');RECORDS.append(rec)
 need(r.returncode==0,'Command failed; preserve '+str(prefix))
 return r

def command(args):
 global SEQ
 SEQ+=1;return json.loads(run([DRIVER,*args],OUT/PROFILE/f'process-{SEQ:03d}').stdout)

def roundtrip(count=2,state=9,variant=0):
 global SEQ
 SEQ+=1;folder=OUT/PROFILE/f'field-{SEQ:03d}';folder.mkdir()
 (folder/'execution.json').write_bytes(EXECUTION)
 obj=json.loads(run([DRIVER,'roundtrip',count,state,variant,folder,REF.binding(IDENTITY).hex(),sha(REF.inline_bytes(INLINE))],folder/'run').stdout)
 old=(folder/'original.json').read_bytes();new=(folder/'legacy.json').read_bytes();stream=(folder/'streamed.json').read_bytes()
 need(old==new==stream,'Shared formatting changed original bytes')
 need(obj['formatNewCalls']==0 and obj['encodeNewCalls']==0,'Formatting/encoding allocated operator-new storage')
 if variant not in (3,4):
  expected,descriptor=REF.encode(IDENTITY,INLINE,{'finalDiagnostics':old.decode('utf-8'),'finalExecutionDiagnostics':EXECUTION.decode('utf-8')})
  wire=(folder/'wire.bin').read_bytes();need(wire==expected,'Native framing mismatch')
  hydrated=REF.decode(io.BytesIO(wire),descriptor,IDENTITY,INLINE)
  need(hydrated['finalDiagnostics'].encode()==old and hydrated['finalExecutionDiagnostics'].encode()==EXECUTION,'Lossless field reconstruction')
  need(obj['wireComplete'],'Missing complete wire result')
  raw={**INLINE,'diagnosticArtifact':descriptor};view=REF.hydrate(raw,wire,descriptor,IDENTITY)
  need(view['status']=='Failed' and view['reportTransport']=='NativeBoundedSnapshotV2','Tagged Failed semantics changed')
  (folder/'descriptor.json').write_text(json.dumps(descriptor,indent=2)+'\n')
 else:
  need(not obj['wireComplete'] and not (folder/'wire.bin').read_bytes(),'Incomplete field accepted')
 return obj,old,folder

class SerializerTests(unittest.TestCase):
 def test_all_fields_large_capture_to_v2(self):
  result,old,_=roundtrip(4096,9,1)
  self.assertGreater(len(old),262144);self.assertGreater(len(EXECUTION),262144)
  decoded=json.loads(old);self.assertEqual(len(decoded['ordinaryClasses']),4096)
  self.assertEqual(decoded['lastError'],13)
 def test_default_disabled_snapshot(self):
  _,old,_=roundtrip(0,0,6);data=json.loads(old);self.assertFalse(data['enabled']);self.assertEqual(data['startupObservationMode'],'Unavailable')
 def test_snapshot_layout_preserved(self):
  old=(HERE/'fixtures/AssemblyShadowDiagnostics.h').read_text()
  new=(NATIVE/'libil2cpp/vm/AssemblyShadowDiagnostics.h').read_text()
  body=lambda s:s[s.index('struct ShadowAssemblyDiagnostic'):s.index('class AssemblyShadowDiagnostics')]
  self.assertEqual(body(old),body(new))
 def test_all_states_and_unknown(self):
  for state in list(range(10))+[-1]:
   with self.subTest(state=state):roundtrip(0,state,0)
 def test_empty_class_inventory(self):
  _,old,_=roundtrip(0);self.assertEqual(json.loads(old)['ordinaryClasses'],[])
 def test_all_control_bytes_and_unicode(self):
  _,old,_=roundtrip(1,9,1);self.assertIn('\x00',json.loads(old)['detail']);self.assertIn('中',json.loads(old)['detail'])
 def test_long_single_string(self):
  _,old,_=roundtrip(1,9,2);self.assertEqual(len(json.loads(old)['detail']),300000)
 def test_minimum_signed_and_maximum_unsigned(self):
  _,old,_=roundtrip(1,9,5);data=json.loads(old);self.assertEqual(data['lastError'],-2**31);self.assertEqual(data['generation'],2**64-1)
 def test_invalid_utf8_remains_invalid(self):
  obj,old,_=roundtrip(1,9,4);self.assertEqual(obj['captureError'],4)
  with self.assertRaises(UnicodeDecodeError):old.decode('utf-8')
 def test_total_capacity_rejects_full_producer(self):
  obj,_,_=roundtrip(1,9,3);self.assertFalse(obj['wireComplete']);self.assertEqual(obj['formatError'],2);self.assertEqual(obj['captureError'],0)
 def test_zero_capacity(self):self.assertTrue(command(['negative','limit',0])['rejected'])
 def test_short_capacity(self):self.assertTrue(command(['negative','limit',100])['rejected'])
 def test_exactly_one_byte_short(self):self.assertTrue(command(['negative','limit',999999])['rejected'])
 def test_all_sink_failures(self):
  x=command(['negative','all-sink-failures',0]);self.assertGreater(x['sinkWritePositions'],200);self.assertEqual(x['partialAndNoWriteFailures'],2*x['sinkWritePositions'])
 def test_null_sink(self):self.assertTrue(command(['negative','null-sink',0])['rejected'])
 def test_nonclassic_locale_explicitly_rejected(self):self.assertTrue(command(['nonclassic'])['primitivePassed'])
 def test_native_integer_writer(self):self.assertTrue(command(['integers'])['primitivePassed'])
 def test_write_reentry(self):self.assertTrue(command(['reenter'])['primitivePassed'])
 def test_finish_reentry(self):self.assertTrue(command(['reenter-finish'])['primitivePassed'])
 def test_second_finish(self):self.assertTrue(command(['repeat-finish'])['primitivePassed'])
 def test_write_after_finish(self):self.assertTrue(command(['write-after-finish'])['primitivePassed'])
 def test_nonempty_null_input(self):self.assertTrue(command(['bad-input'])['primitivePassed'])
 def test_negative_byte_count(self):self.assertTrue(command(['negative-length'])['primitivePassed'])
 def test_unknown_format_manipulator(self):self.assertTrue(command(['bad-manipulator'])['primitivePassed'])
 def test_original_general_schema_tokens(self):
  old=(HERE/'fixtures/LegacyAssemblyShadowDiagnostics.cpp').read_text()
  new=(NATIVE/'libil2cpp/vm/AssemblyShadowDiagnostics.cpp').read_text()
  # All original serialized keys/literals and public name/Quote functions persist.
  a=old[old.index('static void StringArray'):];b=new[new.index('template<class Output>'):new.index('assembly_shadow_diagnostic::WriteResult AssemblyShadowDiagnostics::SerializeTo')]
  strings=lambda s:re.findall(r'"(?:[^"\\]|\\.)*"',s)
  self.assertEqual(strings(a),strings(b))
  self.assertEqual(old[:old.index('static void StringArray')],new[:new.index('template<class Output>')])

def main():
 global OUT,DEMO,NATIVE,REF,DRIVER,PROFILE,EXECUTION
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--native',type=Path,required=True)
 p.add_argument('--demo',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True)
 a=p.parse_args()
 need(__debug__,'Python assertions must remain enabled')
 NATIVE=a.native.resolve(strict=True);DEMO=a.demo.resolve(strict=True);OUT=a.output
 need(OUT.is_absolute() and OUT==OUT.resolve(strict=False) and OUT.parent.is_dir() and not OUT.exists(),'New canonical absolute output required')
 need(all(OUT!=q and q not in OUT.parents and OUT not in q.parents for q in (NATIVE,DEMO)),'Output outside source')
 # Input paths/output identity are checked before creating this fresh root.
 # Once created, every early source-binding failure gets a factual receipt.
 OUT.mkdir()
 verified=False;profiles=[];before={};failure=None;old_producer='NotRun';current_producer='NotRun'
 try:
  codec=DEMO/'Tools/AssemblyShadow/R03IR/bounded_v2/BoundedDiagnosticsV2.h'
  reference=DEMO/'Docs/AssemblyShadow/History/M07R/R03/IR_Remediation/SNAP01_BOUNDED_DESIGN_f8a2/reference/diagnostic_stream.py'
  need(blob(codec.read_bytes())==CODEC_BLOB and blob(reference.read_bytes())==REF_BLOB,'Unchanged native codec/reference')
  need(blob((HERE/'fixtures/LegacyAssemblyShadowDiagnostics.cpp').read_bytes())==OLD_BLOB,'Original legacy serializer binding')
  need(blob((HERE/'fixtures/AssemblyShadowDiagnostics.h').read_bytes())=='d76bb25a7e896d3c42af172ec3c80012ef2f2419','Original snapshot/header binding')
  checks=DEMO/'Docs/AssemblyShadow/History/M07R/R03/IR_Remediation/N_SNAPSHOT_PUBLICATION_91b7/checks'
  # Immutable historical extracts stay frozen. A current producer must be
  # identified independently, NEVER made to match old source text.
  frozen=checks/'source-extracts/GetExecutionDiagnosticsJson.function.cpp'
  need(sha(frozen.read_bytes())=='051497faf816e4094b8f4e8f194c5b877812cee034c6901c755e8b6efdff9c40',
       'Historical original execution source changed')
  sys.path.insert(0,str(checks))
  from test_snapshot_integration import body
  actual=body((NATIVE/'libil2cpp/vm/AssemblyShadow.cpp').read_text(),
              'AssemblyShadowError AssemblyShadow::GetExecutionDiagnosticsJson(std::string& json)')
  need(sha(actual.encode())=='4829a3ae7b9b0df57d054738f99d940b4ad7644a364df0502c9976469819d372',
       'Current execution producer source changed; review required')
  paths=[NATIVE/'libil2cpp/vm'/n for n in ('AssemblyShadowDiagnostics.cpp','AssemblyShadowDiagnostics.h',
         'AssemblyShadowDiagnosticWriter.h','AssemblyShadowExecutionSerializer.h','AssemblyShadowTypes.h','AssemblyShadow.cpp')]
  paths += [codec,reference,Path(__file__).resolve(),HERE/'serializer_driver.cpp',
            HERE/'allocation_probe.cpp',HERE/'current_execution_producer.py',
            HERE/'fixtures/LegacyAssemblyShadowDiagnostics.cpp',
            HERE/'fixtures/AssemblyShadowDiagnostics.h',checks/'test_diagnostic_size.py',
            checks/'test_snapshot_integration.py',frozen]
  before={str(x):{'gitBlob':blob(x.read_bytes()),'sha256':sha(x.read_bytes())} for x in paths}
  (OUT/'stubs').mkdir()
  (OUT/'stubs/il2cpp-config.h').write_text('#pragma once\n// Explicit host-only empty configuration stub. Not Unity SDK.\n')
  spec=importlib.util.spec_from_file_location('wire_reference',reference)
  REF=importlib.util.module_from_spec(spec);spec.loader.exec_module(REF)
  run([sys.executable,'-B',checks/'test_diagnostic_size.py','--output',OUT/'execution-producer'],OUT/'execution-producer-command')
  old_producer='Passed'
  # Run both actual historical and current function bodies against exactly the
  # same pinned synthetic VM inputs, with strict and sanitized compiler profiles.
  # This check must succeed BEFORE the general V2/serializer test matrix.
  run([sys.executable,'-B',HERE/'current_execution_producer.py',
       '--native',NATIVE,'--demo',DEMO,
       '--historical-output',OUT/'execution-producer',
       '--output',OUT/'current-execution-parity'],OUT/'current-execution-parity-command',timeout=240)
  parity=json.loads((OUT/'current-execution-parity/RESULT.json').read_text())
  need(parity['result']=='PassedHostOnly' and len(parity['cases'])==6 and
       len(parity['commands'])==24,'Full immutable/current producer parity missing')
  current_producer='Passed'
  EXECUTION=(OUT/'execution-producer/strict.json').read_bytes()
  need(len(EXECUTION)==331078 and len(json.loads(EXECUTION)['classes'])==1024,
       'Actual immutable synthetic execution output')
  compiler=shutil.which(os.environ.get('CXX','clang++'))
  need(compiler,'CXX compiler required')
  run([compiler,'--version'],OUT/'compiler')
  for name,flags in [('strict',[]),('sanitized',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
   PROFILE=name;folder=OUT/name;folder.mkdir();DRIVER=folder/'serializer'
   common=[compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pedantic',*flags,
           '-iquote',str(NATIVE/'libil2cpp/vm'),'-I'+str(codec.parent),'-I'+str(OUT/'stubs')]
   run([*common,'-DAssemblyShadowDiagnostics=OriginalDiagnostics','-c',
        HERE/'fixtures/LegacyAssemblyShadowDiagnostics.cpp','-o',folder/'original.o'],folder/'legacy-compile')
   run([*common,HERE/'serializer_driver.cpp',HERE/'allocation_probe.cpp',
        NATIVE/'libil2cpp/vm/AssemblyShadowDiagnostics.cpp',folder/'original.o','-o',DRIVER],folder/'current-compile')
   with (folder/'tests.log').open('w') as log:
    result=unittest.TextTestRunner(stream=log,verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(SerializerTests))
   profiles.append({'name':name,'tests':result.testsRun,'failures':len(result.failures),
                    'errors':len(result.errors),'skipped':len(result.skipped)})
   need(result.wasSuccessful() and not result.skipped and result.testsRun==25,
        'Complete serializer matrix failed: '+str(folder/'tests.log'))
  verified=True
 except Exception as error:
  failure=repr(error)
  raise
 finally:
  unchanged=all(Path(x).is_file() and
                {'gitBlob':blob(Path(x).read_bytes()),'sha256':sha(Path(x).read_bytes())}==ident
                for x,ident in before.items())
  status='PassedWithinHostScope' if verified and unchanged else 'FailedOrIncomplete'
  result={'kind':'SharedGeneralSerializerV2HostInterop','result':status,
          'profiles':profiles,'oldExecutionProducer':old_producer,
          'currentExecutionProducer':current_producer,
          'currentExecutionFunctionSha256':'4829a3ae7b9b0df57d054738f99d940b4ad7644a364df0502c9976469819d372',
          'originalExecutionFunctionSha256':'051497faf816e4094b8f4e8f194c5b877812cee034c6901c755e8b6efdff9c40',
          'sources':before,'sourceUnchanged':unchanged,'commands':RECORDS,
          'firstFailure':failure,
          'observedNewAllocationsDuringPreparedGeneralFormattingAndCaptureEncoding':0 if verified else None,
          'generalSerializer':'Actual legacy/current native product source; synthetic immutable VM snapshot',
          'executionSerializer':'Actual pinned historical/current VM function bodies with byte parity over identical synthetic VM inputs; no live bounded acquisition',
          'captureVmAllocationBoundProven':False,'liveCaptureCoherenceProven':False,
          'durableRuntimeFiles':False,'v2PlayerEnabled':False,'actualUnity':False,
          'runtimeAcceptance':False}
  (OUT/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
  rows=[{'path':str(x.relative_to(OUT)),'bytes':x.stat().st_size,'sha256':sha(x.read_bytes())}
        for x in sorted(OUT.rglob('*')) if x.is_file()]
  (OUT/'MANIFEST.json').write_text(json.dumps({'files':rows},indent=2)+'\n')
 need(verified and unchanged,'Validation incomplete')
 print(json.dumps({'result':result['result'],'profiles':profiles,'commands':len(RECORDS),
                   'executionProducerParity':'Passed'},indent=2))


if __name__=='__main__':main()
