#!/usr/bin/env python3
"""R03 P1 actual inactive opaque execution inventory source/host gate.

Two real C++17 host profiles compile exact production method bytes with
explicit VM mocks. No Unity SDK, Player, pointer provenance or VM epoch proof.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

H="libil2cpp/vm/AssemblyShadowOpaqueExecutionInventory.h"
SRC="libil2cpp/vm/AssemblyShadowOpaqueExecutionInventorySource.inl"
CPP="libil2cpp/vm/AssemblyShadow.cpp"
API="libil2cpp/vm/AssemblyShadow.h"
MOCK="tools/r03/diagnostic_stream/opaque_inventory_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_opaque_execution_inventory.py"
SOURCES=(H,SRC,CPP,API,MOCK,RUNNER)
ORIGINAL_CPP_BLOB="53aecc2d113b20b6ff825d0aa4ca0f4372a7cce2"
ORIGINAL_API_BLOB="32956c2c4084951131b497c1201712a0d4fe11b3"
GROUPS=(
 "empty-inventory","one-nonempty-opaque","three-scattered-opaque",
 "full-capacity-opaque","closed-metadata-ticket","declared-count-too-large",
 "over-capacity-actual-entries","missing-identity-for-count",
 "unexpected-identity-with-count-zero","duplicate-pointer",
 "class-added-before-second-sample","same-count-replacement",
 "same-count-relocation","state-flip","active-identity-flip",
 "one-shot-failure","second-sample-count-corruption",
 "successful-scratch-reuse-refused",
)
def sha(value:bytes)->str:return hashlib.sha256(value).hexdigest()
def blob(value:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(value)).encode()+b"\0"+value).hexdigest()
def binding(path:Path)->dict:
    raw=path.read_bytes()
    return {"bytes":len(raw),"sha256":sha(raw),"gitBlob":blob(raw)}
def write(path:Path,value:object)->None:
    path.write_text(json.dumps(value,indent=2,sort_keys=True)+"\n",encoding="utf-8")
def verify(native:Path)->str:
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    source=(native/SRC).read_text()
    fixture=(native/MOCK).read_text()
    include='#include "AssemblyShadowOpaqueExecutionInventorySource.inl"'
    header='#include "AssemblyShadowOpaqueExecutionInventory.h"'
    if cpp.count(include)!=1 or cpp.count(header)!=1 or \
       api.count("static bool TryCaptureOpaqueExecutionInventory(")!=1 or \
       source.count("bool AssemblyShadow::TryCaptureOpaqueExecutionInventory(")!=1:
        raise RuntimeError("Actual native method/header not included exactly once")
    if cpp.count("TryCaptureOpaqueExecutionInventory("):
        raise RuntimeError("Inactive method is called from normal AssemblyShadow.cpp")
    for token in ("assembly_shadow_metadata_lifetime::Gate::Reader admission(",
                  "ExecutionObservationLock lock;","scratch.Before().Unique()",
                  "scratch.After().Unique()","scratch.Before().SameAs(scratch.After())",
                  "scratch.Scrub();","receipt.opaqueInventoryObserved=true;"):
        if source.count(token)!=1:
            raise RuntimeError("Native bounded pointer safety contract changed: "+token)
    if fixture.count(include)!=1 or fixture.count(header)!=1 or \
       any(('"'+x+'"') not in fixture and ('PASS '+x+'\\n') not in fixture for x in GROUPS):
        raise RuntimeError("Fixture does not compile exact native code/case groups")
    oldcpp=cpp.replace(header+"\n","").replace(
        "// P1 inactive opaque execution observation inventory. No class dereference.\n"+
        include+"\n","")
    oldapi=api.replace(
        "namespace assembly_shadow_opaque_execution { class Scratch; struct Receipt; }\n","").replace(
        "    // P1 INACTIVE: observable opaque class identities without metadata dereference.\n"+
        "    // Never returns pointers, type keys, JSON, or a VM-coherence claim.\n"+
        "    static bool TryCaptureOpaqueExecutionInventory(\n"+
        "        assembly_shadow_opaque_execution::Scratch& scratch,\n"+
        "        assembly_shadow_opaque_execution::Receipt& receipt);\n","")
    if blob(oldcpp.encode())!=ORIGINAL_CPP_BLOB or blob(oldapi.encode())!=ORIGINAL_API_BLOB:
        raise RuntimeError("Original VM execution getter or guards have changed")
    return source

def execute(argv:list,output:Path,name:str,env:dict,rows:list,timeout:int=180)->bytes:
    receipt={"name":name,"command":list(map(str,argv)),"startedUnix":time.time(),
             "status":"Failed","exit":None,"commandLifetimeUnverified":False,
             "outputCompleteness":"Unavailable"}
    stdout=stderr=None
    try:
        proc=subprocess.run(list(map(str,argv)),capture_output=True,env=env,timeout=timeout)
        stdout,stderr=proc.stdout,proc.stderr
        receipt.update(exit=proc.returncode,outputCompleteness="CompleteDirectChildStreams")
        if proc.returncode==0:receipt["status"]="Passed"
    except subprocess.TimeoutExpired as error:
        stdout,stderr=error.stdout,error.stderr
        receipt.update(exception=repr(error),commandLifetimeUnverified=True,
                       timedOut=True,outputCompleteness="CapturedPrefix")
    except KeyboardInterrupt:
        receipt.update(exception="KeyboardInterrupt",commandLifetimeUnverified=True,interrupted=True)
    except OSError as error:
        receipt["exception"]=repr(error)
    except Exception as error:
        receipt.update(exception=repr(error),commandLifetimeUnverified=True)
    finally:
        for kind,raw in (("stdout",stdout),("stderr",stderr)):
            payload=raw if raw is not None else b""
            (output/(name+"."+kind+".bin")).write_bytes(payload)
            receipt[kind+"Available"]=raw is not None
            receipt[kind+"CapturedBytes"]=len(payload)
            receipt[kind+"Sha256"]=sha(payload)
        receipt["finishedUnix"]=time.time()
        rows.append(receipt)
        write(output/(name+".json"),receipt)
    if receipt["status"]!="Passed":
        raise RuntimeError(name+" failed; inspect retained raw command receipts")
    return stdout

def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    if not __debug__:raise RuntimeError("Python -O forbidden")
    native,output=args.native,args.output
    if not native.is_absolute() or not native.is_dir() or native!=native.resolve(strict=True) or \
       not output.is_absolute() or output!=output.resolve(strict=False) or \
       output.exists() or not output.parent.is_dir() or \
       native==output or native in output.parents or output in native.parents:
        raise RuntimeError("Fresh disjoint canonical output and native source required")
    before={name:binding(native/name) for name in SOURCES}
    method=verify(native)
    output.mkdir(mode=0o700)
    compiler=shutil.which(os.environ.get("CXX","clang++"))
    env=dict(os.environ,UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
             ASAN_OPTIONS=("detect_leaks=0:halt_on_error=1" if sys.platform=="darwin"
                           else "detect_leaks=1:halt_on_error=1"))
    rows=[];profiles={};firstError=None;postflightError=None
    try:
        if not compiler:raise RuntimeError("Host C++ compiler unavailable")
        execute([compiler,"--version"],output,"compiler-version",env,rows,30)
        for label,extra in (("strict",["-O2"]),("sanitized",
                             ["-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            executable=output/(label+".exe")
            execute([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic","-pthread",
                     "-iquote",str(native/"libil2cpp/vm"),*extra,str(native/MOCK),
                     "-o",str(executable)],output,label+"-compile",env,rows,180)
            raw=execute([executable],output,label+"-run",env,rows,180).decode("utf-8")
            groups=tuple(x[5:] for x in raw.splitlines() if x.startswith("PASS "))
            if groups!=GROUPS or not raw.rstrip().endswith(
                "P1 OPAQUE EXECUTION INVENTORY SOURCE HOST PASS"):
                raise RuntimeError("Wrong/missing/duplicate native test groups "+label)
            profiles[label]={"result":"PassedWithinInactiveOpaqueHostScope",
                             "caseGroups":list(groups)}
    except BaseException as error:
        firstError=repr(error)
    finally:
        lifetime=any(row["commandLifetimeUnverified"] for row in rows)
        try:
            if lifetime:raise RuntimeError("Source postflight unsafe: uncertain child lifetime")
            after={name:binding(native/name) for name in SOURCES}
            preserved=before==after
        except Exception as error:
            after={};preserved=False;postflightError=repr(error)
        compiled=(preserved and all(any(row["name"]==label+"-compile" and
            row["status"]=="Passed" and row["exit"]==0 for row in rows)
            for label in ("strict","sanitized")))
        passed=(firstError is None and preserved and not lifetime and compiled and
                len(rows)==5 and len(profiles)==2 and
                all(row["status"]=="Passed" for row in rows))
        record={"kind":"R03P1InactiveOpaqueExecutionInventoryHost","schemaVersion":1,
                "result":"PassedWithinInactiveOpaqueHostScope" if passed else "FailedOrIncomplete",
                "sourceMethodSha256":sha(method.encode()),"sourceBefore":before,
                "sourceAfter":after,"sourcePreserved":preserved,
                "firstError":firstError,"postflightError":postflightError,
                "commands":rows,"profiles":profiles,
                "commandLifetimeUnverified":lifetime,
                "actualNativeOpaqueMethodCompiled":compiled,
                "opaqueNonemptySourceExercised":passed,
                "classPointersNeverDereferenced":True,
                "physicalClassPointerOriginProven":False,
                "coherentVmEpochProven":False,
                "completeExecutionSource":False,
                "officialUnitySdkCompiled":False,
                "UnityPlayerExecuted":False,"runtimeAcceptance":False}
        write(output/"RESULT.json",record)
        inventory=[{"path":str(p.relative_to(output)),"bytes":p.stat().st_size,
                    "sha256":sha(p.read_bytes())}
                   for p in sorted(output.rglob("*")) if p.is_file() and not p.is_symlink()]
        write(output/"MANIFEST.json",{"files":inventory,
              "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
              "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":record["result"],"groupsPerProfile":len(GROUPS),
                      "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
