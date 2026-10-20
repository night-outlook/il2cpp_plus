#!/usr/bin/env python3
"""R03 P1 inactive, owned physical display-name capture source/host gate.

Compiles the checked-in production capture header and .inl with explicit VM
iterator/metadata mocks. This is NOT TypeKey, Player, pointer-lifetime or
coherent VM epoch validation. Official installed Unity SDK is separate.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HEADER='libil2cpp/vm/AssemblyShadowOwnedPhysicalNames.h'
METHOD='libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesSource.inl'
PHYSICAL='libil2cpp/vm/AssemblyShadowPhysicalMembership.h'
CPP='libil2cpp/vm/AssemblyShadow.cpp'
API='libil2cpp/vm/AssemblyShadow.h'
FIXTURE='tools/r03/diagnostic_stream/owned_physical_names_fixture.cpp'
RUNNER='tools/r03/diagnostic_stream/run_owned_physical_names.py'
SOURCES=(HEADER,METHOD,PHYSICAL,CPP,API,FIXTURE,RUNNER,
         'libil2cpp/vm/AssemblyShadowExecutionRegistrationWitness.h')
# Exact source before the inactive owned-names addition; historical evidence
# source pins and all earlier fingerprint checks remain untouched.
BASE_CPP_BLOB='46240c58f83f1ce1a048266ed3bf421af4197567'
BASE_API_BLOB='55b1be3a134b41652fd88e24d8d548e5ca98126e'
BASE_PHYSICAL_BLOB='375d9225bfbb7be693b15ffc7fc7203b48e45d92'
REGISTRATION_BINDING_BLOCK="    // Read only the opaque slot and registered source of a class ALREADY\n    // identified as FirstMatch in the synchronous physical enumeration.\n    // No class metadata is touched and no pointer is returned. The slot is\n    // scoped to this registry snapshot, not a persistent class identity.\n    bool TryObservedRegistrationBinding(const void* address,size_t& slotOut,\n        assembly_shadow_execution_witness::Source& sourceOut) const noexcept\n    {\n        slotOut=kSlots;\n        sourceOut=assembly_shadow_execution_witness::Source::None;\n        if(!captured_||badEnumeration_||!address)return false;\n        size_t slot=(reinterpret_cast<uintptr_t>(address)>>3)%kSlots;\n        for(size_t probe=0;probe<kSlots;++probe)\n        {\n            const void* current=identities_[slot];\n            if(!current)return false;\n            if(current==address)\n            {\n                if(!seen_[slot])return false;\n                const auto role=static_cast<assembly_shadow_execution_witness::Source>(roles_[slot]);\n                if(role!=assembly_shadow_execution_witness::Source::Candidate &&\n                   role!=assembly_shadow_execution_witness::Source::ActiveShadow)\n                    return false;\n                slotOut=slot;\n                sourceOut=role;\n                return true;\n            }\n            slot=(slot+1)%kSlots;\n        }\n        return false;\n    }\n"
GROUPS=(
 'empty-physical-owned','one-physical-owned','two-roles-owned',
 'duplicate-enumerated-owned-once',
 'unrelated-malformed-class-not-dereferenced',
 'null-namespace-copied-as-empty',
 'owned-bytes-survive-vm-change',
 'reverse-enumeration-role-binding',
 'same-display-name-distinct-binding',
 'hash-collision-binding',
 'missing-physical-row-refused',
 'bad-matched-image-refused',
 'bad-matched-assembly-refused',
 'bad-matched-assembly-name-refused',
 'bad-matched-simple-name-refused',
 'field-overlong-refused',
 'owned-text-budget-refused',
 'source-revision-changed-after-iteration',
 'state-changed-after-iteration',
 'active-changed-after-iteration',
 'closed-admission-refused',
 'unknown-callback-null-refused',
 'binding-not-matched-refused',
 'unobserved-binding-refused',
 'invalid-slot-refused-and-scrubbed',
 'invalid-role-refused-and-scrubbed',
 'duplicate-registration-binding-refused-and-scrubbed',
 'reused-owned-capture-refused',
)
def sha(raw:bytes)->str:return hashlib.sha256(raw).hexdigest()
def blob(raw:bytes)->str:return hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
def source_identity(path:Path)->dict:
    raw=path.read_bytes()
    return {'bytes':len(raw),'sha256':sha(raw),'gitBlob':blob(raw)}
def write(path:Path,value):path.write_text(json.dumps(value,sort_keys=True,indent=2)+'\n')

def verify(native:Path)->str:
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    physical=(native/PHYSICAL).read_text()
    method=(native/METHOD).read_text()
    fixture=(native/FIXTURE).read_text()
    header=(native/HEADER).read_text()
    include='#include "AssemblyShadowOwnedPhysicalNames.h"'
    inline='#include "AssemblyShadowOwnedPhysicalNamesSource.inl"'
    forward='namespace assembly_shadow_owned_physical_names { class Capture; struct Receipt; }\n'
    declaration=(
        '    // P1 INACTIVE: copy display names synchronously for matched physical classes.\n'
        '    // No live class pointer survives; this is not an execution TypeKey.\n'
        '    static bool TryCaptureOwnedObservedPhysicalNames(\n'
        '        assembly_shadow_physical_membership::Scratch& membershipScratch,\n'
        '        assembly_shadow_owned_physical_names::Capture& names,\n'
        '        assembly_shadow_owned_physical_names::Receipt& receipt);\n')
    inline_block=(
        '// P1 inactive owned names from physical enumerator matches only.\n'
        '#include "AssemblyShadowOwnedPhysicalNamesSource.inl"\n')
    if cpp.count(include)!=1 or cpp.count(inline)!=1 or api.count(declaration)!=1 or api.count(forward)!=1:
        raise RuntimeError('Production header/source declaration missing or duplicated')
    if cpp.count('TryCaptureOwnedObservedPhysicalNames(')!=0:
        raise RuntimeError('Inactive owned-names source has an unreviewed ordinary C++ caller')
    required=('assembly_shadow_metadata_lifetime::Gate::Reader admission(',
              'os::FastAutoLock metadataLock(&g_MetadataLock);',
              'il2cpp_class_for_each(',
              'ObserveEnumeratedOwned(klass)',
              'context.names->Add(',
              'TryObservedRegistrationBinding(',
              'receipt.allRowsBoundToObservedRegistry=true;',
              'membershipScratch.SameRegistry(',
              'names.Seal(membershipScratch.Observed())',
              'receipt.namesCopiedFromMatchedEnumeratorCallbacks=true;')
    for token in required:
        if method.count(token)!=1:raise RuntimeError('Actual owned VM source contract changed: '+token)
    if fixture.count(include)!=1 or fixture.count(inline)!=1 or any(('"'+n+'"') not in fixture for n in GROUPS):
        raise RuntimeError('Host fixture does not compile actual source or all named tests')
    if physical.count('enum class ObservationEvent:uint8_t')!=1 or physical.count('ObserveEnumeratedOwned(')!=2 or physical.count(REGISTRATION_BINDING_BLOCK)!=1:
        raise RuntimeError('Physical iterator slot/source witness contract changed')
    if header.count('bool Add(size_t registrationSlot,')!=1 or header.count('bool allRowsBoundToObservedRegistry=false;')!=1 or header.count('usedRegistrationSlots_')<3:
        raise RuntimeError('Owned registration binding storage/admission contract changed')
    if physical.count('enum class ObservationEvent:uint8_t')!=1 or physical.count('ObserveEnumeratedOwned(')!=2:
        raise RuntimeError('Physical iterator first-match classification contract changed')
    oldcpp=cpp.replace(include+'\n','').replace(inline_block,'')
    oldapi=api.replace(forward,'').replace(declaration,'')
    if blob(oldcpp.encode())!=BASE_CPP_BLOB or blob(oldapi.encode())!=BASE_API_BLOB:
        raise RuntimeError('Earlier actual VM getter/physical member API unexpectedly changed')
    # The physical source is amended only to classify a first, duplicate or
    # ignored callback while preserving the existing void ObserveEnumerated API.
    # Do not claim that this establishes metadata provenance or VM coherence.
    oldEnumeration="""    // Called only from the synchronous il2cpp_class_for_each callback while
    // g_MetadataLock is held; compare address values, never dereference them.
    void ObserveEnumerated(const void* address) noexcept
    {
        if(badEnumeration_)return;
        if(!address||enumerated_==kMaximumEnumerated)
        {
            badEnumeration_=true;return;
        }
        ++enumerated_;
        size_t slot=(reinterpret_cast<uintptr_t>(address)>>3)%kSlots;
        for(size_t probe=0;probe<kSlots;++probe)
        {
            const void* current=identities_[slot];
            if(!current)return;  // class exists in enumerator but not observed
            if(current==address)
            {
                if(!seen_[slot]){seen_[slot]=1;++matched_;}
                return;
            }
            slot=(slot+1)%kSlots;
        }
        badEnumeration_=true;
    }
