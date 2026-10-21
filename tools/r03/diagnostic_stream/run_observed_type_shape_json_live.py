#!/usr/bin/env python3
"""R03 P1 genuine observed VM class -> pointer-free owned shape JSON host.

Compiles the THREE real production source .inl methods plus pure Census/JSON
under controlled class/lock/registry/Reader mocks. No Unity Editor/Player,
canonical execution TypeKey, physical allocation provenance, or runtime proof.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
from run_owned_physical_names import execute,sha,source_identity,write
from run_observed_type_shape_json import (
    verify_source as verify_previous,
    NAME_GROUPS,SHAPE_GROUPS,LIVE_GROUPS,GROUPS as JSON_GROUPS
)

CPP="libil2cpp/vm/AssemblyShadow.cpp"
API="libil2cpp/vm/AssemblyShadow.h"
SOURCE="libil2cpp/vm/AssemblyShadowObservedTypeShapeJsonLiveSource.inl"
SHAPE_SOURCE="libil2cpp/vm/AssemblyShadowObservedTypeShapeLiveSource.inl"
NAME_SOURCE="libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesSource.inl"
JSON="libil2cpp/vm/AssemblyShadowObservedTypeShapeJson.h"
CENSUS="libil2cpp/vm/AssemblyShadowObservedTypeShapeCensus.h"
FIXTURE="tools/r03/diagnostic_stream/observed_type_shape_json_live_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_observed_type_shape_json_live.py"
SOURCES=(CPP,API,SOURCE,SHAPE_SOURCE,NAME_SOURCE,JSON,CENSUS,FIXTURE,RUNNER,
         "libil2cpp/vm/AssemblyShadowObservedTypeShape.h",
         "libil2cpp/vm/AssemblyShadowOwnedPhysicalNames.h",
         "libil2cpp/vm/AssemblyShadowPhysicalMembership.h",
         "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
         "tools/r03/diagnostic_stream/run_observed_type_shape_json.py",
         "tools/r03/diagnostic_stream/run_observed_type_shape_live.py",
         "tools/r03/diagnostic_stream/run_observed_type_shape_census.py",
         "tools/r03/diagnostic_stream/run_owned_physical_names.py")
NEW_GROUPS=(
 "empty-real-vm-shape-json",
 "plain-reference-real-vm-json",
 "plain-value-real-vm-json",
 "generic-definition-real-vm-json",
 "constructed-generic-real-vm-json",
 "nested-definition-real-vm-json",
 "ranked-array-real-vm-json",
 "missing-definition-real-vm-json",
 "unsupported-shape-real-vm-json",
 "mixed-candidate-active-real-json",
 "unmatched-source-real-vm-json-refused",
 "revision-after-iterator-json-refused",
 "small-budget-atomic-after-real-source",
 "oversized-budget-no-iterator",
 "reused-complete-json-revokes-source",
 "stale-census-blocked-before-iterator",
)
EXPECTED=(tuple("PASS "+x for x in NAME_GROUPS)+
          tuple("PASS SHAPE "+x for x in SHAPE_GROUPS)+
          tuple("PASS LIVE CENSUS "+x for x in LIVE_GROUPS)+
          tuple("PASS SHAPE JSON "+x for x in JSON_GROUPS)+
          tuple("PASS LIVE SHAPE JSON "+x for x in NEW_GROUPS))

def verify_source(native:Path)->dict:
    predecessor=verify_previous(native)
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    source=(native/SOURCE).read_text()
    fixture=(native/FIXTURE).read_text()
    fn="TryCaptureObservedTypeShapeJson("
    if (cpp.count('#include "AssemblyShadowObservedTypeShapeJson.h"')!=1 or
        cpp.count('#include "AssemblyShadowObservedTypeShapeJsonLiveSource.inl"')!=1 or
        api.count("static bool "+fn)!=1 or
        source.count("bool AssemblyShadow::"+fn)!=1 or
        cpp.count(fn)!=0):
        raise RuntimeError("Inactive genuine shape JSON method not included exactly once")
    must=(
      'namesReceipt=assembly_shadow_owned_physical_names::Receipt{};',
      'jsonReceipt=json::Receipt{};',
      'jsonOutput.Fresh()',
      'census::Fresh(censusResult)',
      'maximumBytes>json::Record::kMaximumBytes',
      'census::Fail(censusResult,census::Error::ReusedCensus)',
      'census::Fail(censusResult,census::Error::InvalidShapeCensus)',
      'json::Fail(jsonOutput,jsonReceipt,json::Error::ReusedOutput)',
      'json::Fail(jsonOutput,jsonReceipt,json::Error::InvalidOutputLimit)',
      'TryCaptureObservedTypeShapeCensus(',
      'json::Compose(censusResult,jsonOutput,jsonReceipt,maximumBytes)',
    )
    if any(part not in source for part in must):
        raise RuntimeError("Genuine VM owned JSON preflight/source/format contract changed")
    if (source.index('jsonOutput.Fresh()')>
        source.index('TryCaptureObservedTypeShapeCensus(') or
        source.index('census::Fresh(censusResult)')>
        source.index('TryCaptureObservedTypeShapeCensus(') or
        source.index('TryCaptureObservedTypeShapeCensus(')>
        source.index('json::Compose(')):
        raise RuntimeError("Shape-JSON preflight/Reader release/source call order changed")
    effective="\n".join(line.split("//",1)[0] for line in source.splitlines())
    for bad in ('g_MetadataLock','ExecutionObservationLock','il2cpp_class_for_each(',
                'klass->','Il2CppClass*','MetadataCache::','Class::',
                'std::vector<','std::string ','fopen('):
        if bad in effective:
            raise RuntimeError("New bridge obtained a VM pointer/lock/heap/file: "+bad)
    for token in (
       '#include "AssemblyShadowOwnedPhysicalNamesSource.inl"',
       '#include "AssemblyShadowObservedTypeShapeLiveSource.inl"',
       '#include "AssemblyShadowObservedTypeShapeJsonLiveSource.inl"',
       '#include "AssemblyShadowObservedTypeShapeJson.h"'):
        if fixture.count(token)!=1:
            raise RuntimeError("Fixture not compiling the actual VM sources: "+token)
    actual=tuple(re.findall(r'\bactualJson\("([^"]+)"',fixture))
    extra=tuple(re.findall(r'^\s*std::cout<<"PASS LIVE SHAPE JSON ([^"]+)\\n";',fixture,re.M))
    # Direct helper cases precede three dedicated stale-state assertions.
    if actual!=NEW_GROUPS[:-2] or extra!=NEW_GROUPS[-2:]:
        # Source uses 14 actualJson cases and 2 standalone tests.
        raise RuntimeError("Exact source-bound new 16 tests absent/duplicated: "+
                           repr((actual,extra)))
    return {
        "precedingActualVmAndPureJsonSourceVerified":predecessor,
        "originalNativeCppAndApiFingerprintPreserved":True,
        "threeActualVmSourceMethodsCompiled":True,
        "preflightBeforeIterator":True,
        "sourceLocksReleasedBeforePureJson":True,
        "ownedPointerFreeJsonOnly":True,
        "oldNameGroups":list(NAME_GROUPS),
        "oldShapeGroups":list(SHAPE_GROUPS),
        "oldLiveCensusGroups":list(LIVE_GROUPS),
        "oldShapeJsonGroups":list(JSON_GROUPS),
        "newBridgeGroups":list(NEW_GROUPS),
        "canonicalExecutionTypeKeys":False,
        "coherentVmEpochProven":False,
        "physicalClassPointerOriginProven":False,
        "fullExecutionV1":False,
        "runtimeAcceptance":False
    }

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Python optimized bytecode disallowed")
    native,out=a.native,a.output
    if (not native.is_absolute() or native!=native.resolve(strict=True) or
        not native.is_dir() or not out.is_absolute() or
        out!=out.resolve(strict=False) or out.exists() or
        not out.parent.is_dir() or native==out or
        native in out.parents or out in native.parents):
        raise RuntimeError("Fresh canonical disjoint source and evidence paths required")
    before={p:source_identity(native/p) for p in SOURCES}
    contract=verify_source(native)
    out.mkdir(mode=0o700)
    cxx=shutil.which(os.environ.get("CXX","clang++"))
    env=dict(os.environ,
        ASAN_OPTIONS=("detect_leaks=0:halt_on_error=1" if sys.platform=="darwin"
            else "detect_leaks=1:halt_on_error=1"),
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    commands=[];profiles={};firstError=None;postflightError=None
    try:
        if not cxx:raise RuntimeError("C++17 compiler unavailable")
        execute([cxx,"--version"],out,"compiler-version",env,commands,30)
        for mode,flags in (("strict",["-O2"]),
           ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
               "-fsanitize=address,undefined"])):
            exe=out/(mode+".exe")
            execute([cxx,"-std=c++17","-Wall","-Wextra","-Werror",
                     "-pedantic","-pthread","-iquote",native/"libil2cpp/vm",
                     *flags,native/FIXTURE,"-o",exe],out,mode+"-compile",env,commands,180)
            raw=execute([exe],out,mode+"-run",env,commands,180).decode("utf-8")
            actual=tuple(x for x in raw.splitlines() if x.startswith("PASS "))
            if actual!=EXPECTED or not raw.rstrip().endswith(
                    "P1 GENUINE VM SHAPE JSON HOST PASS"):
                raise RuntimeError("Source-bound actual 91-group host case sequence drift")
            profiles[mode]={
                "result":"PassedWithinInactiveVmShapeJsonHostScope",
                "oldNameGroups":list(NAME_GROUPS),
                "oldShapeGroups":list(SHAPE_GROUPS),
                "oldLiveCensusGroups":list(LIVE_GROUPS),
                "oldShapeJsonGroups":list(JSON_GROUPS),
                "newBridgeGroups":list(NEW_GROUPS)}
    except BaseException as e:firstError=repr(e)
    finally:
        lifetime=any(c["commandLifetimeUnverified"] for c in commands)
        try:
            if lifetime:raise RuntimeError("Unsafe postflight after unknown child lifetime")
            after={p:source_identity(native/p) for p in SOURCES}
            preserved=before==after
        except Exception as e:
            after={};preserved=False;postflightError=repr(e)
        compiled=(preserved and all(any(c["name"]==mode+"-compile" and
            c["status"]=="Passed" and c["exit"]==0 for c in commands)
            for mode in ("strict","sanitized")))
        passed=(firstError is None and postflightError is None and
                not lifetime and compiled and len(commands)==5 and
                len(profiles)==2 and all(c["status"]=="Passed" for c in commands))
        record={"kind":"R03P1InactiveLiveObservedTypeShapeJsonSourceHost",
            "schemaVersion":1,
            "result":"PassedWithinInactiveVmShapeJsonHostScope" if passed else "FailedOrIncomplete",
            "sourceContract":contract,"sourceBefore":before,"sourceAfter":after,
            "sourcePreserved":preserved,"commands":commands,"profiles":profiles,
            "actualSourceCompiled":compiled,"firstError":firstError,
            "postflightError":postflightError,
            "commandLifetimeUnverified":lifetime,
            "canonicalExecutionTypeKeys":False,"fullExecutionV1":False,
            "physicalClassPointerOriginProven":False,
            "coherentVmEpochProven":False,"officialUnitySdkCompiled":False,
            "UnityPlayerExecuted":False,"runtimeAcceptance":False}
        write(out/"RESULT.json",record)
        entries=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
             "sha256":sha(p.read_bytes())}
            for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        write(out/"MANIFEST.json",{"files":entries,
          "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
          "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":record["result"],"newBridgeCases":len(NEW_GROUPS),
        "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
