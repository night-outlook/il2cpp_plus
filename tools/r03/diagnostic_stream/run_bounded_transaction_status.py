#!/usr/bin/env python3
"""Compile the ACTUAL new production VM status method under explicit VM mocks.

Host-only source-bound tests: do not infer installed Unity or engine ownership.
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

MARKER = "// __PRODUCTION_CAPTURE_FUNCTION_IS_INSERTED_HERE__"
SYMBOL = "bool AssemblyShadow::TryCaptureBoundedTransactionStatus("
REQUIRED = (
    "std::lock_guard<std::mutex> lock(transaction.mutex)",
    "capture.Begin(status)",
    "capture.ObserveFirstFailure(stable,",
    "failureBefore==failureAfter",
    "stateBefore==sampledState",
    "capture.Seal()",
    "s_typeFailure.load(std::memory_order_acquire)",
)

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

def extract(cpp: str) -> str:
    if cpp.count(SYMBOL) != 1:
        raise RuntimeError("Actual product status method is missing or ambiguous")
    start = cpp.index(SYMBOL)
    opening = cpp.index("{", start)
    depth = 0
    mode = None
    escaped = False
    i = opening
    while i < len(cpp):
        ch = cpp[i]
        nxt = cpp[i:i+2]
        if mode == "line":
            if ch == "\n": mode = None
        elif mode == "block":
            if nxt == "*/": mode = None; i += 1
        elif mode in ("'", '"'):
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == mode: mode = None
        elif nxt == "//":
            mode = "line"; i += 1
        elif nxt == "/*":
            mode = "block"; i += 1
        elif ch in ("'", '"'):
            mode = ch
        elif ch == "{": depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return cpp[start:i+1]
        i += 1
    raise RuntimeError("Unbalanced actual production C++ function")

def run(argv: list[str], folder: Path, name: str, commands: list) -> None:
    started = time.time()
    env = dict(os.environ, ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform == "darwin"
                                      else "abort_on_error=1:detect_leaks=1"),
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    result = subprocess.run([str(x) for x in argv], capture_output=True, env=env,
                            timeout=150, check=False)
    (folder/(name+".stdout")).write_bytes(result.stdout)
    (folder/(name+".stderr")).write_bytes(result.stderr)
    record = {"name":name,"argv":list(map(str,argv)),"startedUnix":started,
              "finishedUnix":time.time(),"exit":result.returncode,
              "stdoutSha256":digest(result.stdout),"stderrSha256":digest(result.stderr)}
    (folder/(name+".command.json")).write_text(json.dumps(record,indent=2)+"\n")
    commands.append(record)
    if result.returncode:
        raise RuntimeError(f"{name} failed: return {result.returncode}; logs retained")

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not __debug__: raise RuntimeError("Python assertions must be enabled")
    native=args.native.resolve(strict=True)
    out=args.output
    if (not out.is_absolute() or out!=out.resolve(strict=False)
            or out.exists() or not out.parent.is_dir()
            or out==native or out in native.parents or native in out.parents):
        raise RuntimeError("Unused canonical external output path required")
    product=native/"libil2cpp/vm/AssemblyShadow.cpp"
    header=native/"libil2cpp/vm/AssemblyShadowBoundedTransactionStatus.h"
    public=native/"libil2cpp/vm/AssemblyShadow.h"
    fixture=native/"tools/r03/diagnostic_stream/bounded_transaction_source_fixture.cpp"
    allocator=native/"tools/r03/diagnostic_stream/allocation_probe.cpp"
    runner=Path(__file__).resolve()
    sources=(product,header,public,fixture,allocator,runner)
    before={str(p):{"gitBlob":blob(p.read_bytes()),"sha256":digest(p.read_bytes())} for p in sources}
    current=extract(product.read_text())
    if not all(x in current for x in REQUIRED):
        raise RuntimeError("Actual method's locking/precedence structure requires review")
    if SYMBOL not in public.read_text():
        # public contains unqualified method declaration, not fully qualified
        if "TryCaptureBoundedTransactionStatus(" not in public.read_text():
            raise RuntimeError("Public native declaration missing")
    body=fixture.read_text()
    if body.count(MARKER)!=1:
        raise RuntimeError("Cannot locate exact fixture insertion marker")
    out.mkdir(mode=0o700)
    commands=[]
    error=None
    completed=[]
    try:
        (out/"source-method.cpp").write_text(current+"\n")
        (out/"mock-compilation.cpp").write_text(body.replace(MARKER,current))
        stub=out/"stubs"
        stub.mkdir()
        (stub/"il2cpp-config.h").write_text("#pragma once\n// ONLY a host mock. Not an official Unity SDK.\n")
        compiler=shutil.which(os.environ.get("CXX","clang++"))
        if not compiler:
            raise RuntimeError("Compiler unavailable")
        for profile, extra in (("strict",["-O2"]),
                               ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                                             "-fsanitize=address,undefined"])):
            binary=out/(profile+".exe")
            cmd=[compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                 "-pthread","-iquote",native/"libil2cpp/vm","-iquote",stub,*extra,
                 out/"mock-compilation.cpp",allocator,"-o",binary]
            run(cmd,out,profile+"-compile",commands)
            run([binary],out,profile+"-run",commands)
            completed.append(profile)
    except Exception as e:
        error=repr(e)
        raise
    finally:
        unchanged=all(before[str(p)]=={"gitBlob":blob(p.read_bytes()),
                                         "sha256":digest(p.read_bytes())} for p in sources)
        result={"kind":"P1BoundedTransactionActualMethodHostMock",
                "result":"PassedWithinHostScope" if len(completed)==2 and unchanged else "FailedOrIncomplete",
                "profilesCompleted":completed,"commands":commands,"firstError":error,
                "sources":before,"sourceUnchanged":unchanged,
                "methodSha256":digest(current.encode()),
                "actualNativeMethodBodyCompiled":True,"VMObjectsAreExplicitMocks":True,
                "liveVMSourceEpochProven":False,"fullGeneralCaptureIntegrated":False,
                "firstUseClassesAndR02Integrated":False,"noUnityPlayer":True,
                "officialSdkCompile":False,"runtimeAcceptance":False}
        (out/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        listing=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                  "sha256":digest(p.read_bytes())}
                 for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":listing},indent=2)+"\n")
    print(json.dumps({"result":result["result"],"profilesCompleted":completed,
                      "methodSha256":result["methodSha256"]},indent=2))

if __name__=="__main__":
    main()
