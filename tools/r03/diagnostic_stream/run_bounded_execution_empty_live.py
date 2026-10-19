#!/usr/bin/env python3
"""P1 inactive zero-class execution/R02 *actual method* host gate.

Exact native .h/.inl bytes are compiled with source-identified VM mocks.
This is NOT official SDK compilation, live pointer provenance, coherent epoch,
real class enumeration, completed execution diagnostics, or Player acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

H='libil2cpp/vm/AssemblyShadowBoundedExecutionEmptyLive.h'
INL='libil2cpp/vm/AssemblyShadowBoundedExecutionEmptyLiveSource.inl'
CPP='libil2cpp/vm/AssemblyShadow.cpp'
API='libil2cpp/vm/AssemblyShadow.h'
MOCK='tools/r03/diagnostic_stream/empty_live_fixture/'
MOCK_FILES=('MockRuntime.h','AssemblyShadowFrozenExecutionRecord.h',
            'AssemblyShadowMetadataLifetime.h','fixture.cpp')
SOURCES=(H,INL,CPP,API,
         'libil2cpp/vm/AssemblyShadowR02Snapshot.h',
         'libil2cpp/vm/AssemblyShadowFrozenExecutionRecord.h',
         'libil2cpp/vm/AssemblyShadowMetadataLifetime.h',
         'tools/r03/diagnostic_stream/run_bounded_execution_empty_live.py',
         *(MOCK+n for n in MOCK_FILES))
GROUPS=(
 'level2-zero-class','level1-zero-class','level0-zero-class',
 'reused-frozen-output','metadata-closed','nonempty-class-inventory',
 'inconsistent-class-inventory','counter-first-failure','counter-second-failure',
 'dropped-threads-refused','saturated-refused','dropped-class-rows-refused',
 'state-changed-mid-source','class-added-mid-source',
 'state-changed-during-format','counter-changed-during-format',
 'generation-changed-during-format','class-added-during-format',
 'begin-refusal','freeze-refusal',
)
REQUIRED=(
 'assembly_shadow_metadata_lifetime::Gate::Reader admission(',
 'ExecutionObservationLock lock;',
 'for(const Il2CppClass* klass:s_executionClasses)',
 'if(before.count!=0)',
 'Snapshot::TryCaptureCurrent(observed,receipt.countersBefore)',
 'output.Begin({',
 'output.Freeze()',
 'Snapshot::TryCaptureCurrent(finishedCounters,receipt.countersAfter)',
 'live::Equivalent(observed,finishedCounters)',
 'receipt.zeroClassSubsetComplete=true;',
)
def digest(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def identity(p:Path)->dict:
    data=p.read_bytes()
    blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    return {'bytes':len(data),'sha256':digest(data),'gitBlob':blob}
def write(p:Path,value)->None:p.write_text(json.dumps(value,sort_keys=True,indent=2)+'\n')

def verify(native:Path)->str:
    inl=(native/INL).read_text()
    header=(native/H).read_text()
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    method='bool AssemblyShadow::TryCaptureBoundedEmptyExecutionLive('
    include='#include "AssemblyShadowBoundedExecutionEmptyLiveSource.inl"'
    if inl.count(method)!=1 or cpp.count(include)!=1:
        raise RuntimeError('Actual production method must exist and be included once')
    if cpp.count('#include "AssemblyShadowBoundedExecutionEmptyLive.h"')!=1:
        raise RuntimeError('Actual production header must be included once')
    if api.count('static bool TryCaptureBoundedEmptyExecutionLive(')!=1:
        raise RuntimeError('Actual native API declaration missing or duplicated')
    if cpp.count('TryCaptureBoundedEmptyExecutionLive(')!=0:
        raise RuntimeError('Inactive method unexpectedly invoked from production cpp')
    if any(inl.count(s)!=1 for s in REQUIRED):
        raise RuntimeError('Native admission/zero-class/complete-counter source contract changed')
    if not all(('"'+n+'"' in (native/(MOCK+'fixture.cpp')).read_text()) for n in
               ('MockRuntime.h','AssemblyShadowBoundedExecutionEmptyLive.h','AssemblyShadowBoundedExecutionEmptyLiveSource.inl')):
        raise RuntimeError('Mock must compile production header/inl, not transcription')
    fixture=(native/(MOCK+'fixture.cpp')).read_text()
    if not all('"'+name+'"' in fixture for name in GROUPS):
        raise RuntimeError('Missing or changed test case')
    return inl

def run(argv:list,output:Path,name:str,env:dict,receipts:list,timeout=180)->bytes:
    receipt={'name':name,'argv':list(map(str,argv)),'startedUnix':time.time(),
             'status':'Failed','exit':None,'outputCompleteness':'Unavailable',
             'commandLifetimeUnverified':False,'timedOut':False,'interrupted':False}
    stdout=stderr=None
    try:
        p=subprocess.run(list(map(str,argv)),capture_output=True,timeout=timeout,env=env)
        stdout,stderr=p.stdout,p.stderr
        receipt['exit']=p.returncode
        receipt['outputCompleteness']='CompleteDirectChildStreams'
        if p.returncode==0:receipt['status']='Passed'
    except subprocess.TimeoutExpired as e:
        stdout,stderr=e.stdout,e.stderr
        receipt['exception']=repr(e);receipt['outputCompleteness']='CapturedPrefix'
        receipt['commandLifetimeUnverified']=True;receipt['timedOut']=True
    except OSError as e:
        # Known spawn failure: no process was started by subprocess.run.
        receipt['exception']=repr(e)
    except BaseException as e:
        # KeyboardInterrupt or an unknown post-launch error cannot certify
        # child/descendant termination. Preserve a fail-stop receipt.
        receipt['exception']=repr(e)
        receipt['commandLifetimeUnverified']=True
        receipt['interrupted']=isinstance(e,KeyboardInterrupt)
    finally:
        for stream,raw in (('stdout',stdout),('stderr',stderr)):
            data=raw if raw is not None else b''
            (output/(name+'.'+stream+'.bin')).write_bytes(data)
            receipt[stream+'Available']=raw is not None
            receipt[stream+'Sha256']=digest(data)
            receipt[stream+'CapturedBytes']=len(data)
        receipt['finishedUnix']=time.time()
        receipts.append(receipt)
        write(output/(name+'.json'),receipt)
    if receipt['status']!='Passed':
        raise RuntimeError(name+' failed; source+captured output preserved')
    return stdout

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--native',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    if not __debug__:raise RuntimeError('Python -O forbidden')
    native,output=a.native,a.output
    if (not native.is_absolute() or not native.is_dir() or native!=native.resolve(strict=True)
        or not output.is_absolute() or output!=output.resolve(strict=False) or output.exists()
        or not output.parent.is_dir() or native==output or native in output.parents
        or output in native.parents):raise RuntimeError('Fresh external canonical root required')
    before={name:identity(native/name) for name in SOURCES}
    code=verify(native)
    # Create evidence before compiler discovery, so a missing executable has
    # an accurate FailedOrIncomplete summary instead of no summary.
    output.mkdir(mode=0o700)
    stub=output/'host-only-includes';stub.mkdir()
    for p in (H,INL):(stub/Path(p).name).write_bytes((native/p).read_bytes())
    for p in MOCK_FILES[1:3]:(stub/p).write_bytes((native/(MOCK+p)).read_bytes())
    env=dict(os.environ,ASAN_OPTIONS=('detect_leaks=0:halt_on_error=1' if sys.platform=='darwin'
                                        else 'detect_leaks=1:halt_on_error=1'),
             UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    receipts=[];profiles={};firstError=None
    try:
        cxx=shutil.which(os.environ.get('CXX','clang++'))
        if not cxx:raise RuntimeError('C++17 host compiler unavailable')
        run([cxx,'--version'],output,'compiler-version',env,receipts,30)
        for profile,extra in (('strict',['-O2']),('sanitized',['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined'])):
            exe=output/(profile+'.exe')
            run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-pthread',
                 '-iquote',str(stub),'-iquote',str(native/MOCK),*extra,
                 native/(MOCK+'fixture.cpp'),'-o',exe],output,profile+'-compile',env,receipts)
            raw=run([exe],output,profile+'-run',env,receipts).decode('utf-8')
            matches=tuple(x[5:] for x in raw.splitlines() if x.startswith('PASS '))
            if matches!=GROUPS or not raw.rstrip().endswith('P1 EMPTY LIVE EXECUTION SOURCE HOST PASS'):
                raise RuntimeError('Unexpected/duplicate/missing source test groups '+profile)
            profiles[profile]={'result':'PassedWithinHostScope','caseGroups':list(matches)}
    except BaseException as e:
        firstError=repr(e)
    finally:
        # Per-command uncertainty MUST propagate into the aggregate consumed
        # by the outer fail-stop scheduler. Do not run source postflight when
        # detached children may still exist. Never label the inventory sealed.
        lifetimeUnverified=any(x['commandLifetimeUnverified'] for x in receipts)
        postflightError=None
        if lifetimeUnverified:
            after={};preserved=False
            postflightError='NotRun: command lifetime unverified'
        else:
            try:
                after={name:identity(native/name) for name in SOURCES}
                preserved=before==after
            except Exception as e:
                after={};preserved=False
                postflightError=repr(e)
                firstError=(firstError or '')+' source-postflight '+repr(e)
        # Real successful source-bound compiler receipts, not declarations or
        # a compiler-version probe, are the ONLY basis for this claim.
        successfulCompiles=[name for name in ('strict-compile','sanitized-compile')
            if any(row['name']==name and row['status']=='Passed' and row['exit']==0
                   for row in receipts)]
        sourceCompiled=(len(successfulCompiles)==2 and preserved)
        passed=(firstError is None and preserved and not lifetimeUnverified and
                sourceCompiled and len(receipts)==5 and
                all(x['status']=='Passed' for x in receipts) and len(profiles)==2)
        result={'kind':'R03P1EmptyExecutionLiveSourceHost','schemaVersion':1,
                'result':'PassedWithinInactiveZeroClassHostScope' if passed else 'FailedOrIncomplete',
                'originalProductionMethodSha256':digest(code.encode()),
                'sourceBefore':before,'sourceAfter':after,'sourcePreserved':preserved,
                'commands':receipts,'profiles':profiles,'firstError':firstError,
                'commandLifetimeUnverified':lifetimeUnverified,
                'descendantQuiescenceProven':False,
                'postflightError':postflightError,
                'successfulSourceCompileProfiles':successfulCompiles,
                'actualProductionHeaderAndMethodCompiled':sourceCompiled,
                'nativeVmInputsExplicitlyMocked':True,
                'zeroClassSubsetOnly':True,'completeExecutionSource':False,
                'coherentVmEpochProven':False,'classPointerOriginProven':False,
                'officialUnitySdkCompiled':False,'UnityPlayerExecuted':False,
                'runtimeAcceptance':False}
        write(output/'RESULT.json',result)
        manifest=[{'path':str(p.relative_to(output)),'bytes':p.stat().st_size,
                   'sha256':digest(p.read_bytes())} for p in sorted(output.rglob('*'))
                  if p.is_file() and not p.is_symlink()]
        write(output/'MANIFEST.json',{'files':manifest,
              'inventoryScope':'UnsealedObservedFiles' if lifetimeUnverified else 'CompletedHostFiles',
              'commandLifetimeUnverified':lifetimeUnverified})
    print(json.dumps({'result':result['result'],'profiles':list(profiles),
                      'testsPerProfile':len(GROUPS)}))
    if any(x['interrupted'] for x in receipts):raise SystemExit(130)
    if not passed:raise SystemExit(1)
if __name__=='__main__':main()
