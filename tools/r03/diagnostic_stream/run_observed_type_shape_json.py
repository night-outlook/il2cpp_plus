#!/usr/bin/env python3
"""R03 P1 inactive, pointer-free, noncanonical observed type-shape JSON source.

Compile actual two VM .inl methods, pure owned type-shape census and bounded
JSON header under explicit VM mocks. Exact CI source/stream receipts; NOT
execution V1, canonical TypeKeys, VM epoch, pointer lifetime or Unity Player.
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
from run_observed_type_shape_live import (
    verify_source as verify_live_source,NAME_GROUPS,SHAPE_GROUPS,LIVE_GROUPS
)

HEADER="libil2cpp/vm/AssemblyShadowObservedTypeShapeJson.h"
CENSUS="libil2cpp/vm/AssemblyShadowObservedTypeShapeCensus.h"
SHAPE="libil2cpp/vm/AssemblyShadowObservedTypeShape.h"
NAMES="libil2cpp/vm/AssemblyShadowOwnedPhysicalNames.h"
WRITER="libil2cpp/vm/AssemblyShadowDiagnosticWriter.h"
FIXTURE="tools/r03/diagnostic_stream/observed_type_shape_json_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_observed_type_shape_json.py"
SOURCES=(HEADER,CENSUS,SHAPE,NAMES,WRITER,FIXTURE,RUNNER,
    "tools/r03/diagnostic_stream/run_observed_type_shape_live.py",
    "tools/r03/diagnostic_stream/observed_type_shape_live_fixture.cpp",
    "tools/r03/diagnostic_stream/run_observed_type_shape_census.py",
    "tools/r03/diagnostic_stream/run_owned_physical_names.py",
    "libil2cpp/vm/AssemblyShadowObservedTypeShapeLiveSource.inl",
    "libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesSource.inl",
    "libil2cpp/vm/AssemblyShadow.cpp","libil2cpp/vm/AssemblyShadow.h")
GROUPS=(
    "empty-census-exact-json",
    "plain-reference-candidate-only",
    "genuine-iterator-value-source-json",
    "genuine-iterator-generic-excluded",
    "genuine-mixed-role-shape-cohort",
    "output-independent-after-source-mutation",
    "incomplete-census-zero-output",
    "forged-typekey-or-epoch-refused",
    "inconsistent-role-count-refused",
    "inconsistent-category-census-refused",
    "unmatched-callback-category-refused",
    "saturated-revision-refused",
    "small-byte-budget-atomic-refusal",
    "oversize-output-request-refused",
    "reused-output-scrubs-earlier-success",
    "failed-output-cannot-be-reused",
)
EXPECTED=(tuple("PASS "+n for n in NAME_GROUPS)+
          tuple("PASS SHAPE "+n for n in SHAPE_GROUPS)+
          tuple("PASS LIVE CENSUS "+n for n in LIVE_GROUPS)+
          tuple("PASS SHAPE JSON "+n for n in GROUPS))

def verify_source(native:Path)->dict:
    predecessor=verify_live_source(native) # Recheck immutable original CPP/API
    code=(native/HEADER).read_text()
    fixture=(native/FIXTURE).read_text()
    required=(
        'inline bool Compose(', 'if(!output.Begin())',
        'source.census.completeObservedSubset',
        'source.sourceOwnedAndMatched',
        'source.firstMatchedClassHeaderBitsCopied',
        'source.census.canonicalExecutionTypeKeys',
        'source.physicalClassPointerOwnershipProven',
        'source.registryRevision==std::numeric_limits<uint64_t>::max()',
        'source.candidateRegistrationRows>count',
        'source.activeShadowRegistrationRows!=count-source.candidateRegistrationRows',
        'source.census.rowsByCategory[category::Index(category::Category::MissingMatchedCallbackProof)]!=0',
        'if(n>count-categories)',
        'diagnostic::Writer writer(output.Sink(),maximumBytes)',
        'writer.Finish()', 'if(!output.Seal(done))',
        'output.Reject(reason)',
        'bytes_.fill(0);', 'receipt.ownedShapeJsonComplete=true;',
        'receipt.noncanonicalObservedSubsetOnly=true;',
        'static_assert(sizeof(Record)<=Record::kMaximumObjectBytes',
        'canonicalExecutionTypeKeys=false;',
        'coherentVmEpochProven=false;',
        'runtimeAcceptance=false;'
    )
    for token in required:
        if token not in code:
            raise RuntimeError("Bounded noncanonical census JSON source changed: "+token)
    if (code.count('R03P1ObservedTypeShapeCounts')!=1 or
        code.count('ObservedRegistrySubsetShapeCountsOnly')!=1 or
        code.count('kMaximumBytes=4096')!=1):
        raise RuntimeError("Separate noncanonical JSON schema or fixed byte cap changed")
    if (fixture.count('#include "AssemblyShadowObservedTypeShapeJson.h"')!=1 or
        fixture.count('#include "AssemblyShadowOwnedPhysicalNamesSource.inl"')!=1 or
        fixture.count('#include "AssemblyShadowObservedTypeShapeLiveSource.inl"')!=1):
        raise RuntimeError("Host fixture must compile actual VM source and JSON")
    got=tuple(re.findall(r'\bjsonCase\("([^"]+)"',fixture))
    if got!=GROUPS:
        raise RuntimeError("New JSON test group roster differs: "+repr(got))
    # Pure fixed pre-serialized category counters may not reenter VM or heap.
    stripped="\n".join(line.split("//",1)[0] for line in code.splitlines())
    for forbidden in ("il2cpp_class_for_each(", "g_MetadataLock", "ExecutionObservationLock ",
                      "MetadataCache::", "Class::", "fopen(", "std::fstream",
                      "std::vector<", "std::string ", "new "):
        if forbidden in stripped:
            raise RuntimeError("Pure type-shape JSON performed VM/heap/file operation: "+forbidden)
    return {
      "sourceKind":"R03P1ObservedTypeShapeCounts",
      "rowScope":"ObservedRegistrySubsetShapeCountsOnly",
      "fixedOwnedMaximumBytes":4096,
      "predecessorActualVmSourcesAndFingerprintVerified":True,
      "predecessor":predecessor,
      "previousNameGroups":list(NAME_GROUPS),
      "previousShapeGroups":list(SHAPE_GROUPS),
      "previousLiveCensusGroups":list(LIVE_GROUPS),
      "newJsonGroups":list(GROUPS),
      "purePointerFreeNoVmCalls":True,
      "nonCanonicalOnly":True,
      "fullExecutionV1":False,
      "canonicalExecutionTypeKeys":False,
      "physicalClassPointerOriginProven":False,
      "coherentVmEpochProven":False,
      "runtimeAcceptance":False,
    }

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Python optimization forbidden")
    native,out=a.native,a.output
    if (not native.is_absolute() or native!=native.resolve(strict=True) or
        not native.is_dir() or not out.is_absolute() or
        out!=out.resolve(strict=False) or out.exists() or
        not out.parent.is_dir() or native==out or
        native in out.parents or out in native.parents):
        raise RuntimeError("Fresh, canonical, disjoint source and evidence roots required")
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
        if not cxx:raise RuntimeError("C++17 host compiler unavailable")
        execute([cxx,"--version"],out,"compiler-version",env,commands,30)
        for label,options in (("strict",["-O2"]),
           ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                         "-fsanitize=address,undefined"])):
            exe=out/(label+".exe")
            execute([cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                     "-pthread","-iquote",native/"libil2cpp/vm",*options,
                     native/FIXTURE,"-o",exe],out,label+"-compile",env,commands,180)
            output=execute([exe],out,label+"-run",env,commands,180).decode("utf-8")
            roster=tuple(line for line in output.splitlines() if line.startswith("PASS "))
            if roster!=EXPECTED or not output.rstrip().endswith("P1 OBSERVED TYPE SHAPE JSON HOST PASS"):
                raise RuntimeError("Actual source/JSON case inventory or terminal verdict mismatch")
            profiles[label]={
              "result":"PassedWithinOwnedTypeShapeJsonSourceHostScope",
              "originalNameGroups":list(NAME_GROUPS),
              "originalShapeGroups":list(SHAPE_GROUPS),
              "liveIntegrationGroups":list(LIVE_GROUPS),
              "shapeJsonGroups":list(GROUPS)}
    except BaseException as exc:firstError=repr(exc)
    finally:
        lifetime=any(c["commandLifetimeUnverified"] for c in commands)
        try:
            if lifetime:raise RuntimeError("Unverified command lifetime; source postflight unsafe")
            after={p:source_identity(native/p) for p in SOURCES}
            preserved=before==after
        except Exception as exc:
            after={};preserved=False;postflightError=repr(exc)
        compiled=(preserved and all(any(c["name"]==label+"-compile" and
                  c["status"]=="Passed" and c["exit"]==0 for c in commands)
                  for label in ("strict","sanitized")))
        passed=(firstError is None and postflightError is None and preserved and
                not lifetime and compiled and len(commands)==5 and len(profiles)==2 and
                all(c["status"]=="Passed" for c in commands))
        result={"kind":"R03P1ObservedTypeShapeJsonActualVmSourceHost",
                "schemaVersion":1,
                "result":"PassedWithinOwnedTypeShapeJsonSourceHostScope" if passed else "FailedOrIncomplete",
                "sourceContract":contract,"sourceBefore":before,"sourceAfter":after,
                "sourcePreserved":preserved,"commands":commands,"profiles":profiles,
                "actualSourceCompiled":compiled,"firstError":firstError,
                "postflightError":postflightError,"commandLifetimeUnverified":lifetime,
                "noncanonicalShapeCountsOnly":True,"completeExecutionTypeKeys":False,
                "fullExecutionV1":False,"physicalClassPointerOriginProven":False,
                "coherentVmEpochProven":False,"officialUnitySdkCompiled":False,
                "UnityPlayerExecuted":False,"runtimeAcceptance":False}
        write(out/"RESULT.json",result)
        files=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                "sha256":sha(p.read_bytes())} for p in sorted(out.rglob("*"))
               if p.is_file() and not p.is_symlink()]
        write(out/"MANIFEST.json",{
           "files":files,
           "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
           "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":result["result"],"newCaseGroupsPerProfile":len(GROUPS),
                      "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)
if __name__=="__main__":main()
