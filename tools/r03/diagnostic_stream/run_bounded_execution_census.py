#!/usr/bin/env python3
"""R03 P1 source-bound identity-only execution-class census, HOST ONLY.

Compiles the EXACT production AssemblyShadowBoundedExecutionCensusSource.inl
with actual owned Capture and actual metadata lifetime Gate under explicit VM
table/counter mocks. No Il2CppClass is dereferenced, copied or published.
No official SDK, native link, real VM shutdown, epoch, Player or R03 acceptance.
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

METHOD="libil2cpp/vm/AssemblyShadowBoundedExecutionCensusSource.inl"
FIXTURE="tools/r03/diagnostic_stream/bounded_execution_census_fixture.cpp"
MARKER="// __EXACT_BOUND_EXECUTION_CENSUS_SOURCE__"
FILES=(
    METHOD,
    "libil2cpp/vm/AssemblyShadow.cpp",
    "libil2cpp/vm/AssemblyShadow.h",
    "libil2cpp/vm/AssemblyShadowBoundedExecutionCensus.h",
    "libil2cpp/vm/AssemblyShadowMetadataLifetime.h",
    FIXTURE,
    "tools/r03/diagnostic_stream/allocation_probe.cpp",
    "tools/r03/diagnostic_stream/run_bounded_execution_census.py",
)
GROUPS=(
    "real-registry-two-observed-no-class-dereference",
    "zero-new-after-execution-census-prewarm",
    "empty-execution-registry",
    "observed-slot-count-mismatch-refused",
    "declared-class-limit-refused",
    "concurrent-insert-source-change-refused",
    "rebound-identity-same-count-refused",
    "late-dropped-observation-refused",
    "dropped-thread-and-saturation-refused",
    "partial-diagnostics-level-refused",
    "reentry-invalidates-previous-census",
    "initial-metadata-admission-closed",
)
REQUIRED=(
    "assembly_shadow_metadata_lifetime::Gate::Reader reader(",
    "if(!reader)return capture.RejectOwnerUnavailable();",
    "ExecutionObservationLock lock;",
    "result.declaredCount=s_executionClassCount;",
    "result.tableCapacity=s_executionClasses.size();",
    "const Il2CppClass* klass=s_executionClasses[index];",
    "reinterpret_cast<uintptr_t>(klass)",
    "result.droppedObservations=s_droppedClassObservations.load(",
    "result.droppedCounterThreads=r02::ObservationCounters::DroppedThreads();",
    "result.countersSaturated=r02::ObservationCounters::Saturated();",
    "if(!capture.Begin(before))return false;",
    "return capture.Seal(after);",
)
def digest(raw:bytes)->str:return hashlib.sha256(raw).hexdigest()
def blob(raw:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

def contract(repo:Path)->tuple[str,str]:
    code=(repo/METHOD).read_text()
    host=(repo/FIXTURE).read_text()
    owner=(repo/"libil2cpp/vm/AssemblyShadow.cpp").read_text()
    header=(repo/"libil2cpp/vm/AssemblyShadow.h").read_text()
    capture=(repo/"libil2cpp/vm/AssemblyShadowBoundedExecutionCensus.h").read_text()
    if owner.count('#include "AssemblyShadowBoundedExecutionCensusSource.inl"')!=1:
        raise RuntimeError("Wrong real production method include site or count")
    if owner.count('#include "AssemblyShadowBoundedExecutionCensus.h"')!=1:
        raise RuntimeError("Real production Capture not included in owner")
    if header.count('static bool TryCaptureBoundedExecutionCensus(')!=1:
        raise RuntimeError("Production class has no uniquely declared method")
    if code.count("bool AssemblyShadow::TryCaptureBoundedExecutionCensus(")!=1:
        raise RuntimeError("Missing actual VM method body")
    if any(code.count(s)!=1 for s in REQUIRED):
        raise RuntimeError("Actual VM source/registry/admission contract changed")
    if ("klass->" in code or "MetadataCache::" in code or
        "g_MetadataLock" in code or "TypeKey" in code or "new " in code):
        raise RuntimeError("Opaque class identity path began dereferencing/allocating")
    if capture.count("const char* Data()") or capture.count("uintptr_t class"):
        raise RuntimeError("Capture began exporting raw source identities")
    if host.count(MARKER)!=1 or host.count('std::cout<<"GROUP:')!=len(GROUPS):
        raise RuntimeError("Actual executable host-case count/marker changed")
    if not all("GROUP:"+s in host for s in GROUPS):
        raise RuntimeError("Required source-change/coverage negative test omitted")
    return code,host.replace(MARKER,code)

def call(argv,output:Path,label:str,records:list)->bytes:
    began=time.time()
    env=dict(os.environ)
    env["ASAN_OPTIONS"]=("abort_on_error=1:detect_leaks=0"
                         if sys.platform=="darwin" else "abort_on_error=1:detect_leaks=1")
    env["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    p=subprocess.run([str(v) for v in argv],capture_output=True,timeout=180,env=env)
    (output/(label+".stdout.bin")).write_bytes(p.stdout)
    (output/(label+".stderr.bin")).write_bytes(p.stderr)
    result={"label":label,"argv":[str(v) for v in argv],
            "startedUnix":began,"finishedUnix":time.time(),
            "exit":p.returncode,"stdoutSha256":digest(p.stdout),
            "stderrSha256":digest(p.stderr)}
    (output/(label+".command.json")).write_text(json.dumps(result,indent=2)+"\n")
    records.append(result)
    if p.returncode!=0:
        raise RuntimeError(f"{label}: process exited {p.returncode}; retain stderr receipt")
    return p.stdout

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native",type=Path,required=True)
    ap.add_argument("--output",type=Path,required=True)
    a=ap.parse_args()
    if not __debug__:raise RuntimeError("Python optimization/disabled assertions forbidden")
    repo=a.native.resolve(strict=True)
    out=a.output
    if(not repo.is_dir() or not out.is_absolute() or
       out!=out.resolve(strict=False) or out.exists() or
       not out.parent.is_dir() or repo in out.parents or out in repo.parents):
        raise RuntimeError("Unused canonical external evidence directory required")
    source={p:{"bytes":len((repo/p).read_bytes()),
               "sha256":digest((repo/p).read_bytes()),
               "gitBlob":blob((repo/p).read_bytes())} for p in FILES}
    method,fixture=contract(repo)
    compiler=shutil.which(os.environ.get("CXX","clang++"))
    if not compiler:raise RuntimeError("No host compiler")
    out.mkdir(mode=0o700)
    (out/"compiled-exact-source.cpp").write_text(fixture)
    (out/"exact-method.inl").write_text(method)
    commands=[]
    profiles={}
    failure=None
    try:
        for profile,flags in (
            ("strict",["-O2"]),("sanitized",["-O1","-g",
                "-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            argv=[compiler,"-std=c++17","-Wall","-Wextra","-Werror",
                  "-pedantic","-pthread","-iquote",repo/"libil2cpp/vm",
                  *flags,out/"compiled-exact-source.cpp",
                  repo/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                  "-o",exe]
            call(argv,out,profile+"-compile",commands)
            txt=call([exe],out,profile+"-run",commands).decode("utf-8")
            labels=tuple(z.split("GROUP:",1)[1] for z in txt.splitlines()
                         if z.startswith("GROUP:"))
            if labels!=GROUPS or not txt.rstrip().endswith(
               "P1 R03 EXECUTION IDENTITY-ONLY CENSUS HOST PASS"):
                raise RuntimeError("Actual test result labels differ "+profile)
            profiles[profile]={"result":"PassedWithinHostScope","groups":list(labels)}
    except Exception as exc:
        failure=repr(exc)
        raise
    finally:
        actual={p:{"bytes":len((repo/p).read_bytes()),
                   "sha256":digest((repo/p).read_bytes()),
                   "gitBlob":blob((repo/p).read_bytes())} for p in FILES}
        preserved=actual==source
        passed=(failure is None and preserved and set(profiles)=={"strict","sanitized"}
                and len(commands)==4 and all(row["exit"]==0 for row in commands))
        result={
            "kind":"R03P1ExecutionIdentityOnlyCensusHost",
            "result":"PassedWithinInactiveHostScope" if passed else "FailedOrIncomplete",
            "sourceMethodSha256":digest(method.encode()),
            "sourceBefore":source,"sourcePreserved":preserved,
            "actualProductionMethodAndCaptureCompiled":True,
            "profiles":profiles,"commands":commands,"firstError":failure,
            "sourceClassPointerDereferenced":False,
            "sourceClassPointerOrHashExported":False,
            "observedRegistryTableSlots":2048,
            "maximumObservedClasses":1024,
            "classOriginAndLifetimeProven":False,
            "allVmEpochProven":False,
            "completeExecutionClassRowsOrTypeKeys":False,
            "officialUnitySdkCompiler":False,
            "realPlayerOrRuntime":False,
            "runtimeAcceptance":False
        }
        (out/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        files=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                "sha256":digest(p.read_bytes())}
               for p in sorted(out.rglob("*"))
               if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":files},indent=2)+"\n")
    print(json.dumps({"result":result["result"],
                      "profiles":list(profiles),"groupCount":len(GROUPS)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