"""
    newEnumeration="""    // Classify a synchronous iterator callback without reading any class
    // metadata. FirstMatch is the sole point at which a separate owned-name
    // consumer may borrow fields while g_MetadataLock is held.
    ObservationEvent ObserveEnumeratedOwned(const void* address) noexcept
    {
        if(badEnumeration_)return ObservationEvent::Invalid;
        if(!address||enumerated_==kMaximumEnumerated)
        {
            badEnumeration_=true;return ObservationEvent::Invalid;
        }
        ++enumerated_;
        size_t slot=(reinterpret_cast<uintptr_t>(address)>>3)%kSlots;
        for(size_t probe=0;probe<kSlots;++probe)
        {
            const void* current=identities_[slot];
            if(!current)return ObservationEvent::Ignored;
            if(current==address)
            {
                if(!seen_[slot])
                {
                    seen_[slot]=1;++matched_;
                    return ObservationEvent::FirstMatch;
                }
                return ObservationEvent::DuplicateMatch;
            }
            slot=(slot+1)%kSlots;
        }
        badEnumeration_=true;
        return ObservationEvent::Invalid;
    }
    // Preserve the original opaque-membership API and counting semantics.
    void ObserveEnumerated(const void* address) noexcept
    {
        (void)ObserveEnumeratedOwned(address);
    }
