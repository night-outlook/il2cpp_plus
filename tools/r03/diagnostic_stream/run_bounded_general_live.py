#!/usr/bin/env python3
"""R03 P1 exact inactive live-source orchestrator with real bounded types.

Compiles the real AssemblyShadowBoundedGeneralLiveSource.inl body, not a model
transcription, against explicit source/VM mocks. No official SDK, Player,
coherent VM epoch, metadata origin, runtime admission or GC/RSS is proven.
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

SRC="libil2cpp/vm/AssemblyShadowBoundedGeneralLiveSource.inl"
MOCK="tools/r03/diagnostic_stream/bounded_general_live_fixture.cpp"
MARKER="// __EXACT_REAL_PRODUCTION_GENERAL_LIVE_SOURCE__"
FILES=(
 SRC,
 "libil2cpp/vm/AssemblyShadow.cpp",
 "libil2cpp/vm/AssemblyShadow.h",
 "libil2cpp/vm/AssemblyShadowBoundedGeneralLive.h",
 "libil2cpp/vm/AssemblyShadowBoundedGeneralComposer.h",
 "libil2cpp/vm/AssemblyShadowFrozenGeneralCapture.h",
 "libil2cpp/vm/AssemblyShadowBoundedTransactionInventory.h",
 "libil2cpp/vm/AssemblyShadowBoundedTransactionStatus.h",
 "libil2cpp/vm/AssemblyShadowBoundedFirstUseCapture.h",
 "libil2cpp/vm/AssemblyShadowBoundedPhysicalAssemblies.h",
 "libil2cpp/vm/AssemblyShadowBoundedOrdinaryClasses.h",
 "libil2cpp/vm/AssemblyShadowMetadataLifetime.h",
 "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
 "libil2cpp/vm/AssemblyShadowDiagnostics.h",
 "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
 "libil2cpp/vm/AssemblyShadowTypes.h",
 MOCK,
 "tools/r03/diagnostic_stream/allocation_probe.cpp",
 "tools/r03/diagnostic_stream/run_bounded_general_live.py",
)
GROUPS=(
 "actual-method-legacy-v1-parity",
 "actual-method-zero-new-after-prewarm",
 "missing-owned-source-denied",
 "transaction-source-failure",
 "first-use-source-failure",
 "physical-assembly-source-failure",
 "ordinary-class-source-failure",
 "usage-generation-changed-during-read",
 "usage-generation-changed-during-compose",
 "terminal-state-changed-during-read",
 "bounded-startup-scan-refusal",
 "single-owner-one-shot-output",
 "closed-metadata-admission-refusal",
)
REQUIRED=(
 "assembly_shadow_metadata_lifetime::Gate::Reader reader(",
 "if(!reader)",
 "receipt=live::Receipt{};",
 "before.SameAs(collected)",
 "collected.SameAs(finished)",
 "TryCaptureBoundedTransactionInventory(",
 "TryCaptureBoundedFirstUses(",
 "TryCaptureBoundedPhysicalAssemblies(",
 "TryCaptureBoundedOrdinaryClasses(",
 "scratch.AddStartup(",
 "composer::Compose(",
 "receipt.observableSourceFenceStable=true;",
)
def sha(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def provenance(repo:Path)->tuple[str,str]:
    code=(repo/SRC).read_text()
    assembly=(repo/"libil2cpp/vm/AssemblyShadow.cpp").read_text()
    header=(repo/"libil2cpp/vm/AssemblyShadow.h").read_text()
    mock=(repo/MOCK).read_text()
    include='#include "AssemblyShadowBoundedGeneralLiveSource.inl"'
    if assembly.count(include)!=1 or assembly.count('#include "AssemblyShadowBoundedGeneralLive.h"')!=1:
        raise RuntimeError("Real live-source body/header not included in production AssemblyShadow.cpp")
    if "static bool TryCaptureBoundedGeneralLive(" not in header:
        raise RuntimeError("Native AssemblyShadow API not declared")
    if code.count("bool AssemblyShadow::TryCaptureBoundedGeneralLive(")!=1:
        raise RuntimeError("Real orchestrator missing or ambiguous")
    if not all(code.count(text)==1 for text in REQUIRED):
        raise RuntimeError("Source admission/ordering/conservative fence contract changed")
    if mock.count(MARKER)!=1:
        raise RuntimeError("Fixture insertion marker missing or repeated")
    # SourceFailure/SourceChanged print their supplied group name once from
    # shared helpers; the distinct names are checked at actual execution.
    if mock.count('std::cout<<"GROUP:')!=8:
        raise RuntimeError("Unexpected number of source group emitters")
    if not all(name in mock for name in GROUPS):
        raise RuntimeError("Missing executable negative or ownership coverage")
    return code,mock.replace(MARKER,code)

def run(argv:list,root:Path,name:str,commands:list)->bytes:
    # A timeout's bytes are a captured PREFIX, never a complete diagnostic.
    # subprocess.run controls its direct child, not every descendant/session.
    # Persist that distinction before propagating any command failure.
    began=time.time()
    env=dict(os.environ)
    env["ASAN_OPTIONS"]=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                         else "abort_on_error=1:detect_leaks=1")
    env["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    receipt={"name":name,"argv":list(map(str,argv)),"startedUnix":began,
             "exit":None,"timedOut":False,"interrupted":False,
             "outputCompleteness":"Unavailable",
             "commandLifetimeUnverified":False,"descendantQuiescenceProven":False}
    stdout=stderr=None
    try:
        result=subprocess.run([str(x) for x in argv],env=env,capture_output=True,timeout=180)
        stdout,stderr=result.stdout,result.stderr
        receipt.update(exit=result.returncode,outputCompleteness="CompleteDirectChildStreams")
    except subprocess.TimeoutExpired as error:
        stdout,stderr=error.stdout,error.stderr
        receipt.update(timedOut=True,outputCompleteness="CapturedPrefix",
                       commandLifetimeUnverified=True,exception=repr(error))
        raise
    except KeyboardInterrupt as error:
        receipt.update(interrupted=True,commandLifetimeUnverified=True,
                       exception=type(error).__name__)
        raise
    except BaseException as error:
        receipt["exception"]=repr(error)
        # Unknown execution outcome must not be treated as a completed child.
        receipt["commandLifetimeUnverified"]=not isinstance(error,OSError)
        raise
    finally:
        for stream,data in (("stdout",stdout),("stderr",stderr)):
            receipt[stream+"Available"]=data is not None
            if data is None:data=b""
            if not isinstance(data,bytes):
                raise TypeError("Command capture must return bytes, not decoded text")
            (root/(name+"."+stream+".bin")).write_bytes(data)
            receipt[stream+"Sha256"]=sha(data)
            receipt[stream+"CapturedBytes"]=len(data)
        receipt["finishedUnix"]=time.time()
        commands.append(receipt)
        (root/(name+".command.json")).write_text(json.dumps(receipt,indent=2)+"\n")
    if receipt["exit"]!=0:
        raise RuntimeError(f"{name}: exit={receipt['exit']}; inspect raw output")
    return stdout

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Python assertions disabled")
    native=a.native.resolve(strict=True)
    output=a.output
    if(not native.is_dir() or not output.is_absolute() or
       output!=output.resolve(strict=False) or output.exists() or
       not output.parent.is_dir() or
       native==output or native in output.parents or output in native.parents):
        raise RuntimeError("New canonical external output required")
    before={name:{"bytes":len((native/name).read_bytes()),
                  "sha256":sha((native/name).read_bytes()),
                  "gitBlob":blob((native/name).read_bytes())} for name in FILES}
    actual,assembled=provenance(native)
    output.mkdir(mode=0o700)
    (output/"stubs").mkdir()
    (output/"stubs/il2cpp-config.h").write_text(
        "#pragma once\n// HOST source mock, no Unity SDK.\n")
    (output/"actual-source.inl").write_text(actual)
    (output/"mock-execution.cpp").write_text(assembled)
    commands=[];profiles={};firstError=None
    try:
        cxx=shutil.which(os.environ.get("CXX","clang++"))
        if not cxx:raise RuntimeError("Missing C++ compiler")
        for profile,extra in (("strict",["-O2"]),("sanitized",[
          "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=output/(profile+".exe")
            cmd=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                 "-pthread","-iquote",native/"libil2cpp/vm",
                 "-iquote",output/"stubs",*extra,
                 output/"mock-execution.cpp",
                 native/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                 native/"libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
                 "-o",exe]
            run(cmd,output,profile+"-compile",commands)
            raw=run([exe],output,profile+"-run",commands).decode("utf-8")
            groups=tuple(x.split("GROUP:",1)[1] for x in raw.splitlines()
                         if x.startswith("GROUP:"))
            if groups!=GROUPS or not raw.rstrip().endswith(
                    "P1 REAL GENERAL LIVE SOURCE METHOD HOST PASS"):
                raise RuntimeError(f"{profile}: missing/duplicate/changed case groups")
            profiles[profile]={"result":"PassedWithinHostScope","caseGroups":list(groups)}
    except BaseException as error:
        firstError=repr(error)
        raise
    finally:
        postflightError=None
        after={}
        try:
            after={name:{"bytes":len((native/name).read_bytes()),
                         "sha256":sha((native/name).read_bytes()),
                         "gitBlob":blob((native/name).read_bytes())} for name in FILES}
        except Exception as error:
            postflightError=repr(error)
        lifetimeUnknown=any(c.get("commandLifetimeUnverified",False) for c in commands)
        preserved=postflightError is None and before==after and not lifetimeUnknown
        compiled=(len([c for c in commands if c["name"].endswith("-compile") and c["exit"]==0])==2)
        passed=(firstError is None and preserved and list(profiles)==["strict","sanitized"]
                and len(commands)==4 and all(c["exit"]==0 for c in commands))
        record={
           "kind":"R03P1ActualBoundedGeneralLiveSourceHost",
           "result":"PassedWithinInactiveSourceHostScope" if passed else "FailedOrIncomplete",
           "sourceMethodSha256":sha(actual.encode()),
           "compiledExactProductionSource":compiled,
           "compiledActualBoundedSourcesAndSerializer":compiled,
           "sourcePrepared":True,
           "commandLifetimeUnverified":lifetimeUnknown,
           "descendantQuiescenceProven":False,
           "postflightError":postflightError,
           "caseGroups":profiles,
           "commands":commands,
           "sourceBefore":before,"sourceAfter":after,
           "observedSourceHashesEqual":postflightError is None and before==after,
           "sourcePreserved":preserved,
           "firstError":firstError,
           "realEngineMetadataGate":False,"coherentVmEpochProven":False,
           "nativeClassPointerOriginProven":False,
           "realShutdownDeadlockFreedomProven":False,
           "fullRuntimeAllocationGcRssProven":False,
           "originalLiveGettersReplaced":False,
           "officialUnitySdkCompiled":False,"playerExecuted":False,
           "runtimeAcceptance":False
        }
        (output/"RESULT.json").write_text(json.dumps(record,indent=2)+"\n")
        manifest=[{"path":str(p.relative_to(output)),
                   "bytes":p.stat().st_size,"sha256":sha(p.read_bytes())}
                  for p in sorted(output.rglob("*"))
                  if p.is_file() and not p.is_symlink()]
        (output/"MANIFEST.json").write_text(json.dumps({"files":manifest,
            "inventoryScope":"UnsealedObservedFiles" if lifetimeUnknown else "CompletedRunnerFiles",
            "commandLifetimeUnverified":lifetimeUnknown},indent=2)+"\n")
    print(json.dumps({"result":record["result"],
                     "profiles":list(profiles),
                     "groupsPerProfile":len(GROUPS)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
