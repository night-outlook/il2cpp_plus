#!/usr/bin/env python3
"""R03 P1 source-bound, INACTIVE observed physical type-shape census.

Compile the REAL VM owned-name .inl and shape capture/census headers under
explicit VM metadata/iterator/lock mocks. Reject unsupported constructed,
nested, generic and non-definition rows as non-candidates. NOT canonical
execution TypeKeys, memory provenance, VM epoch or Unity/Player acceptance.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
from run_owned_physical_names import execute,sha,source_identity,write,verify as verify_names,GROUPS as NAME_GROUPS

METHOD="libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesSource.inl"
NAMES="libil2cpp/vm/AssemblyShadowOwnedPhysicalNames.h"
EVIDENCE="libil2cpp/vm/AssemblyShadowObservedTypeShape.h"
CENSUS="libil2cpp/vm/AssemblyShadowObservedTypeShapeCensus.h"
SDK_TYPES="libil2cpp/il2cpp-class-internals.h"
WITNESS="libil2cpp/vm/AssemblyShadowExecutionRegistrationWitness.h"
PHYSICAL="libil2cpp/vm/AssemblyShadowPhysicalMembership.h"
FIXTURE="tools/r03/diagnostic_stream/owned_type_shape_census_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_observed_type_shape_census.py"
SOURCES=(METHOD,NAMES,EVIDENCE,CENSUS,SDK_TYPES,WITNESS,PHYSICAL,FIXTURE,RUNNER,
         "tools/r03/diagnostic_stream/run_owned_physical_names.py",
         "tools/r03/diagnostic_stream/owned_physical_names_fixture.cpp",
         "libil2cpp/vm/AssemblyShadow.cpp","libil2cpp/vm/AssemblyShadow.h")
SHAPE_GROUPS=(
 "zero-observed-shape-subset",
 "plain-reference-definition-candidate",
 "plain-value-definition-candidate",
 "missing-metadata-definition",
 "ranked-array-is-not-definition-candidate",
 "constructed-generic-is-not-definition-candidate",
 "generic-definition-is-not-plain-candidate",
 "nested-definition-is-not-plain-candidate",
 "unsupported-raw-type-code",
 "conflicting-flags-reject-conservatively",
 "mixed-candidate-active-witness-shapes",
 "manual-unobserved-raw-shape-refused",
 "missing-shape-source-receipt-refused",
 "reused-census-revokes-prior-success",
 "missing-physical-source-cannot-certify-shape",
)
EXPECTED=("PASS "+n for n in NAME_GROUPS)+("PASS SHAPE "+n for n in SHAPE_GROUPS)

def source_contract(native:Path)->dict:
    # Reuse previous strict historical Native VM CPP/API reconstruction; its
    # digest anchors and preexisting host fixture name roster must not change.
    verify_names(native)
    vm=(native/METHOD).read_text()
    header=(native/NAMES).read_text()
    shape=(native/EVIDENCE).read_text()
    census=(native/CENSUS).read_text()
    mock=(native/FIXTURE).read_text()
    sdk=(native/SDK_TYPES).read_text()
    required_vm=(
        "static_assert(static_cast<uint8_t>(IL2CPP_TYPE_CLASS)==shape::kClassTypeCode",
        "rawShape.byvalTypeCode=static_cast<uint8_t>(klass->byval_arg.type);",
        "rawShape.arrayRank=klass->rank;",
        "rawShape.hasTypeMetadataHandle=klass->typeMetadataHandle!=nullptr;",
        "rawShape.hasDeclaringType=klass->declaringType!=nullptr;",
        "rawShape.hasConstructedGenericClass=klass->generic_class!=nullptr;",
        "rawShape.isGenericDefinition=klass->is_generic!=0;",
        "rawShape.observedFromMatchedPhysicalCallback=true;",
        "klass->namespaze,klass->name,rawShape);",
        "names.AllObservedShapesFromMatchedCallbacks()",
        "receipt.allObservedTypeShapeRowsFromMatchedCallbacks=true;",
        "os::FastAutoLock metadataLock(&g_MetadataLock);",
        "ObserveEnumeratedOwned(klass)",
    )
    for literal in required_vm:
        if vm.count(literal)!=1:
            raise RuntimeError("Typed VM shape source mismatch: "+literal)
    if vm.index("ObserveEnumeratedOwned(klass)")>=vm.index("rawShape.byvalTypeCode="):
        raise RuntimeError("Metadata shape read escaped exact physical FirstMatch")
    for name in ("Il2CppMetadataTypeHandle typeMetadataHandle",
                 "Il2CppClass* declaringType",
                 "Il2CppGenericClass *generic_class",
                 "uint8_t rank","uint8_t is_generic"):
        if name not in sdk:
            if name=="uint8_t is_generic":
                if "uint8_t is_generic : 1;" in sdk:continue
            raise RuntimeError("Pinned ABI class shape field missing: "+name)
    for token in ("assembly_shadow_observed_type_shape::RawEvidence observedShape{};",
                  "AllObservedShapesFromMatchedCallbacks()", "row.observedShape=observedShape;"):
        if token not in header:raise RuntimeError("Source captured shape storage incomplete: "+token)
    for token in ("enum class Category:uint8_t", "PlainReferenceDefinitionCandidate",
                  "PlainValueDefinitionCandidate", "MissingMatchedCallbackProof",
                  "bool canonicalExecutionTypeKeys=false;"):
        if token not in shape:raise RuntimeError("Shape census status/scope changed: "+token)
    for token in ("inline bool Build(", "capture.Get(i,row)",
                  "sourceReceipt.allObservedTypeShapeRowsFromMatchedCallbacks",
                  "if(seen[row.registrySlot])", "shape::Classify(row.observedShape)",
                  "computed.completeObservedSubset=true;",
                  "output.firstMatchedClassHeaderBitsCopied=true;"):
        if token not in census:raise RuntimeError("Actual pure shape census changed: "+token)
    if mock.count('#include "AssemblyShadowOwnedPhysicalNamesSource.inl"')!=1 or \
       mock.count('#include "AssemblyShadowObservedTypeShapeCensus.h"')!=1:
        raise RuntimeError("Fixture must compile both actual source and census")
    for g in SHAPE_GROUPS:
        if mock.count('"'+g+'"')!=1 and mock.count('"PASS SHAPE '+g+'')!=1:
            raise RuntimeError("Missing/duplicated shape host test "+g)
    return {
      "typedFieldsCopiedOnlyFromMatchedCallback":True,
      "exactPinnedIl2CppClassAbiSourceInspected":True,
      "originalCppApiFingerprintVerified":True,
      "noMetadataPointersRetained":True,
      "candidateRowsAreOnlySimpleShapeHints":True,
      "canonicalExecutionTypeKeys":False,
      "physicalPointerOriginProven":False,
      "globalVmEpochProven":False,
      "playerExecution":False,"runtimeAcceptance":False,
      "originalNameGroups":list(NAME_GROUPS),"shapeGroups":list(SHAPE_GROUPS)}

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Python -O forbidden")
    native,out=a.native,a.output
    if (not native.is_absolute() or not native.is_dir() or native!=native.resolve(strict=True)
        or not out.is_absolute() or out!=out.resolve(strict=False) or out.exists()
        or not out.parent.is_dir() or native==out or
        native in out.parents or out in native.parents):
        raise RuntimeError("Fresh canonical disjoint source and evidence roots required")
    before={path:source_identity(native/path) for path in SOURCES}
    contract=source_contract(native)
    out.mkdir(mode=0o700)
    compiler=shutil.which(os.environ.get("CXX","clang++"))
    env=dict(os.environ,
             ASAN_OPTIONS=("detect_leaks=0:halt_on_error=1" if sys.platform=="darwin"
                           else "detect_leaks=1:halt_on_error=1"),
             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    commands=[];profiles={};failure=None;postflightError=None
    try:
        if not compiler:raise RuntimeError("C++17 compiler missing")
        execute([compiler,"--version"],out,"compiler-version",env,commands,30)
        for profile,flags in (("strict",["-O2"]),
             ("sanitized",["-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            execute([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                     "-pthread","-iquote",native/"libil2cpp/vm",*flags,
                     native/FIXTURE,"-o",exe],out,profile+"-compile",env,commands,180)
            outtext=execute([exe],out,profile+"-run",env,commands,180).decode("utf-8")
            found=tuple(line for line in outtext.splitlines() if line.startswith("PASS "))
            if found!=EXPECTED or not outtext.rstrip().endswith("P1 OWNED TYPE SHAPE SOURCE HOST PASS"):
                raise RuntimeError("Source-bound original/type-shape case inventory changed")
            profiles[profile]={"result":"PassedWithinObservedTypeShapeSourceHostScope",
                               "originalNameGroups":list(NAME_GROUPS),
                               "shapeGroups":list(SHAPE_GROUPS)}
    except BaseException as e:failure=repr(e)
    finally:
        lifetime=any(c["commandLifetimeUnverified"] for c in commands)
        try:
            if lifetime:raise RuntimeError("Unverified descendant lifetime; source postflight skipped")
            after={p:source_identity(native/p) for p in SOURCES}
            preserved=before==after
        except Exception as e:
            after={};preserved=False;postflightError=repr(e)
        compiled=(preserved and all(any(c["name"]==p+"-compile" and
                  c["status"]=="Passed" and c["exit"]==0 for c in commands)
                  for p in ("strict","sanitized")))
        passed=(failure is None and postflightError is None and preserved and
                not lifetime and compiled and len(commands)==5 and len(profiles)==2 and
                all(c["status"]=="Passed" for c in commands))
        result={"kind":"R03P1ObservedTypeShapeActualVmSourceHost","schemaVersion":1,
                "result":"PassedWithinObservedTypeShapeSourceHostScope" if passed else "FailedOrIncomplete",
                "sourceContract":contract,"sourceBefore":before,"sourceAfter":after,
                "sourcePreserved":preserved,"commands":commands,"profiles":profiles,
                "firstError":failure,"postflightError":postflightError,
                "commandLifetimeUnverified":lifetime,"actualSourceCompiled":compiled,
                "typeShapeCategoriesOnly":True,"completeExecutionTypeKeys":False,
                "physicalClassPointerOriginProven":False,"coherentVmEpochProven":False,
                "officialUnitySdkCompiled":False,"UnityPlayerExecuted":False,
                "runtimeAcceptance":False}
        write(out/"RESULT.json",result)
        files=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,"sha256":sha(p.read_bytes())}
               for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        write(out/"MANIFEST.json",{"files":files,
             "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
             "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":result["result"],"shapeGroupsPerProfile":len(SHAPE_GROUPS),
                      "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)
if __name__=="__main__":main()
