#!/usr/bin/env python3
"""Source-bound R03 P1 metadata teardown admission tests (host only).

The test compiles the current production header with strict/sanitized C++17,
checks admission/close/drain/restart and verifies Runtime/reader integration
source order. No actual Unity SDK, VM metadata, engine owner or Player is used.
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

FILES = (
    "libil2cpp/vm/AssemblyShadowMetadataLifetime.h",
    "libil2cpp/vm/Runtime.cpp",
    "libil2cpp/vm/Assembly.cpp",
    "libil2cpp/vm/AssemblyShadowVisibility.cpp",
    "libil2cpp/vm/AssemblyShadowBoundedOrdinaryClasses.h",
    "libil2cpp/vm/AssemblyShadowBoundedPhysicalAssemblies.h",
    "tools/r03/diagnostic_stream/metadata_lifetime_gate_tests.cpp",
    "tools/r03/diagnostic_stream/run_metadata_lifetime_gate.py",
)

def sha256(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def exactly(text:str, substring:str, label:str)->int:
    if text.count(substring)!=1:
        raise RuntimeError(f"Source-contract violation {label}: {substring!r} count={text.count(substring)}")
    return text.index(substring)

def verify_source(repo:Path)->dict:
    runtime=(repo/FILES[1]).read_text()
    asm=(repo/FILES[2]).read_text()
    classes=(repo/FILES[3]).read_text()
    h=(repo/FILES[0]).read_text()
    if "std::atomic<uint32_t> state_{kClosed};" not in h:
        raise RuntimeError("Metadata gate is no longer closed before Init")
    definition=exactly(runtime,"assembly_shadow_metadata_lifetime::Gate g_MetadataLifetimeGate;","single gate storage")
    open_site=exactly(runtime,"assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit()","Init admission")
    mark_site=exactly(runtime,"AssemblyShadowStartup::MarkCoreReady();","startup ready")
    close_site=exactly(runtime,"assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();","Shutdown drain")
    terminal=exactly(runtime,"shutting_down = true;","shutdown flag")
    teardown=exactly(runtime,"MetadataAllocCleanup();","metadata cleanup")
    if not (open_site<mark_site<close_site<terminal<teardown):
        raise RuntimeError("Runtime admission/drain ordering changed")
    results={"globalGateDefinitionOffset":definition,"openBeforeReady":True,
             "closeBeforeTeardown":True}
    for name,text,mutex in (("classes",classes,"os::FastAutoLock lock(&g_MetadataLock)"),
                            ("assemblies",asm,"os::FastAutoLock lock(&s_assemblyLock)")):
        symbol=("bool AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(" if name=="classes" else
                "bool Assembly::TryCaptureBoundedPhysicalAssemblies(")
        start=exactly(text,symbol,name+" reader definition")
        end=text.find("\n}",start)
        if end==-1:
            end=text.find("\n    }",start)
        if end==-1:raise RuntimeError("Cannot find bounded reader closure")
        body=text[start:end]
        guard="assembly_shadow_metadata_lifetime::Gate::Reader reader("
        g=exactly(body,guard,name+" read gate")
        reject=exactly(body,"if(!reader)return capture.RejectOwnerUnavailable();",name+" gate reject")
        lock=exactly(body,mutex,name+" source lock")
        if not g<reject<lock:
            raise RuntimeError(name+" takes source lock before lifetime admission")
        results[name+"ReadGateBeforeSourceLock"]=True
    for p in (FILES[4],FILES[5]):
        h=(repo/p).read_text()
        if "OwnerUnavailable" not in h or "RejectOwnerUnavailable" not in h:
            raise RuntimeError("Unavailable owner not separately reported in "+p)
    return results

def main()->None:
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument("--native",required=True,type=Path)
    a.add_argument("--output",required=True,type=Path)
    args=a.parse_args()
    if not __debug__:raise RuntimeError("Python -O forbidden")
    repo=args.native.resolve(strict=True)
    out=args.output
    if(not out.is_absolute() or out!=out.resolve(strict=False) or out.exists()
       or not out.parent.is_dir() or out==repo or out in repo.parents or repo in out.parents):
        raise RuntimeError("Require fresh canonical output path outside native sources")
    sources=[repo/f for f in FILES]
    before={f:{"bytes":(repo/f).stat().st_size,
               "sha256":sha256((repo/f).read_bytes()),
               "gitBlob":blob((repo/f).read_bytes())} for f in FILES}
    contract=verify_source(repo)
    out.mkdir(mode=0o700)
    cxx=shutil.which(os.getenv("CXX","clang++"))
    if not cxx:raise RuntimeError("CXX not available")
    commands=[];profiles=[];error=None
    try:
        for profile,extra in (("strict",["-O2"]),("sanitized",[
                "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            compiler=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                      "-pthread","-iquote",repo/"libil2cpp/vm",*extra,
                      repo/FILES[6],"-o",exe]
            for label,argv in ((profile+"-compile",compiler),(profile+"-run",[exe])):
                started=time.time()
                env=dict(os.environ,
                    ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                                  else "abort_on_error=1:detect_leaks=1"),
                    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
                result=subprocess.run(list(map(str,argv)),capture_output=True,
                                      timeout=90,env=env)
                (out/(label+".stdout.bin")).write_bytes(result.stdout)
                (out/(label+".stderr.bin")).write_bytes(result.stderr)
                commands.append({"label":label,"argv":list(map(str,argv)),
                    "startUnix":started,"finishUnix":time.time(),
                    "exit":result.returncode,"stdoutSha256":sha256(result.stdout),
                    "stderrSha256":sha256(result.stderr)})
                if result.returncode:raise RuntimeError(label+": exit "+str(result.returncode))
            profiles.append(profile)
    except Exception as exc:
        error=repr(exc)
        raise
    finally:
        unchanged=all(before[f]=={
            "bytes":(repo/f).stat().st_size,"sha256":sha256((repo/f).read_bytes()),
            "gitBlob":blob((repo/f).read_bytes())} for f in FILES)
        success=profiles==["strict","sanitized"] and unchanged
        record={"kind":"R03P1MetadataTeardownAdmissionHost", "result":
             "PassedWithinHostScope" if success else "FailedOrIncomplete",
             "sourceContract":contract,"sourceBefore":before,"sourcePreserved":unchanged,
             "profilesCompleted":profiles,"commands":commands,"firstError":error,
             "nativeHeaderRealSourceCompiled":True,
             "productionRuntimeBodiesHostCompiled":False,
             "originalGettersUnmodified":True,
             "officialUnitySDKCompiled":False,"UnityPlayerExecuted":False,
             "allVmEpochProven":False,"engineOwnerProven":False,
             "shutdownDeadlockFreedomProven":False,"runtimeAcceptance":False}
        (out/"RESULT.json").write_text(json.dumps(record,indent=2)+"\n")
        entries=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                  "sha256":sha256(p.read_bytes())}
                 for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":entries},indent=2)+"\n")
    print(json.dumps({"result":record["result"],"profiles":profiles,
                      "sourceContract":contract},indent=2))
    if not success:raise SystemExit(1)

if __name__=="__main__":main()
