#!/usr/bin/env python3
"""P1 classless execution JSON parity from OWNED scalar+real R02 captures.

This intentionally emits no class rows, does not replace the original getter,
has no live class/type key/R02 epoch, native SDK or Player runtime acceptance.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys, time
from pathlib import Path

FILES=(
 "libil2cpp/vm/AssemblyShadowBoundedExecutionComposite.h",
 "libil2cpp/vm/AssemblyShadowBoundedExecutionHeader.h",
 "libil2cpp/vm/AssemblyShadowBoundedR02Metrics.h",
 "libil2cpp/vm/AssemblyShadowExecutionSerializer.h",
 "libil2cpp/vm/AssemblyShadowR02Diagnostics.h",
 "libil2cpp/vm/AssemblyShadowObservationCounters.h",
 "libil2cpp/vm/AssemblyShadowObservationMemo.h",
 "libil2cpp/vm/AssemblyShadowDiagnostics.h",
 "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
 "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
 "libil2cpp/vm/AssemblyShadowTypes.h",
 "libil2cpp/vm/AssemblyShadowDiagnosticsProfile.h",
 "tools/r03/diagnostic_stream/bounded_execution_composite_fixture.cpp",
 "tools/r03/diagnostic_stream/allocation_probe.cpp",
 "tools/r03/diagnostic_stream/run_bounded_execution_composite.py",
)
GROUPS=(
 "classless-original-execution-r02-byte-parity",
 "classless-composition-zero-new-allocations",
 "missing-execution-header-rejected",
 "missing-r02-metrics-rejected",
 "unsealed-header-rejected",
 "unsealed-r02-rejected",
 "invalid-classless-capacity-denied",
 "short-capacity-fails-closed-no-prefix",
 "classless-once-only-reentry-refused",
 "owned-metric-values-not-live-aliases",
 "classless-object-bound-not-full-execution",
 "no-vm-epoch-or-class-census-acceptance",
)
def digest(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def blob(b:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()
def contract(root:Path)->dict:
    text=(root/FILES[0]).read_text()
    fixture=(root/FILES[12]).read_text()
    keys=(
        'serial::Begin(output,h);',
        'serial::EndClasses(output);',
        'metrics->Append(output)',
        'output.Finish()',
        'maximumBytes==0 || maximumBytes>kClasslessMaxBytes',
        'if(!scalar->Complete() || !metrics->Complete())',
        'const char* Data() const noexcept {return Complete()?bytes_:nullptr;}',
        'static constexpr size_t kClasslessMaxBytes=32768',
    )
    if any(text.count(x)!=1 for x in keys):
        raise RuntimeError("Classless fixed output or partial-result contract changed")
    if fixture.count('std::cout<<"GROUP:')!=len(GROUPS) or not all(
        "GROUP:"+name in fixture for name in GROUPS):
        raise RuntimeError("Missing/duplicated actual positive or negative case")
    legacy=(root/"libil2cpp/vm/AssemblyShadowR02Diagnostics.h").read_text()
    current=(root/"libil2cpp/vm/AssemblyShadowBoundedR02Metrics.h").read_text()
    metrics=r'\{"([^"]+)",\s*Metric::([A-Za-z0-9_]+)\}'
    oldFields=re.findall(metrics,legacy)
    newFields=re.findall(metrics,current)
    if len(oldFields)!=22 or oldFields!=newFields:
        raise RuntimeError("Composed metric source drifted from actual original R02")
    return {"stageOrder":list(keys[:3]),
            "legacyR02FieldOrderAuthenticated":True,"metricFields":22,
            "realObservationCountersNotMocked":True,
            "classRowsCaptured":False,"runtimeAcceptance":False}
def one(argv:list,root:Path,name:str,commands:list)->bytes:
    start=time.time()
    env=dict(os.environ)
    env["ASAN_OPTIONS"]=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                         else "abort_on_error=1:detect_leaks=1")
    env["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    p=subprocess.run([str(v) for v in argv],capture_output=True,timeout=150,env=env)
    (root/(name+".stdout.bin")).write_bytes(p.stdout)
    (root/(name+".stderr.bin")).write_bytes(p.stderr)
    r={"name":name,"argv":list(map(str,argv)),"start":start,"end":time.time(),
       "exit":p.returncode,"stdoutSha256":digest(p.stdout),
       "stderrSha256":digest(p.stderr)}
    (root/(name+".command.json")).write_text(json.dumps(r,indent=2)+"\n")
    commands.append(r)
    if p.returncode:raise RuntimeError(name+": child returned "+str(p.returncode))
    return p.stdout
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    args=p.parse_args()
    if not __debug__:raise RuntimeError("Python -O forbidden")
    native=args.native.resolve(strict=True);out=args.output
    if(not native.is_dir() or not out.is_absolute() or out.exists() or
       out!=out.resolve(strict=False) or not out.parent.is_dir() or
       out in native.parents or native in out.parents):
        raise RuntimeError("New unused canonical evidence directory required")
    before={path:{"bytes":len((native/path).read_bytes()),
                  "sha256":digest((native/path).read_bytes()),
                  "gitBlob":blob((native/path).read_bytes())} for path in FILES}
    contractSource=contract(native)
    cxx=shutil.which(os.environ.get("CXX","clang++"))
    if not cxx:raise RuntimeError("Host compiler unavailable")
    out.mkdir(mode=0o700)
    commands=[];profiles={};failure=None
    try:
        for profile,extra in (("strict",["-O2"]),("sanitized",[
            "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=out/(profile+".exe")
            cmd=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                 "-pthread","-DHYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL=2",
                 "-iquote",native/"libil2cpp/vm",*extra,
                 native/FILES[12],native/"libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
                 native/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                 "-o",exe]
            one(cmd,out,profile+"-compile",commands)
            output=one([exe],out,profile+"-run",commands).decode("utf-8")
            actual=tuple(x.split("GROUP:",1)[1] for x in output.splitlines()
                         if x.startswith("GROUP:"))
            if actual!=GROUPS or not output.rstrip().endswith(
                 "R03 P1 INACTIVE CLASSLESS EXECUTION COMPOSITE HOST PASS"):
                raise RuntimeError("Wrong executable groups "+profile)
            profiles[profile]={"result":"PassedWithinHostScope","groups":list(actual)}
    except Exception as exc:
        failure=repr(exc)
        raise
    finally:
        after={path:{"bytes":len((native/path).read_bytes()),
                     "sha256":digest((native/path).read_bytes()),
                     "gitBlob":blob((native/path).read_bytes())} for path in FILES}
        preserved=before==after
        passed=(failure is None and preserved and len(commands)==4 and
            all(x["exit"]==0 for x in commands) and set(profiles)=={"strict","sanitized"})
        result={"kind":"P1ClasslessOwnedExecutionCompositeHost",
            "result":"PassedWithinInactiveHostScope" if passed else "FailedOrIncomplete",
            "sourceContract":contractSource,"sourceBefore":before,
            "sourcePreserved":preserved,"commands":commands,"profiles":profiles,
            "firstError":failure,
            "fullExecutionJsonProven":False,"realClassRowsOrTypeKeysCovered":False,
            "coherentVmEpochProven":False,"sourceGcRssOrTeardownSafetyProven":False,
            "originalExecutionGetterReplaced":False,"officialUnitySdkCompiled":False,
            "unityEditorOrPlayerRun":False,"runtimeAcceptance":False}
        (out/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        manifest=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                   "sha256":digest(p.read_bytes())} for p in sorted(out.rglob("*"))
                   if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":manifest},indent=2)+"\n")
    print(json.dumps({"result":result["result"],"profiles":list(profiles),
                      "groupsPerProfile":len(GROUPS)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
