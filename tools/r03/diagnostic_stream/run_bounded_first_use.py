#!/usr/bin/env python3
"""Compile the REAL P1 first-use VM source method with explicit C++ test mocks.

It is intentionally NOT an official Unity SDK/Player test, and a class name
requires an external metadata-lifetime ticket unavailable to the live method.
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

SYMBOL="bool AssemblyShadow::TryCaptureBoundedFirstUses("
MARKER="// __EXACT_PRODUCTION_FIRST_USE_METHOD__"
REQUIRED=(
    "UsageLock lock;",
    "raw.opaqueClass=use.klass",
    "capture.AddRaw(raw)",
    "capture.EndRaw(s_usageGeneration)",
    "capture.AddCandidateName(index,",
    "capture.EndNames()",
    "verifiedGeneration=s_usageGeneration",
    "capture.Seal(verifiedGeneration,",
    "s_candidates.load(std::memory_order_acquire)==registry",
    "static_assert(sizeof(UseRecord::detail)",
)
FORBIDDEN=(
    "use.klass->",
    "Class::",
    "FormatMetadataOnly(",
    "g_MetadataLock",
    "MetadataCache::",
    "ResolvePhysicalClass(",
)

def sha(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def extract(src:str)->str:
    if src.count(SYMBOL)!=1:raise RuntimeError("Active product first-use method missing or duplicated")
    start=src.index(SYMBOL)
    brace=src.index("{",start)
    depth=0
    mode=None
    escape=False
    i=brace
    while i<len(src):
        ch=src[i];pair=src[i:i+2]
        if mode=="line":
            if ch=="\n":mode=None
        elif mode=="block":
            if pair=="*/":mode=None;i+=1
        elif mode in ("'",'"'):
            if escape:escape=False
            elif ch=="\\":escape=True
            elif ch==mode:mode=None
        elif pair=="//":mode="line";i+=1
        elif pair=="/*":mode="block";i+=1
        elif ch in ("'",'"'):mode=ch
        elif ch=="{":depth+=1
        elif ch=="}":
            depth-=1
            if depth==0:return src[start:i+1]
        i+=1
    raise RuntimeError("Unbalanced actual native function")

def command(argv:list,root:Path,key:str,records:list):
    began=time.time()
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE="1",
        ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                      else "abort_on_error=1:detect_leaks=1"),
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    result=subprocess.run([str(v) for v in argv],capture_output=True,
                          timeout=160,env=env,check=False)
    (root/(key+".stdout")).write_bytes(result.stdout)
    (root/(key+".stderr")).write_bytes(result.stderr)
    entry=dict(name=key,argv=list(map(str,argv)),start=began,end=time.time(),
               exit=result.returncode,stdoutSha256=sha(result.stdout),
               stderrSha256=sha(result.stderr))
    records.append(entry)
    (root/(key+".command.json")).write_text(json.dumps(entry,indent=2)+"\n")
    if result.returncode:
        raise RuntimeError(f"{key} failed with {result.returncode}; preserve raw logs")

def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    if not __debug__:raise RuntimeError("Python assertions disabled")
    repo=args.native.resolve(strict=True)
    out=args.output
    if(not out.is_absolute() or out!=out.resolve(strict=False)
       or not out.parent.is_dir() or out.exists() or out in repo.parents
       or repo in out.parents or out==repo):
        raise RuntimeError("New canonical external output path required")
    paths=[
      "libil2cpp/vm/AssemblyShadow.cpp",
      "libil2cpp/vm/AssemblyShadow.h",
      "libil2cpp/vm/AssemblyShadowBoundedFirstUseCapture.h",
      "libil2cpp/vm/AssemblyShadowFrozenGeneralCapture.h",
      "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
      "libil2cpp/vm/AssemblyShadowDiagnostics.h",
      "libil2cpp/vm/AssemblyShadowTypes.h",
      "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
      "tools/r03/diagnostic_stream/bounded_first_use_fixture.cpp",
      "tools/r03/diagnostic_stream/allocation_probe.cpp",
      "tools/r03/diagnostic_stream/run_bounded_first_use.py",
    ]
    sources=[repo/p for p in paths]
    before={str(p):{"bytes":p.stat().st_size,"sha256":sha(p.read_bytes()),
                    "gitBlob":blob(p.read_bytes())} for p in sources}
    actual=extract(sources[0].read_text())
    if not all(x in actual for x in REQUIRED):
        raise RuntimeError("First-use source lock/ordering contract changed")
    # Check executable statements ONLY: the original comments explicitly
    # discuss methods/metadata forbidden on this short usage spinlock.
    statements="\n".join(line.split("//",1)[0] for line in actual.splitlines())
    if any(token in statements for token in FORBIDDEN):
        raise RuntimeError("First-use method gained unreviewed class/metadata access")
    if "static bool TryCaptureBoundedFirstUses(" not in sources[1].read_text():
        raise RuntimeError("Enabled native API declaration missing")
    fixture=sources[8].read_text()
    if fixture.count(MARKER)!=1:raise RuntimeError("Ambiguous test source insertion")
    out.mkdir(mode=0o700)
    commands=[];completed=[];failure=None
    try:
        (out/"actual-method.cpp").write_text(actual+"\n")
        (out/"mock-execution.cpp").write_text(fixture.replace(MARKER,actual))
        stubs=out/"stubs";stubs.mkdir()
        (stubs/"il2cpp-config.h").write_text("#pragma once\n// HOST MOCK, not Unity SDK.\n")
        cxx=shutil.which(os.environ.get("CXX","clang++"))
        if not cxx:raise RuntimeError("No host compiler available")
        for profile,flags in (("strict",["-O2"]),("sanitized",[
            "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            flags=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                   "-pthread","-iquote",repo/"libil2cpp/vm","-iquote",stubs,
                   *flags,
                   out/"mock-execution.cpp",
                   repo/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                   repo/"libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
                   "-o",exe]
            command(flags,out,profile+"-compile",commands)
            command([exe],out,profile+"-run",commands)
            completed.append(profile)
    except Exception as error:
        failure=repr(error)
        raise
    finally:
        preserved=all(before[str(p)]=={
            "bytes":p.stat().st_size,"sha256":sha(p.read_bytes()),
            "gitBlob":blob(p.read_bytes())}for p in sources)
        passed=completed==["strict","sanitized"] and preserved
        result={
          "kind":"P1BoundedFirstUseActualSourceHost",
          "result":"PassedWithinHostScope" if passed else "FailedOrIncomplete",
          "compiledActualProductionMethod":True,
          "methodSha256":sha(actual.encode()),
          "sourceBefore":before,"sourcePreserved":preserved,
          "profilesCompleted":completed,"commands":commands,"firstError":failure,
          "realMetadataClassLifetimeTicket":False,
          "classBearingLiveProducerComplete":False,
          "classPointerDereferencedInSourceMethod":False,
          "actualUnitySdkCompile":False,
          "realPlayerRun":False,"semanticAcceptance":False
        }
        (out/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        files=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                "sha256":sha(p.read_bytes())} for p in sorted(out.rglob("*")) if p.is_file()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":files},indent=2)+"\n")
    print(json.dumps({"result":result["result"],"profiles":completed,
                      "methodSha256":result["methodSha256"]},indent=2))
    if not passed:raise SystemExit(1)

if __name__=="__main__":
    main()
