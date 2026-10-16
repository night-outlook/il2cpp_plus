#!/usr/bin/env python3
"""Source-bound P1 runtime-epoch admission contracts; no real Unity/SDK/Player.

Compiles the real Gate, the two exact bounded entry methods under explicit VM
mocks, and an unchanged old-header regression demonstrating the reopen gap.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

FILES=(
    "libil2cpp/vm/AssemblyShadowMetadataLifetime.h",
    "libil2cpp/vm/Runtime.cpp",
    "libil2cpp/vm/Assembly.cpp",
    "libil2cpp/vm/AssemblyShadowVisibility.cpp",
    "libil2cpp/vm/AssemblyShadowBoundedOrdinaryClasses.h",
    "libil2cpp/vm/AssemblyShadowBoundedPhysicalAssemblies.h",
    "tools/r03/diagnostic_stream/metadata_lifetime_gate_tests.cpp",
    "tools/r03/diagnostic_stream/run_metadata_lifetime_gate.py",
    "tools/r03/diagnostic_stream/metadata_lifetime_readers_fixture.cpp",
    "tools/r03/diagnostic_stream/metadata_lifetime_reopen_regression.cpp",
    "tools/r03/diagnostic_stream/fixtures/metadata_lifetime_v0/AssemblyShadowMetadataLifetime.h",
)
OLD_BLOB="88f60fac00a52c0b518e99f1bda31be61eff0ca0"


def sha(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()


def extract(source:str,signature:str)->str:
    if source.count(signature)!=1:raise RuntimeError("Ambiguous C++ symbol: "+signature)
    start=source.index(signature);i=source.index("{",start)
    depth=0;mode=None;escaped=False
    while i<len(source):
        c=source[i];pair=source[i:i+2]
        if mode=="line":
            if c=="\n":mode=None
        elif mode=="block":
            if pair=="*/":mode=None;i+=1
        elif mode in ("'",'"'):
            if escaped:escaped=False
            elif c=="\\":escaped=True
            elif c==mode:mode=None
        elif pair=="//":mode="line";i+=1
        elif pair=="/*":mode="block";i+=1
        elif c in ("'",'"'):mode=c
        elif c=="{":depth+=1
        elif c=="}":
            depth-=1
            if not depth:return source[start:i+1]
        i+=1
    raise RuntimeError("Unbalanced C++ method: "+signature)


def exactly(text:str,value:str)->int:
    if text.count(value)!=1:raise RuntimeError("Expected one source site: "+value)
    return text.index(value)


def verify_source(repo:Path)->tuple[dict,dict]:
    runtime=(repo/FILES[1]).read_text()
    header=(repo/FILES[0]).read_text()
    definition=exactly(runtime,"Gate g_MetadataLifetimeGate;")
    opened=exactly(runtime,"assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit()")
    ready=exactly(runtime,"AssemblyShadowStartup::MarkCoreReady();")
    close=exactly(runtime,"assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();")
    shutdown=exactly(runtime,"shutting_down = true;")
    cleanup=exactly(runtime,"MetadataAllocCleanup();")
    if not opened<ready<close<shutdown<cleanup:raise RuntimeError("Runtime lifecycle source order changed")
    if "std::atomic<uint64_t> state_{detail::kClosed};" not in header:
        raise RuntimeError("Expected one packed epoch/admission state")
    if "sourceEpoch=detail::kInitialEpoch" not in header:
        raise RuntimeError("Default reader must require initial source epoch")
    result={"globalGateDefinitionOffset":definition,"openBeforeReady":True,
            "closeBeforeTeardown":True,"defaultReaderSourceEpoch":1}
    methods={}
    entries=(("assemblies",FILES[2],"bool Assembly::TryCaptureBoundedPhysicalAssemblies(","s_assemblyLock"),
             ("classes",FILES[3],"bool AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(","g_MetadataLock"))
    for name,path,signature,mutex in entries:
        method=extract((repo/path).read_text(),signature)
        ticket=exactly(method,"assembly_shadow_metadata_lifetime::Gate::Reader reader(")
        reject=exactly(method,"if(!reader)return capture.RejectOwnerUnavailable();")
        lock=exactly(method,"os::FastAutoLock lock(&"+mutex+")")
        if not ticket<reject<lock:raise RuntimeError(name+" admission occurs after source lock")
        if not re.search(r"Gate::Reader reader\(\s*assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate\);",method):
            raise RuntimeError(name+" lacks the reviewed initial-source epoch policy")
        methods[name]=method
        result[name+"ReadGateBeforeSourceLock"]=True
    if blob((repo/FILES[10]).read_bytes())!=OLD_BLOB:
        raise RuntimeError("Immutable old-header regression source changed")
    return result,methods


def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    a=parser.parse_args()
    if not __debug__:raise RuntimeError("Python -O prohibited")
    repo=a.native.resolve(strict=True);out=a.output
    if(not out.is_absolute() or out!=out.resolve(strict=False) or out.exists()
       or not out.parent.is_dir() or out==repo or out in repo.parents or repo in out.parents):
        raise RuntimeError("Fresh canonical external output required")
    out.mkdir(mode=0o700)
    before={};contract={};methods={};records=[];profiles=[];error=None

    def snapshot()->dict:
        values={}
        for path in FILES:
            data=(repo/path).read_bytes()
            values[path]={"bytes":len(data),"sha256":sha(data),"gitBlob":blob(data)}
        return values

    def command(label:str,argv:list,expected:int=0)->None:
        began=time.time();timed_out=False
        env=dict(os.environ,ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                                        else "abort_on_error=1:detect_leaks=1"),
                 UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        try:
            p=subprocess.run(list(map(str,argv)),capture_output=True,timeout=240,env=env)
            code,stdout,stderr=p.returncode,p.stdout,p.stderr
        except subprocess.TimeoutExpired as failure:
            code=None;stdout=failure.stdout or b"";stderr=failure.stderr or b"";timed_out=True
        (out/(label+".stdout.bin")).write_bytes(stdout)
        (out/(label+".stderr.bin")).write_bytes(stderr)
        record={"label":label,"argv":list(map(str,argv)),"startUnix":began,"finishUnix":time.time(),
                "exit":code,"expectedExit":expected,"timedOut":timed_out,
                "stdoutSha256":sha(stdout),"stderrSha256":sha(stderr)}
        records.append(record)
        (out/(label+".command.json")).write_text(json.dumps(record,indent=2)+"\n")
        if timed_out or code!=expected:raise RuntimeError(label+" failed; raw streams preserved")

    try:
        before=snapshot()
        contract,methods=verify_source(repo)
        fixture=(repo/FILES[8]).read_text()
        for key,marker in (("assemblies","// __EXACT_PHYSICAL_METHOD__"),("classes","// __EXACT_CLASS_METHOD__")):
            if fixture.count(marker)!=1:raise RuntimeError("Ambiguous source insertion: "+marker)
            fixture=fixture.replace(marker,methods[key])
            (out/(key+"-actual-method.cpp")).write_text(methods[key]+"\n")
        combined=out/"actual-readers-mocked.cpp";combined.write_text(fixture)
        cxx=shutil.which(os.environ.get("CXX","clang++"))
        if not cxx:raise RuntimeError("Compiler unavailable")
        base=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic","-pthread"]
        old=out/"old-reopen";current=out/"current-reopen"
        command("old-reopen-compile",base+["-O2","-iquote",(repo/FILES[10]).parent,repo/FILES[9],"-o",old])
        command("old-reopen-run",[old],expected=4)
        command("current-reopen-compile",base+["-O2","-iquote",repo/"libil2cpp/vm",repo/FILES[9],"-o",current])
        command("current-reopen-run",[current])
        for profile,flags in (("strict",["-O2"]),("sanitized",[
                "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            for name,source in (("gate",repo/FILES[6]),("readers",combined)):
                exe=out/(profile+"-"+name)
                command(profile+"-"+name+"-compile",base+flags+[
                        "-iquote",repo/"libil2cpp/vm",source,"-o",exe])
                command(profile+"-"+name+"-run",[exe])
            profiles.append(profile)
    except Exception as failure:
        error=repr(failure)
        raise
    finally:
        try:preserved=bool(before) and before==snapshot()
        except Exception as failure:
            preserved=False
            if error is None:error=repr(failure)
        passed=error is None and preserved and profiles==["strict","sanitized"] and len(records)==12
        report={"schemaVersion":2,"kind":"R03P1MetadataTeardownAdmissionHost",
                "result":"PassedWithinHostScope" if passed else "FailedOrIncomplete",
                "sourceContract":contract,"sourceBefore":before,"sourcePreserved":preserved,
                "methodSha256":{k:sha(v.encode()) for k,v in methods.items()},
                "originalGateGitBlob":OLD_BLOB,"profilesCompleted":profiles,
                "commands":records,"firstError":error,
                "nativeHeaderRealSourceCompiled":all(any(r["label"]==p+"-gate-compile" and r["exit"]==0 for r in records) for p in ("strict","sanitized")),
                "actualEntryMethodsHostCompiled":all(any(r["label"]==p+"-readers-compile" and r["exit"]==0 for r in records) for p in ("strict","sanitized")),
                "productionRuntimeBodiesHostCompiled":False,
                "mockCaptureRegistriesLocksAndClassCallback":True,
                "defaultSourceEpoch":1,"allVmEpochProven":False,
                "officialUnitySDKCompiled":False,"UnityPlayerExecuted":False,
                "engineOwnerProven":False,"shutdownDeadlockFreedomProven":False,
                "runtimeAcceptance":False}
        (out/"RESULT.json").write_text(json.dumps(report,indent=2)+"\n")
        entries=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,"sha256":sha(p.read_bytes())}
                 for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":entries},indent=2)+"\n")
    print(json.dumps({"result":report["result"],"profiles":profiles,"commands":len(records),
                      "methodSha256":report["methodSha256"]},indent=2))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