"""
    enumAdded='enum class ObservationEvent:uint8_t {Ignored,FirstMatch,DuplicateMatch,Invalid};\n'
    if (physical.count(enumAdded)!=1 or physical.count(newEnumeration)!=1):
        raise RuntimeError('Physical enumeration source modification differs from reviewed first-match contract')
    oldPhysical=physical.replace(REGISTRATION_BINDING_BLOCK,'').replace(enumAdded,'').replace(newEnumeration,oldEnumeration)
    if blob(oldPhysical.encode())!=BASE_PHYSICAL_BLOB:
        raise RuntimeError('Original physical membership source unexpectedly changed')
    return method

def execute(argv:list,output:Path,name:str,env:dict,receipts:list,timeout=180)->bytes:
    receipt={'name':name,'argv':list(map(str,argv)),'startedUnix':time.time(),
             'status':'Failed','exit':None,'commandLifetimeUnverified':False,
             'outputCompleteness':'Unavailable'}
    stdout=stderr=None
    try:
        p=subprocess.run(list(map(str,argv)),capture_output=True,timeout=timeout,env=env)
        stdout,stderr=p.stdout,p.stderr
        receipt.update(exit=p.returncode,outputCompleteness='CompleteDirectChildStreams')
        if p.returncode==0:receipt['status']='Passed'
    except subprocess.TimeoutExpired as e:
        stdout,stderr=e.stdout,e.stderr
        receipt.update(exception=repr(e),timedOut=True,
                       commandLifetimeUnverified=True,outputCompleteness='CapturedPrefix')
    except KeyboardInterrupt:
        receipt.update(exception='KeyboardInterrupt',interrupted=True,commandLifetimeUnverified=True)
    except OSError as e:receipt['exception']=repr(e)
    except Exception as e:receipt.update(exception=repr(e),commandLifetimeUnverified=True)
    finally:
        for stream,data in (('stdout',stdout),('stderr',stderr)):
            raw=data if data is not None else b''
            (output/(name+'.'+stream+'.bin')).write_bytes(raw)
            receipt[stream+'Available']=data is not None
            receipt[stream+'CapturedBytes']=len(raw)
            receipt[stream+'Sha256']=sha(raw)
        receipt['finishedUnix']=time.time()
        receipts.append(receipt)
        write(output/(name+'.json'),receipt)
    if receipt['status']!='Passed':raise RuntimeError(name+' failed; evidence retained')
    return stdout

def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not __debug__:raise RuntimeError('Python -O is forbidden')
    native,out=args.native,args.output
    if (not native.is_absolute() or not native.is_dir() or native!=native.resolve(strict=True)
        or not out.is_absolute() or out!=out.resolve(strict=False) or out.exists()
        or not out.parent.is_dir() or native==out or native in out.parents or out in native.parents):
        raise RuntimeError('Fresh disjoint canonical source and evidence roots required')
    before={f:source_identity(native/f) for f in SOURCES}
    method=verify(native)
    out.mkdir(mode=0o700)
    compiler=shutil.which(os.environ.get('CXX','clang++'))
    env=dict(os.environ,ASAN_OPTIONS=('detect_leaks=0:halt_on_error=1' if sys.platform=='darwin'
                                      else 'detect_leaks=1:halt_on_error=1'),
             UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    commands=[];profiles={};firstError=None;postflightError=None
    try:
        if not compiler:raise RuntimeError('Host C++17 compiler unavailable')
        execute([compiler,'--version'],out,'compiler-version',env,commands,30)
        for name,flags in (('strict',['-O2']),('sanitized',[
            '-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined'])):
            exe=out/(name+'.exe')
            execute([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-pthread',
                     '-iquote',str(native/'libil2cpp/vm'),*flags,
                     str(native/FIXTURE),'-o',str(exe)],out,name+'-compile',env,commands)
            raw=execute([exe],out,name+'-run',env,commands).decode('utf-8')
            found=tuple(line[5:] for line in raw.splitlines() if line.startswith('PASS '))
            if found!=GROUPS or not raw.rstrip().endswith('P1 OWNED PHYSICAL NAMES SOURCE HOST PASS'):
                raise RuntimeError('Missing, duplicate or unordered owned-name source host groups')
            profiles[name]={'result':'PassedWithinExplicitVmMocks','caseGroups':list(found)}
    except BaseException as e:firstError=repr(e)
    finally:
        lifetime=any(c['commandLifetimeUnverified'] for c in commands)
        try:
            if lifetime:raise RuntimeError('Unverified child lifetime; skip source/Git postflight')
            after={f:source_identity(native/f) for f in SOURCES}
            preserved=before==after
        except Exception as e:
            after={};preserved=False;postflightError=repr(e)
        compiled=(preserved and all(any(c['name']==profile+'-compile' and c['status']=='Passed'
                   and c['exit']==0 for c in commands) for profile in ('strict','sanitized')))
        passed=(firstError is None and postflightError is None and preserved
                and not lifetime and compiled and len(commands)==5 and len(profiles)==2
                and all(c['status']=='Passed' for c in commands))
        result={'kind':'R03P1InactiveOwnedObservedPhysicalNamesSourceHost',
                'schemaVersion':1,'result':'PassedWithinInactiveOwnedPhysicalNamesHostScope'
                if passed else 'FailedOrIncomplete',
                'actualProductionMethodSha256':sha(method.encode()),
                'sourceBefore':before,'sourceAfter':after,'sourcePreserved':preserved,
                'commands':commands,'profiles':profiles,'firstError':firstError,
                'postflightError':postflightError,'commandLifetimeUnverified':lifetime,
                'actualSourceCompiled':compiled,'metadataAndIteratorInputsMocked':True,
                'ownedPhysicalNamesOnly':True,'completeExecutionTypeKeys':False,
                'physicalClassPointerOriginProven':False,'metadataPointerLifetimeProven':False,
                'coherentVmEpochProven':False,'officialUnitySdkCompiled':False,
                'UnityPlayerExecuted':False,'runtimeAcceptance':False}
        write(out/'RESULT.json',result)
        files=[{'path':str(p.relative_to(out)),'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())}
               for p in sorted(out.rglob('*')) if p.is_file() and not p.is_symlink()]
        write(out/'MANIFEST.json',{'files':files,
             'inventoryScope':'UnsealedObservedFiles' if lifetime else 'CompletedHostFiles',
             'commandLifetimeUnverified':lifetime})
    print(json.dumps({'result':result['result'],'caseGroupsPerProfile':len(GROUPS),
                      'profiles':list(profiles)}))
    if not passed:raise SystemExit(1)
if __name__=='__main__':main()
