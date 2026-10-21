#!/usr/bin/env python3
"""R03 P1 REAL INACTIVE VM-SOURCE→OWNED-JSON bridge, under explicit host mocks.

Compiles both genuine AssemblyShadow source .inl methods and pure owned JSON
against controlled metadata/registry/physical iterator and locks. No Unity
Player/linked product, physical pointer lifetime or all-VM epoch proof.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
from run_owned_physical_names import execute, sha, source_identity, write, verify as verify_names, GROUPS as NAME_GROUPS

SRC="libil2cpp/vm/AssemblyShadowOwnedPhysicalLiveJsonSource.inl"
CPP="libil2cpp/vm/AssemblyShadow.cpp"
API="libil2cpp/vm/AssemblyShadow.h"
JSON="libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesJson.h"
ORIGINAL_NAMES="libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesSource.inl"
FIXTURE="tools/r03/diagnostic_stream/owned_physical_live_json_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_owned_physical_live_json.py"
SOURCES=(SRC,CPP,API,JSON,ORIGINAL_NAMES,
         "libil2cpp/vm/AssemblyShadowOwnedPhysicalNames.h",
         "libil2cpp/vm/AssemblyShadowPhysicalMembership.h",
         "libil2cpp/vm/AssemblyShadowExecutionRegistrationWitness.h",
         "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
         "tools/r03/diagnostic_stream/run_owned_physical_names.py",
         FIXTURE,RUNNER)
LIVE_GROUPS=(
 "empty-live-physical-json",
 "one-live-candidate-json",
 "two-live-mixed-witnesses",
 "reversed-live-iterator-binding",
 "owned-live-source-mutation-independent",
 "unmatched-live-physical-refused",
 "invalid-live-matched-name-refused",
 "invalid-live-utf8-refused-after-copy",
 "source-revision-after-enumeration-refused",
 "limited-json-output-atomic-refusal",
 "invalid-oversized-limit-no-iterator",
 "reused-json-output-no-iterator",
)
EXPECTED=tuple("PASS "+name for name in NAME_GROUPS)+tuple("PASS LIVE "+name for name in LIVE_GROUPS)

def verify_source(native:Path)->dict:
    # Execute the existing source gate including exact original C++/API byte
    # reconstruction, NOT a new relaxed baseline or rebaselined source SHA.
    verify_names(native)
    src=(native/SRC).read_text()
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    fixture=(native/FIXTURE).read_text()
    method="TryCaptureOwnedObservedPhysicalJson("
    if (cpp.count('#include "AssemblyShadowOwnedPhysicalNamesJson.h"')!=1 or
        cpp.count('#include "AssemblyShadowOwnedPhysicalLiveJsonSource.inl"')!=1 or
        api.count("static bool "+method)!=1 or
        src.count("bool AssemblyShadow::"+method)!=1 or
        cpp.count(method)!=0):
        raise RuntimeError("Actual inactive live owned JSON declaration/TU source not exact")
    required=(
      "jsonOutput.Fresh()", "json::Record::kMaximumBytes",
      "TryCaptureOwnedObservedPhysicalNames(membershipScratch,names,namesReceipt)",
      "json::Compose(names,namesReceipt,jsonOutput,jsonReceipt,",
      "names.Revoke();", "json::Error::IncompleteSource",
      "return json::Fail(jsonOutput,jsonReceipt,json::Error::ReusedOutput);",
      "return json::Fail(jsonOutput,jsonReceipt,json::Error::InvalidOutputLimit);")
    if any(t not in src for t in required):
        raise RuntimeError("Source path lost existing VM bridge/typed rejection")
    # VM class/lock/metadata reads occur ONLY in the original source; this new
    # .inl may call that exact original source before formatting and must not
    # acquire unrelated locks or reinterpret a class pointer.
    effective="\n".join(line.split("//",1)[0] for line in src.splitlines())
    for bad in ("il2cpp_class_for_each(", "g_MetadataLock", "ExecutionObservationLock",
                "klass->", "Il2CppClass*", "std::vector<", "std::string ", "fopen("):
        if bad in effective:raise RuntimeError("Bridge introduced additional VM/heap access: "+bad)
    if (src.index("TryCaptureOwnedObservedPhysicalNames(")>=src.index("json::Compose(") or
        src.rfind("names.Revoke();")<src.index("json::Compose(")):
        raise RuntimeError("Source capture→pure format→temporary-source revocation order changed")
    if (fixture.count('#include "AssemblyShadowOwnedPhysicalNamesSource.inl"')!=1 or
        fixture.count('#include "AssemblyShadowOwnedPhysicalLiveJsonSource.inl"')!=1 or
        fixture.count('#include "AssemblyShadowOwnedPhysicalNamesJson.h"')!=1):
        raise RuntimeError("Host fixture does not compile actual source methods")
    for name in LIVE_GROUPS:
        if fixture.count('liveTest("'+name+'"')!=1 and fixture.count('PASS LIVE '+name)!=1:
            raise RuntimeError("Missing/duplicate live VM source case "+name)
    return {"originalNativeCppAndApiFingerprintPreserved":True,
        "productionSourceMethodsCompiled":2,
        "bridgeDoesNotReadAdditionalVmPointers":True,
        "sourcePassBeforePureOwnedSerialization":True,
        "caseGroups":list(LIVE_GROUPS),
        "canonicalExecutionTypeKeys":False,"runtimeAcceptance":False}

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    args=p.parse_args()
    if not __debug__:raise RuntimeError("Python optimization prohibited")
    native,out=args.native,args.output
    if (not native.is_absolute() or not native.is_dir() or
        native!=native.resolve(strict=True) or
        not out.is_absolute() or out!=out.resolve(strict=False) or
        out.exists() or not out.parent.is_dir() or native==out or
        native in out.parents or out in native.parents):
        raise RuntimeError("Fresh canonical disjoint source and evidence roots required")
    before={f:source_identity(native/f) for f in SOURCES}
    contract=verify_source(native)
    out.mkdir(mode=0o700)
    compiler=shutil.which(os.environ.get("CXX","clang++"))
    env=dict(os.environ,ASAN_OPTIONS=("detect_leaks=0:halt_on_error=1" if sys.platform=="darwin"
        else "detect_leaks=1:halt_on_error=1"),UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    cmds=[];profiles={};failure=None;postflightError=None
    try:
        if not compiler:raise RuntimeError("C++17 compiler missing")
        execute([compiler,"--version"],out,"compiler-version",env,cmds,30)
        for profile,flags in (("strict",["-O2"]),
              ("sanitized",["-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            execute([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                "-pthread","-iquote",native/"libil2cpp/vm",*flags,
                native/FIXTURE,"-o",exe],out,profile+"-compile",env,cmds,180)
            output=execute([exe],out,profile+"-run",env,cmds,180).decode("utf-8")
            got=tuple(line for line in output.splitlines() if line.startswith("PASS "))
            if got!=EXPECTED or not output.rstrip().endswith("P1 LIVE OWNED PHYSICAL JSON HOST PASS"):
                raise RuntimeError("Actual source method group count/order or completion mismatch")
            profiles[profile]={"result":"PassedWithinInactiveLiveOwnedPhysicalJsonVmMockScope",
                "originalNameGroups":list(NAME_GROUPS),"liveIntegrationGroups":list(LIVE_GROUPS)}
    except BaseException as exc:failure=repr(exc)
    finally:
        lifetime=any(c["commandLifetimeUnverified"] for c in cmds)
        try:
            if lifetime:raise RuntimeError("Unknown command lifetime; unsafe source postflight")
            after={f:source_identity(native/f) for f in SOURCES};preserved=before==after
        except Exception as exc:
            after={};preserved=False;postflightError=repr(exc)
        compiled=(preserved and all(any(c["name"]==profile+"-compile" and c["status"]=="Passed"
            and c["exit"]==0 for c in cmds) for profile in ("strict","sanitized")))
        passed=(failure is None and postflightError is None and preserved and
            not lifetime and compiled and len(cmds)==5 and len(profiles)==2 and
            all(c["status"]=="Passed" for c in cmds))
        result={"kind":"R03P1InactiveLiveOwnedPhysicalJsonSourceHost","schemaVersion":1,
            "result":"PassedWithinInactiveLiveOwnedPhysicalJsonVmMockScope" if passed else "FailedOrIncomplete",
            "sourceContract":contract,"sourceBefore":before,"sourceAfter":after,
            "sourcePreserved":preserved,"commands":cmds,"profiles":profiles,
            "firstError":failure,"postflightError":postflightError,
            "commandLifetimeUnverified":lifetime,"actualSourceCompiled":compiled,
            "sourceToJsonIntegrationTestedWithMocks":True,
            "physicalClassPointerOriginProven":False,"metadataPointerLifetimeProven":False,
            "canonicalExecutionTypeKeys":False,"coherentVmEpochProven":False,
            "officialUnitySdkCompiled":False,"UnityPlayerExecuted":False,"runtimeAcceptance":False}
        write(out/"RESULT.json",result)
        files=[{"path":str(q.relative_to(out)),"bytes":q.stat().st_size,"sha256":sha(q.read_bytes())}
            for q in sorted(out.rglob("*")) if q.is_file() and not q.is_symlink()]
        write(out/"MANIFEST.json",{"files":files,
             "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
             "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":result["result"],"liveGroupsPerProfile":len(LIVE_GROUPS),
                      "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)
if __name__=="__main__":main()
