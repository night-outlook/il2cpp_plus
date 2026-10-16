#!/usr/bin/env python3
"""P1 source-bound owned general V1 composition, host only (no Unity/Player).

Compiles actual production headers and legacy Serialize body with explicit
already-owned captures. Does NOT prove a global live VM epoch, class pointer
provenance, a native scheduler, SDK linking or runtime acceptance.
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

FILES=(
 "libil2cpp/vm/AssemblyShadowBoundedGeneralComposer.h",
 "libil2cpp/vm/AssemblyShadowFrozenGeneralCapture.h",
 "libil2cpp/vm/AssemblyShadowBoundedTransactionInventory.h",
 "libil2cpp/vm/AssemblyShadowBoundedTransactionStatus.h",
 "libil2cpp/vm/AssemblyShadowBoundedFirstUseCapture.h",
 "libil2cpp/vm/AssemblyShadowBoundedPhysicalAssemblies.h",
 "libil2cpp/vm/AssemblyShadowBoundedOrdinaryClasses.h",
 "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
 "libil2cpp/vm/AssemblyShadowDiagnostics.h",
 "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
 "libil2cpp/vm/AssemblyShadowTypes.h",
 "tools/r03/diagnostic_stream/bounded_general_composer_fixture.cpp",
 "tools/r03/diagnostic_stream/allocation_probe.cpp",
 "tools/r03/diagnostic_stream/run_bounded_general_composer.py",
)
EXPECTED_CASES=(
 "byte-exact-general-owned-parity",
 "composition-no-new-allocation",
 "missing-source-fail-closed",
 "incomplete-source-fail-closed",
 "cross-reader-generation-mismatch",
 "startup-source-bounds",
 "invalid-borrowed-startup-refusal",
 "complete-output-budget-refusal",
 "invalid-utf8-refusal",
 "single-owner-single-freeze",
 "no-coherent-vm-epoch-or-player-claim",
)
def sha(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def source_contract(repo:Path)->dict:
    code=(repo/FILES[0]).read_text()
    frozen=(repo/FILES[1]).read_text()
    tests=(repo/FILES[11]).read_text()
    required=(
      "inline bool Compose(const Inputs& input,frozen::Capture& output,Receipt& receipt) noexcept",
      "transaction.Complete()", "uses.Complete()", "assemblies.Complete()",
      "typeRows.Complete()", "assemblies.EnumerationGeneration()!=typeRows.ClassEnumerationGeneration()",
      "output.AddClosureName", "output.AddStableAotName",
      "output.AddCommitOrderName", "output.AddAssembly", "output.AddEvent",
      "output.AddBaselineUse", "output.EndBaselineUses",
      "output.AddOrdinaryAssembly", "output.AddOrdinaryClass",
      "if(emittedUses!=uses.PresentCount())", "output.Freeze()",
      "destination->RejectInvalidSource()",
      "bool coherentVmEpochProven=false;",
      "bool runtimeAcceptance=false;",
    )
    if not all(code.count(x)==1 for x in required):
        raise RuntimeError("Production composer source field/stage/denial contract changed")
    if "bool RejectInvalidSource() noexcept" not in frozen:
        raise RuntimeError("A late source failure can leave a completed frozen record")
    if tests.count('std::cout<<"GROUP:')!=len(EXPECTED_CASES):
        raise RuntimeError("Actual production fixture changed case count")
    for name in EXPECTED_CASES:
        if 'GROUP:'+name not in tests:
            raise RuntimeError("Missing actual fixture group "+name)
    # Strip comments to avoid confusing commentary about locks with operations.
    body="\n".join(line.split("//",1)[0] for line in code.splitlines())
    for token in ("UsageLock lock", "g_MetadataLock", "Thread::", "std::vector<",
                  "std::string ", "std::mutex", "fopen(", "std::fstream"):
        if token in body:
            raise RuntimeError("Composer gained new VM/heap/I/O source operation "+token)
    return {"requiredProductionFragments":len(required),
            "caseNames":list(EXPECTED_CASES),
            "onlyOwnedCompletedSourcesRead":True,
            "noVmOrFileOperationsInComposer":True,
            "crossEnumerationGenerationMismatchRejected":True,
            "globalVmEpochProven":False}

def one(argv,output:Path,name:str,records:list[dict])->bytes:
    started=time.time()
    env=dict(os.environ)
    env["ASAN_OPTIONS"]=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                         else "abort_on_error=1:detect_leaks=1")
    env["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    proc=subprocess.run([str(a) for a in argv],capture_output=True,timeout=150,env=env)
    (output/(name+".stdout.bin")).write_bytes(proc.stdout)
    (output/(name+".stderr.bin")).write_bytes(proc.stderr)
    record=dict(label=name,argv=list(map(str,argv)),startUnix=started,
                finishUnix=time.time(),exit=proc.returncode,
                stdoutSha256=sha(proc.stdout),stderrSha256=sha(proc.stderr))
    records.append(record)
    (output/(name+".command.json")).write_text(json.dumps(record,indent=2)+"\n")
    if proc.returncode:
        raise RuntimeError(f"{name}: exit={proc.returncode}; preserve raw logs")
    return proc.stdout

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Python optimization forbidden")
    native=a.native.resolve(strict=True)
    output=a.output
    if(not native.is_dir() or not output.is_absolute() or
       output!=output.resolve(strict=False) or output.exists() or
       not output.parent.is_dir() or
       native==output or native in output.parents or output in native.parents):
        raise RuntimeError("Unused canonical external evidence root required")
    original={}
    for name in FILES:
        data=(native/name).read_bytes()
        original[name]={"bytes":len(data),"sha256":sha(data),"gitBlob":blob(data)}
    audit=source_contract(native)
    output.mkdir(mode=0o700)
    (output/"stubs").mkdir()
    (output/"stubs/il2cpp-config.h").write_text(
        "#pragma once\n// Explicit HOST fixture; no Unity SDK metadata.\n")
    cxx=shutil.which(os.environ.get("CXX","clang++"))
    if not cxx:raise RuntimeError("No host C++ compiler")
    commands=[]; profiles={}; failure=None
    try:
        for profile,extra in (
            ("strict",["-O2"]),
            ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                          "-fsanitize=address,undefined"])
        ):
            exe=output/(profile+".exe")
            cmd=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                 "-pthread","-iquote",native/"libil2cpp/vm",
                 "-iquote",output/"stubs",*extra,
                 native/FILES[11],native/FILES[12],native/FILES[9],
                 "-o",exe]
            one(cmd,output,profile+"-compile",commands)
            text=one([exe],output,profile+"-run",commands).decode("utf-8")
            lines=[line.split("GROUP:",1)[1] for line in text.splitlines()
                   if line.startswith("GROUP:")]
            if tuple(lines)!=EXPECTED_CASES:
                raise RuntimeError("Unexpected case list or missing exact test group "+profile)
            counts=[line for line in text.splitlines()
                    if line.startswith("ALLOCATIONS_AFTER_PREWARM:")]
            if counts!=["ALLOCATIONS_AFTER_PREWARM:0"]:
                raise RuntimeError("Captured output after prewarm allocated new heap memory")
            if not text.rstrip().endswith("P1 BOUNDED GENERAL COMPOSER HOST PASS"):
                raise RuntimeError("Test case main did not reach complete PASS")
            profiles[profile]={"result":"PassedWithinHostScope","cases":lines,
                               "allocationsAfterPrewarm":0}
    except Exception as exc:
        failure=repr(exc)
        raise
    finally:
        unchanged=original=={name:{
            "bytes":len((native/name).read_bytes()),
            "sha256":sha((native/name).read_bytes()),
            "gitBlob":blob((native/name).read_bytes())} for name in FILES}
        passed=unchanged and list(profiles)==["strict","sanitized"] and len(commands)==4
        result=dict(kind="P1OwnedGeneralComposerActualHeadersHost",
            result="PassedWithinOwnedHostScope" if passed else "FailedOrIncomplete",
            profiles=profiles,sourceContract=audit,sourceBefore=original,
            sourcePreserved=unchanged,commands=commands,firstError=failure,
            actualProductionHeadersCompiled=True,
            legacySerializerSameSourceCompiled=True,
            allVmEpochProven=False,realClassPointerProven=False,
            originalLiveGettersReplaced=False,vmAllocationGcRssProven=False,
            officialUnitySdkCompiled=False,realPlayerRun=False,
            runtimeAcceptance=False)
        (output/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        entries=[{"path":str(p.relative_to(output)),"bytes":p.stat().st_size,
                  "sha256":sha(p.read_bytes())}
                 for p in sorted(output.rglob("*")) if p.is_file() and not p.is_symlink()]
        (output/"MANIFEST.json").write_text(json.dumps({"files":entries},indent=2)+"\n")
    print(json.dumps({"result":result["result"],
         "profiles":list(profiles),"caseGroupsPerProfile":len(EXPECTED_CASES)},indent=2))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
