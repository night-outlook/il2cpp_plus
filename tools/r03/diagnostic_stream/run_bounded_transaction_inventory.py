#!/usr/bin/env python3
"""Compile the actual additive transaction-inventory method under source mocks.

Host contract only. No Unity native SDK, class metadata lifecycle, callback
safe point, native file transport or original Player semantic acceptance.
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

SYMBOL="bool AssemblyShadow::TryCaptureBoundedTransactionInventory("
PLACEHOLDER="// __EXACT_PRODUCTION_METHOD_INSERTED_HERE__"
REQUIRED=(
    "std::lock_guard<std::mutex> lock(transaction.mutex)",
    "capture.Begin(status)",
    "capture.AddStable(",
    "capture.AddClosure(row)",
    "capture.AddCommit(",
    "capture.AddEvent(row)",
    "capture.EndLocked()",
    "capture.Seal(stable,",
    "configurationBefore==s_configuration.load(std::memory_order_acquire)",
    "failureBefore==failureAfter",
    "sampledGeneration==ActiveGeneration()",
    "if (!member.candidate) return capture.RejectInvalidSource()",
)

def sha(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()
def extract(text:str)->str:
    if text.count(SYMBOL)!=1:raise RuntimeError("Product method missing or repeated")
    start=text.index(SYMBOL)
    open_brace=text.index("{",start)
    depth=0
    mode=None
    slash_escaped=False
    pos=open_brace
    while pos<len(text):
        ch=text[pos];pair=text[pos:pos+2]
        if mode=="line":
            if ch=="\n":mode=None
        elif mode=="block":
            if pair=="*/":mode=None;pos+=1
        elif mode in ("'",'"'):
            if slash_escaped:slash_escaped=False
            elif ch=="\\":slash_escaped=True
            elif ch==mode:mode=None
        elif pair=="//":mode="line";pos+=1
        elif pair=="/*":mode="block";pos+=1
        elif ch in ("'",'"'):mode=ch
        elif ch=="{":depth+=1
        elif ch=="}":
            depth-=1
            if depth==0:return text[start:pos+1]
        pos+=1
    raise RuntimeError("Product C++ function has unbalanced braces")

def invoke(argv:list[str],out:Path,label:str,commands:list[dict]):
    t=time.time()
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE="1",
             ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                           else "abort_on_error=1:detect_leaks=1"),
             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    p=subprocess.run([str(x) for x in argv],capture_output=True,env=env,check=False,timeout=210)
    (out/(label+".stdout")).write_bytes(p.stdout)
    (out/(label+".stderr")).write_bytes(p.stderr)
    item=dict(name=label,argv=list(map(str,argv)),startedUnix=t,finishedUnix=time.time(),
              exit=p.returncode,stdoutSha256=sha(p.stdout),stderrSha256=sha(p.stderr))
    (out/(label+".command.json")).write_text(json.dumps(item,indent=2)+"\n")
    commands.append(item)
    if p.returncode:raise RuntimeError(f"{label} failed with exit {p.returncode}; preserve logs")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    a=parser.parse_args()
    if not __debug__:raise RuntimeError("Python assertion policy must not be disabled")
    repo=a.native.resolve(strict=True)
    root=a.output
    if not (root.is_absolute() and root==root.resolve(strict=False)
            and not root.exists() and root.parent.is_dir()
            and root not in repo.parents and repo not in root.parents and root!=repo):
        raise RuntimeError("Unused absolute output root external to sources required")
    relative=[
        "libil2cpp/vm/AssemblyShadow.cpp",
        "libil2cpp/vm/AssemblyShadow.h",
        "libil2cpp/vm/AssemblyShadowBoundedTransactionInventory.h",
        "libil2cpp/vm/AssemblyShadowBoundedTransactionStatus.h",
        "libil2cpp/vm/AssemblyShadowFrozenGeneralCapture.h",
        "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
        "libil2cpp/vm/AssemblyShadowDiagnostics.h",
        "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
        "tools/r03/diagnostic_stream/bounded_transaction_inventory_fixture.cpp",
        "tools/r03/diagnostic_stream/allocation_probe.cpp",
        "tools/r03/diagnostic_stream/run_bounded_transaction_inventory.py",
    ]
    source=[repo/p for p in relative]
    before={str(p):{"bytes":p.stat().st_size,"sha256":sha(p.read_bytes()),
                    "gitBlob":blob(p.read_bytes())}for p in source}
    original=(repo/relative[0]).read_text()
    method=extract(original)
    if not all(token in method for token in REQUIRED):
        raise RuntimeError("Live product method missing mandatory source checks")
    if "static bool TryCaptureBoundedTransactionInventory(" not in (repo/relative[1]).read_text():
        raise RuntimeError("Live native public API not declared")
    fixture=(repo/relative[8]).read_text()
    if fixture.count(PLACEHOLDER)!=1:raise RuntimeError("Fixture insertion marker mismatch")
    root.mkdir(mode=0o700)
    commands=[]
    completed=[]
    error=None
    try:
        (root/"source-method.cpp").write_text(method+"\n")
        (root/"mock-test.cpp").write_text(fixture.replace(PLACEHOLDER,method))
        stub=root/"stubs";stub.mkdir()
        (stub/"il2cpp-config.h").write_text("#pragma once\n// Mock-only header, not official Unity SDK.\n")
        cxx=shutil.which(os.environ.get("CXX","clang++"))
        if not cxx:raise RuntimeError("No CXX compiler")
        for profile,opts in (("strict",["-O2"]),("sanitized",[
            "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=root/(profile+".exe")
            argv=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                  "-pthread","-iquote",repo/"libil2cpp/vm","-iquote",stub,*opts,
                  root/"mock-test.cpp",repo/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                  repo/"libil2cpp/vm/AssemblyShadowDiagnostics.cpp","-o",exe]
            invoke(argv,root,profile+"-compile",commands)
            invoke([exe],root,profile+"-run",commands)
            completed.append(profile)
    except Exception as e:
        error=repr(e)
        raise
    finally:
        unchanged=all(before[str(p)]=={"bytes":p.stat().st_size,"sha256":sha(p.read_bytes()),
                                       "gitBlob":blob(p.read_bytes())} for p in source)
        passed=completed==["strict","sanitized"] and unchanged
        record=dict(kind="P1TransactionInventoryActualProductMethodHost",
                    result="PassedWithinHostScope" if passed else "FailedOrIncomplete",
                    actualProductionMethodBodyCompiled=True,
                    sourceSha256=sha(method.encode()),sourceBefore=before,sourcePreserved=unchanged,
                    completedProfiles=completed,commands=commands,firstFailure=error,
                    realVmClassOrMetadataAcquisition=False,realUnitySdkCompiler=False,
                    singletonPrewarmProven=False,completeGeneralOrExecutionIntegrated=False,
                    transactionMutexHeldInMock=True,globalVmEpochProven=False,
                    actualUnityPlayerRun=False,runtimeAcceptance=False)
        (root/"RESULT.json").write_text(json.dumps(record,indent=2)+"\n")
        files=[dict(path=str(p.relative_to(root)),bytes=p.stat().st_size,
                    sha256=sha(p.read_bytes())) for p in sorted(root.rglob("*")) if p.is_file()]
        (root/"MANIFEST.json").write_text(json.dumps({"files":files},indent=2)+"\n")
    print(json.dumps({"result":record["result"],"methodSha256":record["sourceSha256"],
                      "profiles":completed,"commands":len(commands)},indent=2))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
