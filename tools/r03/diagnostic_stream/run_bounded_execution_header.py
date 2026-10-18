#!/usr/bin/env python3
"""R03 P1 exact inactive execution scalar source; host source scope ONLY.

Compile the REAL .inl source method against actual Capture and original
execution serializer definitions, with explicit VM/atomic test doubles.
Not official Unity SDK, Player, GC/RSS, class metadata or a VM-wide epoch.
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

SOURCE="libil2cpp/vm/AssemblyShadowBoundedExecutionHeaderSource.inl"
FIXTURE="tools/r03/diagnostic_stream/bounded_execution_header_fixture.cpp"
MARKER="// __EXACT_PRODUCTION_EXECUTION_SCALAR_SOURCE__"
FILES=(
 SOURCE,
 "libil2cpp/vm/AssemblyShadow.cpp",
 "libil2cpp/vm/AssemblyShadow.h",
 "libil2cpp/vm/AssemblyShadowBoundedExecutionHeader.h",
 "libil2cpp/vm/AssemblyShadowExecutionSerializer.h",
 "libil2cpp/vm/AssemblyShadowMetadataLifetime.h",
 "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
 "libil2cpp/vm/AssemblyShadowDiagnostics.h",
 "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
 "libil2cpp/vm/AssemblyShadowTypes.h",
 FIXTURE,
 "tools/r03/diagnostic_stream/allocation_probe.cpp",
 "tools/r03/diagnostic_stream/run_bounded_execution_header.py",
)
GROUPS=(
 "exact-legacy-execution-header-byte-parity",
 "no-new-allocations-after-prewarm",
 "missing-state-source-denied",
 "invalid-state-code-denied",
 "impossible-subset-total-refused",
 "saturated-counter-coverage-refused",
 "dropped-counter-thread-refused",
 "method-counter-changed-during-sampling",
 "state-flip-within-second-sample",
 "active-snapshot-rebound-same-generation",
 "single-owner-no-reentry",
 "state-name-enum-owned-not-borrowed",
 "closed-gate-no-source-admission",
)
REQUIRED=(
 "assembly_shadow_metadata_lifetime::Gate::Reader reader(",
 "if(!reader)return capture.RejectOwnerUnavailable();",
 "s_shadowMethodChecks.load(std::memory_order_acquire)",
 "s_rejectedBaselineMethods.load(std::memory_order_acquire)",
 "s_methodChecks.load(std::memory_order_relaxed)",
 "s_shadowInterpreterTransformations.load(std::memory_order_acquire)",
 "s_interpreterTransformations.load(std::memory_order_relaxed)",
 "r02::ObservationCounters::DroppedThreads()",
 "r02::ObservationCounters::Saturated()",
 "activeBefore==activeAfter",
 "before.stateCode==static_cast<int32_t>(stateBefore)",
 "after.stateCode==static_cast<int32_t>(stateAfter)",
 "capture.Seal(after,droppedAfter,saturatedAfter,stableOwners)",
)
def digest(raw:bytes)->str:return hashlib.sha256(raw).hexdigest()
def gitblob(raw:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
def check(repo:Path):
    source=(repo/SOURCE).read_text()
    owner=(repo/"libil2cpp/vm/AssemblyShadow.cpp").read_text()
    declaration=(repo/"libil2cpp/vm/AssemblyShadow.h").read_text()
    fixture=(repo/FIXTURE).read_text()
    if owner.count('#include "AssemblyShadowBoundedExecutionHeaderSource.inl"')!=1:
        raise ValueError("Production owner no longer includes exact scalar source once")
    if owner.count('#include "AssemblyShadowBoundedExecutionHeader.h"')!=1:
        raise ValueError("Production owner missing actual bounded execution header")
    if "static bool TryCaptureBoundedExecutionHeader(" not in declaration:
        raise ValueError("Original native class declaration is missing")
    if source.count("bool AssemblyShadow::TryCaptureBoundedExecutionHeader(")!=1:
        raise ValueError("Ambiguous or missing exact production method")
    for needle in REQUIRED:
        expected=2 if needle in (
          "r02::ObservationCounters::DroppedThreads()",
          "r02::ObservationCounters::Saturated()") else 1
        if source.count(needle)!=expected:
            raise ValueError("Production scalar acquisition ordering changed: "+needle)
    if fixture.count(MARKER)!=1:
        raise ValueError("Expected one marked real-source replacement slot")
    if not all("GROUP:"+name in fixture for name in GROUPS):
        raise ValueError("Missing required actual host fixture group")
    if fixture.count('std::cout<<"GROUP:')!=len(GROUPS):
        raise ValueError("Unexpected number of source-bound test labels")
    return source, fixture.replace(MARKER,source)

def one(argv,output:Path,name:str,rows:list)->bytes:
    before=time.time()
    env=os.environ.copy()
    env["ASAN_OPTIONS"]=("abort_on_error=1:detect_leaks=0"
                         if sys.platform=="darwin" else "abort_on_error=1:detect_leaks=1")
    env["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    p=subprocess.run([str(x) for x in argv],timeout=160,capture_output=True,env=env)
    (output/(name+".stdout.bin")).write_bytes(p.stdout)
    (output/(name+".stderr.bin")).write_bytes(p.stderr)
    receipt={"name":name,"argv":list(map(str,argv)),
         "startedUnix":before,"finishedUnix":time.time(),
         "exit":p.returncode,"stdoutSha256":digest(p.stdout),
         "stderrSha256":digest(p.stderr)}
    rows.append(receipt)
    (output/(name+".command.json")).write_text(json.dumps(receipt,indent=2)+"\n")
    if p.returncode:raise RuntimeError(name+": C++ exit "+str(p.returncode))
    return p.stdout

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native",type=Path,required=True)
    ap.add_argument("--output",type=Path,required=True)
    args=ap.parse_args()
    if not __debug__:raise RuntimeError("Optimized Python unsupported")
    native=args.native.resolve(strict=True)
    out=args.output
    if (not out.is_absolute() or out!=out.resolve(strict=False) or
        out.exists() or not out.parent.is_dir() or
        native==out or out in native.parents or native in out.parents):
        raise RuntimeError("New unused, disjoint canonical evidence directory required")
    sourceInput={name:{"bytes":len((native/name).read_bytes()),
              "sha256":digest((native/name).read_bytes()),
              "gitBlob":gitblob((native/name).read_bytes())} for name in FILES}
    exact,fixture=check(native)
    out.mkdir(mode=0o700)
    (out/"stubs").mkdir()
    (out/"stubs/il2cpp-config.h").write_text("#pragma once\n// host only\n")
    (out/"actual-execution-scalar-source.inl").write_text(exact)
    (out/"compiled-execution-header.cpp").write_text(fixture)
    compiler=shutil.which(os.environ.get("CXX","clang++"))
    if not compiler:raise RuntimeError("Requested host C++ compiler unavailable")
    commands=[]
    profiles={}
    error=None
    try:
        for profile,flags in (
            ("strict",["-O2"]),
            ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                          "-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            argv=[compiler,"-std=c++17","-Wall","-Wextra","-Werror",
                  "-pedantic","-pthread",
                  "-iquote",native/"libil2cpp/vm","-iquote",out/"stubs",
                  *flags,out/"compiled-execution-header.cpp",
                  native/"libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
                  native/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                  "-o",exe]
            one(argv,out,profile+"-compile",commands)
            raw=one([exe],out,profile+"-run",commands).decode("utf-8")
            seen=tuple(x.split("GROUP:",1)[1] for x in raw.splitlines()
                      if x.startswith("GROUP:"))
            if seen!=GROUPS or not raw.rstrip().endswith(
                    "P1 INACTIVE EXECUTION HEADER SOURCE HOST PASS"):
                raise RuntimeError("Missing, changed or reordered actual execution header groups")
            profiles[profile]={"result":"PassedWithinSourceHostScope","groups":list(seen)}
    except Exception as exc:
        error=repr(exc)
        raise
    finally:
        preserved=sourceInput=={name:{
            "bytes":len((native/name).read_bytes()),
            "sha256":digest((native/name).read_bytes()),
            "gitBlob":gitblob((native/name).read_bytes())} for name in FILES}
        passed=error is None and preserved and len(commands)==4 and all(
               x["exit"]==0 for x in commands) and set(profiles)=={"strict","sanitized"}
        record={
          "kind":"R03P1ExecutionScalarExactSourceHost",
          "result":"PassedWithinInactiveHostScope" if passed else "FailedOrIncomplete",
          "sourceMethodSha256":digest(exact.encode()),
          "sourceBefore":sourceInput,"sourcePreserved":preserved,
          "cases":profiles,"commands":commands,"firstError":error,
          "exactNativeSourceMethodCompiled":True,
          "realBoundedCaptureAndLegacyExecutionSerializerCompiled":True,
          "nativeVmWritersMocked":True,
          "realR02MetricSetCovered":False,
          "realExecutionClassesOrTypeKeysCovered":False,
          "coherentVmEpochProven":False,
          "officialUnitySdkCompile":False,
          "nativeLinkOrPlayerRun":False,
          "runtimeAcceptance":False}
        (out/"RESULT.json").write_text(json.dumps(record,indent=2)+"\n")
        manifest=[{"path":str(p.relative_to(out)),
                   "bytes":p.stat().st_size,"sha256":digest(p.read_bytes())}
                 for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":manifest},indent=2)+"\n")
    print(json.dumps({"result":record["result"],"profiles":list(profiles),
                      "groupsPerProfile":len(GROUPS)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
