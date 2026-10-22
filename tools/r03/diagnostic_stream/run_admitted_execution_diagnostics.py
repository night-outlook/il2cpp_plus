#!/usr/bin/env python3
"""R03 P1: actual INACTIVE metadata-admitted execution diagnostic source.

Compile the production admission .inl + actual MetadataLifetime gate in host
fixtures under strict + ASan/UBSan. No Player, original getter integration,
canonical TypeKeys, class pointer allocator provenance or VM epoch proof.
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
from run_observed_type_shape_json_live import verify_source as verify_previous
CPP="libil2cpp/vm/AssemblyShadow.cpp"
API="libil2cpp/vm/AssemblyShadow.h"
BRIDGE="libil2cpp/vm/AssemblyShadowAdmittedExecutionDiagnosticsSource.inl"
GATE="libil2cpp/vm/AssemblyShadowMetadataLifetime.h"
FIXTURE="tools/r03/diagnostic_stream/admitted_execution_diagnostics_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_admitted_execution_diagnostics.py"
SOURCES=(CPP,API,BRIDGE,GATE,FIXTURE,RUNNER,
    "tools/r03/diagnostic_stream/run_owned_physical_names.py",
    "tools/r03/diagnostic_stream/run_physical_membership.py",
    "tools/r03/diagnostic_stream/run_opaque_execution_inventory.py",
    "tools/r03/diagnostic_stream/run_observed_type_shape_json_live.py",
    "libil2cpp/vm/AssemblyShadowObservedTypeShapeJsonLiveSource.inl",
    "libil2cpp/vm/AssemblyShadowObservedTypeShapeJson.h")
GROUPS=(
 "closed-before-first-init",
 "original-reader-opens-once",
 "actual-old-getter-inside-reader",
 "separate-output-owned-after-admission",
 "original-error-code-propagated",
 "original-exception-unwinds-reader",
 "shutdown-blocked-until-reader-exits",
 "shutdown-refuses-without-calling-getter",
 "reopen-epoch-two-refuses-unproven-source",
)
EXPECTED=tuple("PASS ADMITTED EXECUTION "+x for x in GROUPS)

def verify_source(native:Path)->dict:
    preceding=verify_previous(native)  # original physical/getter fingerprint
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    gate=(native/GATE).read_text()
    bridge=(native/BRIDGE).read_text()
    fixture=(native/FIXTURE).read_text()
    header_marker='#include "AssemblyShadowAdmittedExecutionDiagnosticsSource.inl"'
    signature="TryGetExecutionDiagnosticsWithMetadataAdmission(std::string& json)"
    if cpp.count(header_marker)!=1 or api.count("static AssemblyShadowError "+signature)!=1:
        raise RuntimeError("Admitted execution method not present exactly once in CPP/API")
    if bridge.count("AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(")!=1:
        raise RuntimeError("Actual admission production body differs")
    if cpp.count("AssemblyShadow::"+signature):
        raise RuntimeError("Wrapper must be an exactly included original .inl method")
    original=cpp.index("AssemblyShadowError AssemblyShadow::GetExecutionDiagnosticsJson(std::string& json)")
    wrapper=cpp.index(header_marker)
    next_method=cpp.index("const Il2CppAssembly* AssemblyShadow::ResolveAssembly(")
    if not original < wrapper < next_method:
        raise RuntimeError("Wrapper moved into the legacy getter or outside its owning VM region")
    required=(
      "json.clear();",
      "assembly_shadow_metadata_lifetime::Gate::Reader reader(",
      "assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate",
      "if (!reader)",
      "return AssemblyShadowError::InvalidState;",
      "return GetExecutionDiagnosticsJson(json);")
    if any(p not in bridge for p in required) or not (
        bridge.index("json.clear();") <
        bridge.index("Reader reader(") <
        bridge.index("if (!reader)") <
        bridge.index("GetExecutionDiagnosticsJson(json);")):
        raise RuntimeError("Fail-closed admission/old getter call order changed")
    if bridge.count("GetExecutionDiagnosticsJson(json)")!=1:
        raise RuntimeError("Original getter was invoked zero/multiple times")
    no_cpp="\n".join(line.split("//",1)[0] for line in bridge.splitlines())
    for token in ("OpenAfterInit(","CloseAndDrain(","CurrentEpoch(",
       "s_executionClasses","klass->","g_MetadataLock",
       "std::vector<","std::string ","std::unordered_map<",
       "il2cpp_class_for_each(","malloc(","new ","fopen("):
        if token in no_cpp:
            raise RuntimeError("Admission wrapper touched VM metadata or mutated gate: "+token)
    if ("class Reader final" not in gate or
        "sourceEpoch=detail::kInitialEpoch" not in gate or
        "void CloseAndDrain()" not in gate):
        raise RuntimeError("Original metadata lifetime gate epoch/drain semantics changed")
    if (fixture.count('#include "AssemblyShadowAdmittedExecutionDiagnosticsSource.inl"')!=1
        or fixture.count('#include "AssemblyShadowMetadataLifetime.h"')!=1):
        raise RuntimeError("Host fixture must compile actual production source and gate")
    got=tuple(re.findall(r'\btest\("([^"]+)"',fixture))
    if got!=GROUPS:
        raise RuntimeError("Bounded metadata admission case identities changed "+repr(got))
    return {
       "sourceKind":"R03P1InactiveExecutionMetadataAdmissionActualSource",
       "precedingSourceAndLegacyFingerprintVerified":preceding,
       "actualProductionWrapperCompiled":True,
       "actualGateCompiled":True,
       "legacyGetterBodyUnchanged":True,
       "wrapperOnlyCallsExistingGetter":True,
       "failClosedOnReaderRejection":True,
       "originalInitialEpochOnly":True,
       "noVmFieldReadsInWrapper":True,
       "cases":list(GROUPS),
       "canonicalExecutionTypeKeys":False,
       "physicalPointerOriginProven":False,
       "coherentVmEpochProven":False,
       "fullExecutionV1SourceSafe":False,
       "originalGetterEnabledByWrapper":False,
       "UnityPlayerExecuted":False,
       "runtimeAcceptance":False}

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Optimized Python source evaluation forbidden")
    native,out=a.native,a.output
    if (not native.is_absolute() or native!=native.resolve(strict=True)
        or not native.is_dir() or not out.is_absolute()
        or out!=out.resolve(strict=False) or out.exists()
        or not out.parent.is_dir() or native==out
        or out in native.parents or native in out.parents):
        raise RuntimeError("Fresh canonical disjoint source/receipt roots required")
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
        if not cxx:raise RuntimeError("No C++17 compiler")
        execute([cxx,"--version"],out,"compiler-version",env,commands,30)
        for profile,flags in (("strict",["-O2"]),("sanitized",[
                "-O1","-g","-fno-omit-frame-pointer",
                "-fsanitize=address,undefined"])):
            target=out/(profile+".exe")
            execute([cxx,"-std=c++17","-Wall","-Wextra","-Werror",
                     "-pedantic","-pthread","-iquote",native/"libil2cpp/vm",
                     *flags,native/FIXTURE,"-o",target],
                    out,profile+"-compile",env,commands,180)
            raw=execute([target],out,profile+"-run",env,commands,180).decode("utf-8")
            group=tuple(line for line in raw.splitlines() if line.startswith("PASS "))
            if group!=EXPECTED or not raw.rstrip().endswith(
                    "P1 INACTIVE EXECUTION METADATA ADMISSION HOST PASS"):
                raise RuntimeError("Actual source-host metadata admission group mismatch")
            profiles[profile]={
                "result":"PassedWithinInactiveMetadataAdmittedExecutionHostScope",
                "caseGroups":list(GROUPS)}
    except BaseException as e:firstError=repr(e)
    finally:
        lifetime=any(cmd["commandLifetimeUnverified"] for cmd in commands)
        try:
            if lifetime:raise RuntimeError("Postflight unsafe: unknown compiler/child lifetime")
            after={p:source_identity(native/p) for p in SOURCES}
            preserved=before==after
        except Exception as e:
            after={};preserved=False;postflightError=repr(e)
        compiled=(preserved and all(any(c["name"]==profile+"-compile" and
                c["status"]=="Passed" and c["exit"]==0 for c in commands)
                for profile in ("strict","sanitized")))
        passed=(firstError is None and postflightError is None and
                not lifetime and compiled and len(commands)==5 and len(profiles)==2
                and all(c["status"]=="Passed" for c in commands))
        record={
          "kind":"R03P1InactiveExecutionMetadataAdmissionActualSourceHost",
          "schemaVersion":1,
          "result":"PassedWithinInactiveMetadataAdmittedExecutionHostScope" if passed
                   else "FailedOrIncomplete",
          "sourceContract":contract,
          "sourceBefore":before,"sourceAfter":after,"sourcePreserved":preserved,
          "commands":commands,"profiles":profiles,"actualSourceCompiled":compiled,
          "commandLifetimeUnverified":lifetime,"firstError":firstError,
          "postflightError":postflightError,
          "originalGetterNotActivated":True,
          "canonicalExecutionTypeKeys":False,
          "physicalPointerOriginProven":False,
          "coherentVmEpochProven":False,
          "fullExecutionV1SourceSafe":False,
          "officialUnitySdkCompiled":False,
          "UnityPlayerExecuted":False,"runtimeAcceptance":False}
        write(out/"RESULT.json",record)
        entries=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                  "sha256":sha(p.read_bytes())} for p in sorted(out.rglob("*"))
                 if p.is_file() and not p.is_symlink()]
        write(out/"MANIFEST.json",{
          "files":entries,
          "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
          "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":record["result"],"profileCount":len(profiles),
                      "groupsPerProfile":len(GROUPS)}))
    if not passed:raise SystemExit(1)
if __name__=="__main__":main()
