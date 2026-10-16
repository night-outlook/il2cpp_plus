#!/usr/bin/env python3
"""Source-bound R03 P1 class row host contracts, NOT actual Unity VM evidence.

The exact native visibility callback and new entry point are compiled with
explicit class/image/metadata-lock mocks; immutable historical/current V1
serializers are compiled independently and their full bytes compared.
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

METHOD = "bool AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses("
CALLBACK = "void CaptureOwnedOrdinaryClass("
OLD = "void AssemblyShadowVisibility::CollectOrdinaryClasses("
CALLBACK_MARKER = "// __EXACT_CALLBACK_SOURCE__"
METHOD_MARKER = "// __EXACT_NATIVE_METHOD_SOURCE__"
ORIGINAL_GIT_BLOB = "226b01273d3e30d190422c4c4c740c49a8d957f0"
REQUIRED = (
    "os::FastAutoLock lock(&g_MetadataLock)",
    "il2cpp_class_for_each(CaptureOwnedOrdinaryClass,&capture)",
    "capture.Seal(after,before==after)"
)
CALLBACK_REQUIRED = (
    "owned.Add(klass->image->assembly->aname.name,klass->namespaze,klass->name,",
    "AssemblyShadowVisibility::ClassUsesStagedMetadata(klass)",
    "hybridclr::metadata::IsInterpreterImage(klass->image)",
    "klass->generic_class!=nullptr"
)
FORBIDDEN = ("std::vector<", "push_back(", "std::string(", "Class::FromIl2CppType(",
             "MetadataCache::", "new ", "delete ", "return klass")

def hash256(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def git_blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def extract(source:str,signature:str)->str:
    if source.count(signature)!=1:raise RuntimeError("Ambiguous native C++ symbol: "+signature)
    start=source.index(signature)
    opening=source.index("{",start)
    depth=0;mode=None;escape=False;index=opening
    while index<len(source):
        ch=source[index];pair=source[index:index+2]
        if mode=="line":
            if ch=="\n":mode=None
        elif mode=="block":
            if pair=="*/":mode=None;index+=1
        elif mode in ("'",'"'):
            if escape:escape=False
            elif ch=="\\":escape=True
            elif ch==mode:mode=None
        elif pair=="//":mode="line";index+=1
        elif pair=="/*":mode="block";index+=1
        elif ch in ("'",'"'):mode=ch
        elif ch=="{":depth+=1
        elif ch=="}":
            depth-=1
            if depth==0:return source[start:index+1]
        index+=1
    raise RuntimeError("Unbalanced C++ function body: "+signature)

def exec_case(argv:list[str],out:Path,name:str,receipts:list):
    began=time.time()
    env=dict(os.environ,
             ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform=="darwin"
                           else "abort_on_error=1:detect_leaks=1"),
             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    p=subprocess.run([str(x) for x in argv],capture_output=True,env=env,timeout=240)
    (out/(name+".stdout.bin")).write_bytes(p.stdout)
    (out/(name+".stderr.bin")).write_bytes(p.stderr)
    row={"name":name,"command":list(map(str,argv)),"startedUnix":began,
         "finishedUnix":time.time(),"exit":p.returncode,
         "stdoutSha256":hash256(p.stdout),"stderrSha256":hash256(p.stderr)}
    receipts.append(row)
    (out/(name+".command.json")).write_text(json.dumps(row,indent=2)+"\n")
    if p.returncode:raise RuntimeError(name+" failed with exit "+str(p.returncode))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    if not __debug__:raise RuntimeError("Python assertions must not be disabled")
    repo=args.native.resolve(strict=True)
    dest=args.output
    if(not dest.is_absolute() or dest!=dest.resolve(strict=False)
       or dest.exists() or not dest.parent.is_dir()
       or dest==repo or dest in repo.parents or repo in dest.parents):
        raise RuntimeError("Fresh absolute output directory outside sources required")
    relative=[
        "libil2cpp/vm/AssemblyShadowVisibility.cpp",
        "libil2cpp/vm/AssemblyShadowVisibility.h",
        "libil2cpp/vm/AssemblyShadowBoundedOrdinaryClasses.h",
        "libil2cpp/vm/AssemblyShadowFrozenGeneralCapture.h",
        "libil2cpp/vm/AssemblyShadowDiagnostics.cpp",
        "libil2cpp/vm/AssemblyShadowDiagnostics.h",
        "libil2cpp/vm/AssemblyShadowTypes.h",
        "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h",
        "tools/r03/diagnostic_stream/bounded_ordinary_classes_fixture.cpp",
        "tools/r03/diagnostic_stream/run_bounded_ordinary_classes.py",
        "tools/r03/diagnostic_stream/fixtures/LegacyAssemblyShadowDiagnostics.cpp",
        "tools/r03/diagnostic_stream/fixtures/AssemblyShadowDiagnostics.h",
        "tools/r03/diagnostic_stream/allocation_probe.cpp",
    ]
    sources=[repo/p for p in relative]
    before={str(p):{"bytes":p.stat().st_size,"sha256":hash256(p.read_bytes()),
                    "gitBlob":git_blob(p.read_bytes())}for p in sources}
    if git_blob(sources[10].read_bytes())!=ORIGINAL_GIT_BLOB:
        raise RuntimeError("Immutable original general serializer changed")
    cpp=sources[0].read_text()
    current=extract(cpp,METHOD)
    callback=extract(cpp,CALLBACK)
    old=extract(cpp,OLD)
    if not all(x in current for x in REQUIRED):
        raise RuntimeError("Reviewed lock/generation/source collector contract changed")
    if not all(x in callback for x in CALLBACK_REQUIRED):
        raise RuntimeError("Reviewed class callback field source changed")
    if not all(x in old for x in (
        "os::FastAutoLock lock(&g_MetadataLock)",
        "il2cpp_class_for_each(CollectClass, &classes)",
        "generation = AssemblyShadow::ActiveGeneration()")):
        raise RuntimeError("Original class source enumeration changed")
    # Only executable statement substrings, not explanatory comments.
    executable="\n".join(line.split("//",1)[0] for line in (current+"\n"+callback).splitlines())
    if any(x in executable for x in FORBIDDEN):
        raise RuntimeError("Unexpected unreviewed allocating/meta source operation")
    if "static bool TryCaptureBoundedOrdinaryClasses(" not in sources[1].read_text():
        raise RuntimeError("Enabled native visibility declaration missing")
    fixture=sources[8].read_text()
    if fixture.count(CALLBACK_MARKER)!=1 or fixture.count(METHOD_MARKER)!=1:
        raise RuntimeError("Host source markers ambiguous")
    dest.mkdir(mode=0o700)
    cases=[]
    completed=[]
    first_error=None
    try:
        (dest/"current-callback.cpp").write_text(callback+"\n")
        (dest/"current-method.cpp").write_text(current+"\n")
        (dest/"mock-combined.cpp").write_text(
            fixture.replace(CALLBACK_MARKER,callback).replace(METHOD_MARKER,current))
        (dest/"stubs").mkdir()
        (dest/"stubs/il2cpp-config.h").write_text(
            "#pragma once\n// HOST-ONLY test stub, not official Unity SDK.\n")
        cxx=shutil.which(os.environ.get("CXX","clang++"))
        if not cxx:raise RuntimeError("CXX native compiler missing")
        for profile,opts in (("strict",["-O2"]),("sanitized",[
            "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined"])):
            exe=dest/(profile+".exe")
            original_obj=dest/(profile+"-original.o")
            common=[cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                    "-pthread","-iquote",repo/"libil2cpp/vm",
                    "-iquote",dest/"stubs",*opts]
            exec_case(common+["-DAssemblyShadowDiagnostics=OriginalDiagnostics",
                      "-c",sources[10],"-o",original_obj],
                      dest,profile+"-original-compile",cases)
            exec_case(common+[dest/"mock-combined.cpp",
                      sources[12],sources[4],original_obj,"-o",exe],
                      dest,profile+"-compile",cases)
            exec_case([exe],dest,profile+"-run",cases)
            completed.append(profile)
    except Exception as error:
        first_error=repr(error)
        raise
    finally:
        preserved=all(before[str(p)]=={
            "bytes":p.stat().st_size,
            "sha256":hash256(p.read_bytes()),
            "gitBlob":git_blob(p.read_bytes())}for p in sources)
        passed=preserved and completed==["strict","sanitized"]
        result={
            "kind":"R03P1BoundedOrdinaryClassActualSourceHost",
            "result":"PassedWithinHostScope" if passed else "FailedOrIncomplete",
            "actualCallbackAndMethodBodiesCompiled":True,
            "callbackSha256":hash256(callback.encode()),
            "methodSha256":hash256(current.encode()),
            "originalCollectorSha256":hash256(old.encode()),
            "immutableOriginalSerializerBlob":ORIGINAL_GIT_BLOB,
            "sources":before,"sourcePreserved":preserved,
            "profilesCompleted":completed,"commands":cases,
            "firstError":first_error,
            "hostMetadataAndVisibilityMocks":True,
            "realMetadataShutdownQuiescenceProven":False,
            "realClassEnumeratorAllocationBoundProven":False,
            "realUnityNativeSdkCompiler":False,
            "actualUnityPlayerRun":False,
            "runtimeAcceptance":False
        }
        (dest/"RESULT.json").write_text(json.dumps(result,indent=2)+"\n")
        files=[{"path":str(p.relative_to(dest)),"bytes":p.stat().st_size,
                "sha256":hash256(p.read_bytes())}
               for p in sorted(dest.rglob("*")) if p.is_file()]
        (dest/"MANIFEST.json").write_text(json.dumps({"files":files},indent=2)+"\n")
    print(json.dumps({"result":result["result"],"profiles":completed,
                      "methodSha256":result["methodSha256"]},indent=2))
    if not passed:raise SystemExit(1)

if __name__=="__main__":
    main()
