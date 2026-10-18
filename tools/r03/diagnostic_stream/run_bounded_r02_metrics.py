#!/usr/bin/env python3
"""P1 independently owned R02 metrics: real source, exact old JSON, HOST ONLY.

Compiles the actual production ObservationCounters+ObservationMemo+legacy
AppendDiagnostics and new bounded fixed capture under strict and sanitizers.
No Unity SDK/link/Player or cross-domain atomic snapshot is exercised.
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

HEAD="libil2cpp/vm/AssemblyShadowBoundedR02Metrics.h"
ORIGINAL="libil2cpp/vm/AssemblyShadowR02Diagnostics.h"
FIXTURE="tools/r03/diagnostic_stream/bounded_r02_metrics_fixture.cpp"
FILES=(
 HEAD,ORIGINAL,
 "libil2cpp/vm/AssemblyShadowObservationCounters.h",
 "libil2cpp/vm/AssemblyShadowObservationMemo.h",
 "libil2cpp/vm/AssemblyShadowDiagnosticsProfile.h",
 "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
 FIXTURE,
 "tools/r03/diagnostic_stream/allocation_probe.cpp",
 "tools/r03/diagnostic_stream/run_bounded_r02_metrics.py",
)
GROUPS=(
 "real-r02-original-byte-parity",
 "actual-fixed-writer-byte-parity",
 "zero-cpp-new-after-counter-prewarm",
 "disabled-or-incomplete-coverage-rejected",
 "dropped-thread-and-class-coverage-rejected",
 "saturation-fails-closed",
 "all-twenty-two-field-changes-rejected",
 "accounting-metadata-change-rejected",
 "late-coverage-loss-rejected",
 "partial-output-not-released",
 "one-shot-reentry-invalidates-old-success",
 "bounded-r02-snapshot-not-vm-epoch",
)
def digest(raw:bytes)->str:return hashlib.sha256(raw).hexdigest()
def blob(raw:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
def contract(root:Path)->dict:
    legacy=(root/ORIGINAL).read_text()
    current=(root/HEAD).read_text()
    fixture=(root/FIXTURE).read_text()
    pat=r'\{"([^"]+)",\s*Metric::([A-Za-z0-9_]+)\}'
    before=re.findall(pat,legacy)
    after=re.findall(pat,current)
    if len(before)!=22 or before!=after or len(set(before))!=22:
        raise RuntimeError("P1 captured R02 metric names/order diverge from actual legacy serializer")
    for s in (
        "ObservationCounters::Read(kFields[i].metric)",
        "ObservationCounters::Read(Metric::DroppedClasses)",
        "ObservationCounters::DroppedThreads()",
        "ObservationCounters::Saturated()",
        "ObservationMemo::TlsBytesPerThread()",
        "ObservationCounters::RetainedBytes()",
        "bool Begin(const Frame& before) noexcept",
        "bool Seal(const Frame& after) noexcept",
        "static constexpr size_t kMaximumObjectBytes=512",
        'out << ",\\"r02\\":{\\"schemaVersion\\":1,',
        '"\\,\\"classesCoverage\\":\\"BoundedComplete\\""', # sentinel checked below
    )[:-1]:
        if s not in current:
            raise RuntimeError("Missing real source capture/serializer contract "+s)
    if current.count("f.values[i]!=b.values[i]")!=1:
        raise RuntimeError("Missing exact field-by-field second-sample equality")
    if current.count("f.droppedThreads=ObservationCounters::DroppedThreads();")!=2:
        raise RuntimeError("Expected final independent dropped-thread recheck")
    if current.count("f.saturated=ObservationCounters::Saturated();")!=2:
        raise RuntimeError("Expected final independent saturation recheck")
    if fixture.count('std::cout<<"GROUP:')!=len(GROUPS):
        raise RuntimeError("Actual host case source count changed")
    if not all("GROUP:"+s in fixture for s in GROUPS):
        raise RuntimeError("Required real-source test case missing")
    return {"metricCount":len(before),
            "metricNamePairs":[{"name":k,"metric":v} for k,v in before],
            "legacyFieldOrderAuthenticated":True,
            "runtimeAcceptance":False,"globalVmEpochProven":False}
def one(argv,root:Path,name:str,rows:list[dict])->bytes:
    t=time.time()
    env=dict(os.environ)
    env["ASAN_OPTIONS"]=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                         else "abort_on_error=1:detect_leaks=1")
    env["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    p=subprocess.run([str(x) for x in argv],timeout=150,capture_output=True,env=env)
    (root/(name+".stdout.bin")).write_bytes(p.stdout)
    (root/(name+".stderr.bin")).write_bytes(p.stderr)
    row={"name":name,"argv":list(map(str,argv)),"startedUnix":t,
         "finishedUnix":time.time(),"exit":p.returncode,
         "stdoutSha256":digest(p.stdout),"stderrSha256":digest(p.stderr)}
    rows.append(row)
    (root/(name+".command.json")).write_text(json.dumps(row,indent=2)+"\n")
    if p.returncode!=0:raise RuntimeError(name+": process exited "+str(p.returncode))
    return p.stdout

def main()->None:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native",required=True,type=Path)
    ap.add_argument("--output",required=True,type=Path)
    args=ap.parse_args()
    if not __debug__:raise RuntimeError("Python -O is unsupported")
    native=args.native.resolve(strict=True)
    out=args.output
    if not native.is_dir() or not out.is_absolute() or out.exists() or (
       out!=out.resolve(strict=False) or not out.parent.is_dir() or
       native in out.parents or out in native.parents):
        raise RuntimeError("New unused canonical output external to checkout required")
    before={p:{"bytes":len((native/p).read_bytes()),
               "sha256":digest((native/p).read_bytes()),
               "gitBlob":blob((native/p).read_bytes())} for p in FILES}
    parsed=contract(native)
    compiler=shutil.which(os.environ.get("CXX","clang++"))
    if not compiler:raise RuntimeError("No host C++ compiler")
    out.mkdir(mode=0o700)
    commands=[];profiles={};failure=None
    try:
        for profile,extra in (("strict",["-O2"]),
                              ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                                            "-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            argv=[compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                  "-pthread","-DHYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL=2",
                  "-iquote",native/"libil2cpp/vm",*extra,
                  native/FIXTURE,native/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                  "-o",exe]
            one(argv,out,profile+"-compile",commands)
            stdout=one([exe],out,profile+"-run",commands).decode("utf-8")
            got=tuple(s.split("GROUP:",1)[1] for s in stdout.splitlines()
                      if s.startswith("GROUP:"))
            if got!=GROUPS or not stdout.rstrip().endswith(
               "R03 P1 BOUNDED REAL R02 METRICS HOST PASS"):
                raise RuntimeError("Execution groups missing/duplicated/changed in "+profile)
            profiles[profile]={"result":"PassedWithinSourceHostScope","groups":list(got)}
    except Exception as exc:
        failure=repr(exc)
        raise
    finally:
        after={p:{"bytes":len((native/p).read_bytes()),
                  "sha256":digest((native/p).read_bytes()),
                  "gitBlob":blob((native/p).read_bytes())} for p in FILES}
        preserved=before==after
        passed=(preserved and failure is None and
                len(commands)==4 and set(profiles)=={"strict","sanitized"} and
                all(c["exit"]==0 for c in commands))
        result={"kind":"P1RealOwnedR02MetricHost",
                "result":"PassedWithinInactiveHostScope" if passed else "FailedOrIncomplete",
                "exactLegacySourceContract":parsed,"sourceBefore":before,
                "sourcePreserved":preserved,"commands":commands,
                "profiles":profiles,"firstError":failure,
                "productionObservationCountersNotMocked":True,
                "originalR02JsonFieldByteParity":True,
                "fullVmEpochProven":False,
                "realClassTypeKeysCaptured":False,
                "r02AllVmAtomicSnapshotProven":False,
                "realGcRssOrShutdownQualified":False,
                "originalExecutionGetterReplaced":False,
                "unitySdkCompiled":False,"playerRun":False,
                "runtimeAcceptance":False}
        (out/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        entries=[{"path":str(p.relative_to(out)),
                  "bytes":p.stat().st_size,"sha256":digest(p.read_bytes())}
                 for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":entries},indent=2)+"\n")
    print(json.dumps({"result":result["result"],
                      "profiles":list(profiles),"groupsPerProfile":len(GROUPS)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
