#!/usr/bin/env python3
"""R03 P1 genuine INACTIVE physical-class iterator -> owned shape census.

Compiles BOTH actual production VM .inl methods and the real pointer-free
census in one source-bound host fixture. VM metadata, registry, locks, and
pointer lifetime are explicit mocks; no canonical TypeKeys or Player proof.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import sys

from run_owned_physical_names import execute,sha,source_identity,write,GROUPS as NAME_GROUPS
from run_observed_type_shape_census import source_contract as verify_shape_source,SHAPE_GROUPS

CPP="libil2cpp/vm/AssemblyShadow.cpp"
API="libil2cpp/vm/AssemblyShadow.h"
SOURCE="libil2cpp/vm/AssemblyShadowObservedTypeShapeLiveSource.inl"
NAMES="libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesSource.inl"
CENSUS="libil2cpp/vm/AssemblyShadowObservedTypeShapeCensus.h"
FIXTURE="tools/r03/diagnostic_stream/observed_type_shape_live_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_observed_type_shape_live.py"
SOURCES=(CPP,API,SOURCE,NAMES,CENSUS,
         "libil2cpp/vm/AssemblyShadowObservedTypeShape.h",
         "libil2cpp/vm/AssemblyShadowOwnedPhysicalNames.h",
         "libil2cpp/vm/AssemblyShadowPhysicalMembership.h",
         "libil2cpp/vm/AssemblyShadowExecutionRegistrationWitness.h",
         "libil2cpp/il2cpp-class-internals.h",
         "tools/r03/diagnostic_stream/run_owned_physical_names.py",
         "tools/r03/diagnostic_stream/run_observed_type_shape_census.py",
         "tools/r03/diagnostic_stream/owned_type_shape_census_fixture.cpp",
         FIXTURE,RUNNER)
LIVE_GROUPS=(
 "empty-iterator-complete-only-subset",
 "firstmatched-plain-reference",
 "firstmatched-plain-value",
 "generic-definition-remains-uncertified",
 "constructed-generic-remains-uncertified",
 "nested-definition-remains-uncertified",
 "ranked-array-remains-uncertified",
 "missing-definition-handle-is-not-candidate",
 "unsupported-typecode-is-not-candidate",
 "conflicting-metadata-flags-conservative",
 "mixed-candidate-active-noncanonical-cohort",
 "unmatched-observed-class-refused",
 "invalid-physical-matched-name-refused",
 "revision-after-metadata-pass-refused",
 "stale-output-rejected-before-iterator",
 "reused-success-revoked-before-iterator",
)
EXPECTED=(tuple("PASS "+n for n in NAME_GROUPS)+
          tuple("PASS SHAPE "+n for n in SHAPE_GROUPS)+
          tuple("PASS LIVE CENSUS "+n for n in LIVE_GROUPS))
def verify_source(native:Path)->dict:
    previous=verify_shape_source(native)
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    src=(native/SOURCE).read_text()
    fixture=(native/FIXTURE).read_text()
    method="TryCaptureObservedTypeShapeCensus("
    if (cpp.count('#include "AssemblyShadowObservedTypeShapeCensus.h"')!=1 or
        cpp.count('#include "AssemblyShadowObservedTypeShapeLiveSource.inl"')!=1 or
        api.count("static bool "+method)!=1 or
        src.count("bool AssemblyShadow::"+method)!=1 or
        cpp.count(method)!=0):
        raise RuntimeError("Actual inactive VM census source or API declaration missing")
    required=(
      "shapes::Fresh(censusResult)",
      "TryCaptureOwnedObservedPhysicalNames(membershipScratch,names,namesReceipt)",
      "shapes::Build(names,namesReceipt,censusResult)",
      "names.Revoke();",
      "shapes::Fail(censusResult,shapes::Error::ReusedCensus)",
      "shapes::Fail(censusResult,shapes::Error::IncompleteCapture)"
    )
    if any(k not in src for k in required):
        raise RuntimeError("Real live census bridge changed its fail-stop/source order")
    if (src.index("shapes::Fresh(censusResult)")>
        src.index("TryCaptureOwnedObservedPhysicalNames(") or
        src.index("TryCaptureOwnedObservedPhysicalNames(")>
        src.index("shapes::Build(") or
        src.rfind("names.Revoke();")<src.index("shapes::Build(")):
        raise RuntimeError("Source/lock/one-use census ordering changed")
    source_code="\n".join(line.split("//",1)[0] for line in src.splitlines())
    for bad in ("g_MetadataLock","il2cpp_class_for_each(", "ExecutionObservationLock",
                "klass->","Il2CppClass*","MetadataCache::",
                "std::vector<","std::string ","fopen("):
        if bad in source_code:
            raise RuntimeError("Bridge performed new VM metadata/pointer/file operation: "+bad)
    if fixture.count('#include "AssemblyShadowOwnedPhysicalNamesSource.inl"')!=1 or \
       fixture.count('#include "AssemblyShadowObservedTypeShapeLiveSource.inl"')!=1 or \
       fixture.count('#include "AssemblyShadowObservedTypeShapeCensus.h"')!=1:
        raise RuntimeError("Source fixture must compile both actual VM methods")
    for name in LIVE_GROUPS:
        if fixture.count('liveCensusTest("'+name+'"')!=1 and \
           fixture.count('"PASS LIVE CENSUS '+name+'')!=1:
            raise RuntimeError("Missing/duplicated genuine source integration test "+name)
    return {
        "previousMatchedShapeSourceContractVerified":previous,
        "originalNativeCppAndApiFingerprintPreserved":True,
        "actualNativeSourceMethodsCompiled":2,
        "sourceLocksReleasedBeforePureCensus":True,
        "oneShotCensusPreflightBeforeIterator":True,
        "candidateShapeOnlyNotCanonicalIdentity":True,
        "originalNameGroups":list(NAME_GROUPS),
        "originalShapeGroups":list(SHAPE_GROUPS),
        "liveIntegrationGroups":list(LIVE_GROUPS),
        "physicalClassPointerOriginProven":False,
        "canonicalExecutionTypeKeys":False,
        "coherentVmEpochProven":False,
        "runtimeAcceptance":False}
def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    if not __debug__:raise RuntimeError("Python -O forbidden")
    native,dest=args.native,args.output
    if (not native.is_absolute() or not native.is_dir() or
        native!=native.resolve(strict=True) or not dest.is_absolute() or
        dest!=dest.resolve(strict=False) or dest.exists() or
        not dest.parent.is_dir() or native==dest or
        native in dest.parents or dest in native.parents):
        raise RuntimeError("Fresh, canonical, separate source/evidence paths required")
    before={p:source_identity(native/p) for p in SOURCES}
    source_audit=verify_source(native)
    dest.mkdir(mode=0o700)
    cc=shutil.which(os.environ.get("CXX","clang++"))
    env=dict(os.environ,
             ASAN_OPTIONS=("detect_leaks=0:halt_on_error=1" if sys.platform=="darwin"
                           else "detect_leaks=1:halt_on_error=1"),
             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    commands=[];profiles={};firstError=None;postflightError=None
    try:
        if not cc:raise RuntimeError("C++17 host compiler unavailable")
        execute([cc,"--version"],dest,"compiler-version",env,commands,30)
        for profile,flags in (("strict",["-O2"]),
              ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                            "-fsanitize=address,undefined"])):
            exe=dest/(profile+".exe")
            execute([cc,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                     "-pthread","-iquote",native/"libil2cpp/vm",*flags,
                     native/FIXTURE,"-o",exe],dest,profile+"-compile",env,commands,180)
            raw=execute([exe],dest,profile+"-run",env,commands,180).decode("utf-8")
            actual=tuple(line for line in raw.splitlines() if line.startswith("PASS "))
            if actual!=EXPECTED or not raw.rstrip().endswith("P1 LIVE TYPE SHAPE CENSUS HOST PASS"):
                raise RuntimeError("Original/live ordered source-bound test groups mismatch")
            profiles[profile]={"result":"PassedWithinInactiveLiveTypeShapeCensusHostScope",
                               "originalNameGroups":list(NAME_GROUPS),
                               "originalShapeGroups":list(SHAPE_GROUPS),
                               "liveIntegrationGroups":list(LIVE_GROUPS)}
    except BaseException as exc:
        firstError=repr(exc)
    finally:
        lifetime=any(c["commandLifetimeUnverified"] for c in commands)
        try:
            if lifetime:raise RuntimeError("Unverified child lifetime; unsafe source postflight refused")
            after={p:source_identity(native/p) for p in SOURCES}
            source_preserved=before==after
        except Exception as exc:
            after={};source_preserved=False;postflightError=repr(exc)
        compiled=source_preserved and all(any(
              c["name"]==profile+"-compile" and c["status"]=="Passed" and c["exit"]==0
              for c in commands) for profile in ("strict","sanitized"))
        passed=(firstError is None and postflightError is None and compiled and
                not lifetime and source_preserved and len(commands)==5 and
                len(profiles)==2 and all(c["status"]=="Passed" for c in commands))
        record={"kind":"R03P1InactiveLiveObservedTypeShapeCensusHost",
                "schemaVersion":1,
                "result":"PassedWithinInactiveLiveTypeShapeCensusHostScope" if passed else "FailedOrIncomplete",
                "profiles":profiles,"sourceContract":source_audit,
                "sourceBefore":before,"sourceAfter":after,"sourcePreserved":source_preserved,
                "commands":commands,"actualSourceCompiled":compiled,
                "firstError":firstError,"postflightError":postflightError,
                "commandLifetimeUnverified":lifetime,
                "canonicalExecutionTypeKeys":False,
                "physicalClassPointerOriginProven":False,
                "coherentVmEpochProven":False,
                "officialUnitySdkCompiled":False,
                "UnityPlayerExecuted":False,
                "runtimeAcceptance":False}
        write(dest/"RESULT.json",record)
        files=[{"path":str(p.relative_to(dest)),"bytes":p.stat().st_size,
                "sha256":sha(p.read_bytes())} for p in sorted(dest.rglob("*"))
               if p.is_file() and not p.is_symlink()]
        write(dest/"MANIFEST.json",{"files":files,
            "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
            "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":record["result"],"liveCasesPerProfile":len(LIVE_GROUPS),
                      "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)
if __name__=="__main__":main()
