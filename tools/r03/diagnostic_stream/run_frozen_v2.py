#!/usr/bin/env python3
"""Host-only owned-execution snapshot -> actual native V2 memory wire parity.

Two fields are complete, with no heap allocations in the frozen source/sink
path. The VM census and actual Unity/Player native/C# emitters are not linked.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

REFERENCE_BLOB="0866b07f758a20284d2a5d5d933b2be577b20b50"
IDENTITY=dict(runId="p1-frozen-host",caseId="baseline",processId=1234,
              requestSha256="a"*64,buildSha256="b"*64,demoCommit="1"*40,
              hybridclrCommit="2"*40,packageCommit="3"*40,nativeCommit="4"*40)
INLINE=dict(reportTransport="NativeBoundedSnapshotV2",status="Failed",
            preReflection=42,preDelegate=42)

def sha(data:bytes)->str:
    return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()
def require(condition,message):
    if not condition:raise RuntimeError(message)
def run(argv:list[str], out:Path, name:str, commands:list[dict], env:dict):
    started=time.time()
    completed=subprocess.run([str(x) for x in argv],capture_output=True,timeout=150,env=env)
    (out/(name+".stdout")).write_bytes(completed.stdout)
    (out/(name+".stderr")).write_bytes(completed.stderr)
    record=dict(name=name,argv=list(map(str,argv)),startedUnix=started,
                finishedUnix=time.time(),exit=completed.returncode,
                stdoutSha256=sha(completed.stdout),stderrSha256=sha(completed.stderr))
    (out/(name+".command.json")).write_text(json.dumps(record,indent=2)+"\n")
    commands.append(record)
    require(completed.returncode==0,"Host process failed; retained "+name)
    return completed.stdout

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",required=True,type=Path)
    parser.add_argument("--demo",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    require(__debug__,"Assertions cannot be disabled")
    native,demo=args.native.resolve(strict=True),args.demo.resolve(strict=True)
    out=args.output
    require(out.is_absolute() and out==out.resolve(strict=False)
            and not out.exists() and out.parent.is_dir(),
            "Output must be an unused canonical absolute path")
    require(all(out!=p and out not in p.parents and p not in out.parents for p in (native,demo)),
            "Output may not alias source")
    reference=demo/"Docs/AssemblyShadow/History/M07R/R03/IR_Remediation/SNAP01_BOUNDED_DESIGN_f8a2/reference/diagnostic_stream.py"
    codec=demo/"Tools/AssemblyShadow/R03IR/bounded_v2/BoundedDiagnosticsV2.h"
    driver=native/"tools/r03/diagnostic_stream/frozen_execution_v2_driver.cpp"
    frozen=native/"libil2cpp/vm/AssemblyShadowFrozenExecutionCapture.h"
    sources=[reference,codec,driver,frozen,native/"libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
             native/"libil2cpp/vm/AssemblyShadowExecutionSerializer.h",Path(__file__).resolve()]
    require(blob(reference.read_bytes())==REFERENCE_BLOB,"Immutable original V2 reference blob changed")
    before={str(p):dict(sha256=sha(p.read_bytes()),gitBlob=blob(p.read_bytes()))
            for p in sources}
    spec=importlib.util.spec_from_file_location("frozen_v2_wire_reference",reference)
    specmod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(specmod)
    out.mkdir(mode=0o700)
    commands=[];profiles=[];error=None
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE="1",
             ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                           else "abort_on_error=1:detect_leaks=1"),
             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    try:
        compiler=shutil.which(os.environ.get("CXX","clang++"))
        require(compiler is not None,"C++ compiler unavailable")
        context=specmod.binding(IDENTITY).hex()
        inline_hash=sha(specmod.inline_bytes(INLINE))
        inline_len=len(specmod.inline_bytes(INLINE))
        for profile,flags in [("strict",["-O2"]),("sanitized",
                              ["-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])]:
            p=out/profile;p.mkdir()
            binary=p/"frozen-v2"
            base=[compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                  "-iquote",native/"libil2cpp/vm",
                  "-I"+str(codec.parent),*flags,driver,
                  native/"tools/r03/diagnostic_stream/allocation_probe.cpp",
                  "-o",binary]
            run(base,p,"compile",commands,env)
            def invoke(mode):
                folder=p/mode;folder.mkdir()
                raw=run([binary,mode,folder,context,inline_hash,str(inline_len),
                         "300000","1024"],folder,"driver",commands,env)
                result=json.loads(raw)
                require(result["mode"]==mode and result["capturedClasses"]==1024,
                        "Unexpected source-bound fixture")
                return folder,result
            good,receipt=invoke("complete")
            require(receipt["complete"] and receipt["wireComplete"],
                    "Successful codec receipt missing")
            x=(good/"general.json").read_bytes()
            y=(good/"execution.json").read_bytes()
            data=(good/"wire.bin").read_bytes()
            require(len(x)>262144 and len(y)==331078,"Complete two-field producer lengths")
            fields={"finalDiagnostics":x.decode("utf-8"),
                    "finalExecutionDiagnostics":y.decode("utf-8")}
            expected,descriptor=specmod.encode(IDENTITY,INLINE,fields)
            require(data==expected,"Byte mismatch from actual C++ capture/encoder to immutable V2 wire reference")
            parsed=specmod.decode(io.BytesIO(data),descriptor,IDENTITY,INLINE)
            require(parsed["finalDiagnostics"].encode()==x and
                    parsed["finalExecutionDiagnostics"].encode()==y,"Lossless V2 decoded fields")
            hydrated=specmod.hydrate({**INLINE,"diagnosticArtifact":descriptor},data,
                                    descriptor,IDENTITY)
            require(hydrated["status"]=="Failed" and hydrated["reportTransport"]=="NativeBoundedSnapshotV2",
                    "Immutable semantic failure was promoted")
            short,s=invoke("short")
            require(not s["complete"] and not s["wireComplete"]
                    and s["wireBytes"]==0 and not (short/"wire.bin").exists(),
                    "Short capture was accepted")
            partial,z=invoke("sink-fail")
            corrupt=(partial/"partial.wire.bin").read_bytes()
            require(not z["complete"] and not z["wireComplete"] and z["wireBytes"]==0
                    and corrupt and len(corrupt)<len(data),
                    "Short native output accepted")
            try:
                specmod.decode(io.BytesIO(corrupt),descriptor,IDENTITY,INLINE)
            except specmod.ContractError:
                pass
            else:
                raise RuntimeError("Original strict V2 decoder accepted short partial wire")
            (good/"descriptor.json").write_text(json.dumps(descriptor,indent=2)+"\n")
            profiles.append(dict(profile=profile,originalGeneralBytes=len(x),
                                 originalExecutionBytes=len(y),encodedBytes=len(data),
                                 sha256=sha(data),strictWireReferenceByteParity=True,
                                 originalSemanticStatus="Failed",completeCapturedClasses=1024,
                                 overflowRejected=True,partialSinkRejected=True))
    except Exception as exc:
        error=repr(exc)
        raise
    finally:
        unchanged=all(before[str(p)]==dict(sha256=sha(p.read_bytes()),
                                          gitBlob=blob(p.read_bytes())) for p in sources)
        result=dict(kind="R03P1FrozenExecutionNativeV2Interop",
                    result="PassedWithinInactiveHostScope" if len(profiles)==2 and unchanged else "FailedOrIncomplete",
                    profiles=profiles,commandCount=len(commands),commands=commands,
                    sourceBefore=before,sourceUnchanged=unchanged,firstError=error,
                    actualUnity=False,realVmCaptureIntegrated=False,
                    liveVmCoherenceProven=False,VMGcRssBoundProven=False,
                    nativeCSharpPlayerIntegrated=False,semanticVerifierIntegrated=False,
                    engineOwnerCapabilityProven=False,runtimeAcceptance=False)
        (out/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        manifest=[dict(path=str(p.relative_to(out)),bytes=p.stat().st_size,
                       sha256=sha(p.read_bytes())) for p in sorted(out.rglob("*")) if p.is_file()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":manifest},indent=2)+"\n")
    require(len(profiles)==2 and unchanged,"Frozen V2 host result incomplete")
    print(json.dumps({p["profile"]:p["sha256"] for p in profiles},indent=2))

if __name__=="__main__":
    main()
