#!/usr/bin/env python3
"""Source-bound P1 physical assembly registry inventory under explicit host mocks.

Compiles the exact ADDITIVE native Assembly method and tests byte parity with
the immutable original and current product general V1 serializers. It does
not claim Unity/IL2CPP SDK compilation or real Player/metadata lifetime.
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

METHOD = "bool Assembly::TryCaptureBoundedPhysicalAssemblies("
OLD = "uint64_t Assembly::CaptureShadowEnumeration("
MARKER = "// __CURRENT_PRODUCTION_PHYSICAL_METHOD__"
ORIGINAL_GIT_BLOB = "226b01273d3e30d190422c4c4c740c49a8d957f0"
REQUIRED = (
    "os::FastAutoLock lock(&s_assemblyLock)",
    "if(!assembly->token)continue",
    "capture.Add(assembly->aname.name,isInterpreter)",
    "hybridclr::metadata::IsInterpreterImage(assembly->image)",
    "capture.Seal(afterGeneration,beforeGeneration==afterGeneration)",
    "if(!assembly)return capture.RejectSource()",
)
FORBIDDEN = ("Class::", "MetadataCache::", "g_MetadataLock", "push_back(",
             "std::vector<", "new ", "delete ", "il2cpp_class_for_each")

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

def extract(src: str, symbol: str) -> str:
    if src.count(symbol) != 1:
        raise RuntimeError("Expected one exact production method: " + symbol)
    start = src.index(symbol)
    opening = src.index("{", start)
    depth = 0
    mode = None
    escaped = False
    i = opening
    while i < len(src):
        ch = src[i]
        pair = src[i:i+2]
        if mode == "line":
            if ch == "\n": mode = None
        elif mode == "block":
            if pair == "*/": mode = None; i += 1
        elif mode in ("'", '"'):
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == mode: mode = None
        elif pair == "//":
            mode = "line"; i += 1
        elif pair == "/*":
            mode = "block"; i += 1
        elif ch in ("'", '"'):
            mode = ch
        elif ch == "{": depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0: return src[start:i+1]
        i += 1
    raise RuntimeError("Unbalanced C++ source method")

def run(argv: list, root: Path, label: str, commands: list) -> None:
    started = time.time()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1",
               ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                             else "abort_on_error=1:detect_leaks=1"),
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    p = subprocess.run([str(a) for a in argv], capture_output=True,
                       check=False, timeout=200, env=env)
    (root/(label+".stdout")).write_bytes(p.stdout)
    (root/(label+".stderr")).write_bytes(p.stderr)
    receipt = {"name":label,"argv":list(map(str,argv)),"startUnix":started,
               "finishUnix":time.time(),"exit":p.returncode,
               "stdoutSha256":digest(p.stdout),"stderrSha256":digest(p.stderr)}
    commands.append(receipt)
    (root/(label+".command.json")).write_text(json.dumps(receipt,indent=2)+"\n")
    if p.returncode:
        raise RuntimeError(f"{label} failed with {p.returncode}; raw command/logs preserved")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    a = parser.parse_args()
    if not __debug__: raise RuntimeError("Python -O is prohibited")
    repo = a.native.resolve(strict=True)
    out = a.output
    if (not out.is_absolute() or out != out.resolve(strict=False)
        or out.exists() or not out.parent.is_dir()
        or out==repo or out in repo.parents or repo in out.parents):
        raise RuntimeError("Unused canonical external output directory required")
    relative = [
        "libil2cpp/vm/Assembly.cpp",
        "libil2cpp/vm/Assembly.h",
        "libil2cpp/vm/AssemblyShadowBoundedPhysicalAssemblies.h",
        "libil2cpp/vm/AssemblyShadowFrozenGeneralCapture.h",
        "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
        "libil2cpp/vm/AssemblyShadowDiagnostics.h",
        "libil2cpp/vm/AssemblyShadowTypes.h",
        "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
        "tools/r03/diagnostic_stream/bounded_physical_assemblies_fixture.cpp",
        "tools/r03/diagnostic_stream/fixtures/LegacyAssemblyShadowDiagnostics.cpp",
        "tools/r03/diagnostic_stream/fixtures/AssemblyShadowDiagnostics.h",
        "tools/r03/diagnostic_stream/allocation_probe.cpp",
        "tools/r03/diagnostic_stream/run_bounded_physical_assemblies.py",
        "libil2cpp/vm/AssemblyShadowMetadataLifetime.h",
    ]
    sources=[repo/p for p in relative]
    expected={str(p):{"bytes":p.stat().st_size,"sha256":digest(p.read_bytes()),
                      "gitBlob":blob(p.read_bytes())}for p in sources}
    if blob(sources[9].read_bytes())!=ORIGINAL_GIT_BLOB:
        raise RuntimeError("Immutable original general V1 serializer blob changed")
    cpp=sources[0].read_text()
    current=extract(cpp,METHOD)
    original=extract(cpp,OLD)
    for check in REQUIRED:
        if check not in current:
            raise RuntimeError("Current producer lost protected source contract: "+check)
    statements="\n".join(x.split("//",1)[0] for x in current.splitlines())
    if any(x in statements for x in FORBIDDEN):
        raise RuntimeError("Unexpected metadata/allocating call under assembly registry lock")
    if "CopyValidAssemblies(assemblies, s_Assemblies)" not in original or \
       "os::FastAutoLock lock(&s_assemblyLock)" not in original:
        raise RuntimeError("Old physical enumeration source has changed")
    if "static bool TryCaptureBoundedPhysicalAssemblies(" not in sources[1].read_text():
        raise RuntimeError("Enabled assembly declaration missing")
    fixture=sources[8].read_text()
    if fixture.count(MARKER)!=1: raise RuntimeError("Fixture marker ambiguity")
    out.mkdir(mode=0o700)
    commands=[]
    completed=[]
    failure=None
    try:
        (out/"exact-method.cpp").write_text(current+"\n")
        (out/"mock-compiled.cpp").write_text(fixture.replace(MARKER,current))
        stub=out/"stubs";stub.mkdir()
        (stub/"il2cpp-config.h").write_text("#pragma once\n// Host-only stub, not official Unity SDK.\n")
        cxx=shutil.which(os.environ.get("CXX","clang++"))
        if not cxx: raise RuntimeError("Configured native C++ compiler missing")
        for profile,extras in (("strict",["-O2"]),("sanitized",[
            "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            binary=out/(profile+".exe")
            obj=out/(profile+"-original.o")
            base=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                  "-pthread","-iquote",repo/"libil2cpp/vm",
                  "-iquote",stub,*extras]
            run(base+["-DAssemblyShadowDiagnostics=OriginalDiagnostics",
                "-c",sources[9],"-o",obj],out,profile+"-legacy-compile",commands)
            run(base+[out/"mock-compiled.cpp",sources[11],sources[4],
                      obj,"-o",binary],out,profile+"-compile",commands)
            run([binary],out,profile+"-run",commands)
            completed.append(profile)
    except Exception as exc:
        failure=repr(exc)
        raise
    finally:
        preserved=all(expected[str(p)]=={"bytes":p.stat().st_size,
                     "sha256":digest(p.read_bytes()),"gitBlob":blob(p.read_bytes())}
                      for p in sources)
        good=completed==["strict","sanitized"] and preserved
        result={"kind":"R03P1BoundedPhysicalAssemblyActualSourceHost",
                "result":"PassedWithinHostScope" if good else "FailedOrIncomplete",
                "methodSha256":digest(current.encode()),
                "originalPhysicalEnumerationSha256":digest(original.encode()),
                "actualProductionMethodCompiled":True,"sourceBefore":expected,
                "sourcePreserved":preserved,"profilesCompleted":completed,
                "commands":commands,"firstError":failure,
                "originalV1SerializerGitBlob":ORIGINAL_GIT_BLOB,
                "mockRegistryAndInterpreterClassification":True,
                "realUnityMetadataLifetimeProof":False,
                "sourceGenerationIsGlobalEpoch":False,
                "officialUnitySdkCompiled":False,
                "actualUnityPlayerRun":False,"runtimeAcceptance":False}
        (out/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        manifest=[{"path":str(p.relative_to(out)),"bytes":p.stat().st_size,
                   "sha256":digest(p.read_bytes())}
                  for p in sorted(out.rglob("*")) if p.is_file() and not p.is_symlink()]
        (out/"MANIFEST.json").write_text(json.dumps({"files":manifest},indent=2)+"\n")
    print(json.dumps({"result":result["result"],"profiles":completed,
                      "methodSha256":result["methodSha256"]},indent=2))
    if not good: raise SystemExit(1)

if __name__=="__main__":
    main()
