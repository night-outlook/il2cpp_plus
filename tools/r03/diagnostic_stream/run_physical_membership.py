#!/usr/bin/env python3
"""R03 P1 inactive physical class enumeration membership source/host gate.

Compiles actual production source under explicit VM metadata/iterator mocks.
No linked Unity Player, source pointer ownership, epoch or teardown proof.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

HEADER="libil2cpp/vm/AssemblyShadowPhysicalMembership.h"
SOURCE="libil2cpp/vm/AssemblyShadowPhysicalMembershipSource.inl"
CPP="libil2cpp/vm/AssemblyShadow.cpp"
API="libil2cpp/vm/AssemblyShadow.h"
FIXTURE="tools/r03/diagnostic_stream/physical_membership_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_physical_membership.py"
SOURCES=(HEADER,SOURCE,CPP,API,FIXTURE,RUNNER,
         "libil2cpp/vm/AssemblyShadowExecutionRegistrationWitness.h",
         "libil2cpp/vm/AssemblyShadowExecutionRegistryRevision.h",
         "libil2cpp/vm/AssemblyShadowExecutionRegistryPublisher.h",
         "libil2cpp/vm/AssemblyShadowMetadataLifetime.h")
BASE_CPP_BLOB="dbe067c95476e2ba95f065fe9e67ce85639845c2"
BASE_API_BLOB="4f4d6a59675d66786826e32906d863efa4485d27"
GROUPS=(
 "empty-registry","one-enumerated","candidate-active-enumerated",
 "unrelated-physical-class-ignored","duplicate-enumeration-not-double-counted",
 "hash-collision-addresses","unmatched-class-refused",
 "partially-enumerated-refused","missing-witness-refused",
 "wrong-observed-count-refused","saturated-revision-refused",
 "admission-closed","duplicate-registry-identity",
 "null-enumerator-class","enumeration-over-cap",
 "revision-changes-after-iteration","state-changes-after-iteration",
 "active-changes-after-iteration","witness-changes-after-iteration",
 "consumed-scratch-refused","successful-scratch-reuse-refused",
)
def sha(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def blob(data:bytes)->str:
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()
def binding(path:Path)->dict:
    raw=path.read_bytes()
    return {"bytes":len(raw),"sha256":sha(raw),"gitBlob":blob(raw)}
def write(path:Path,obj)->None:
    path.write_text(json.dumps(obj,indent=2,sort_keys=True)+"\n")
def verify(native:Path)->str:
    cpp=(native/CPP).read_text()
    api=(native/API).read_text()
    source=(native/SOURCE).read_text()
    fixture=(native/FIXTURE).read_text()
    include='#include "AssemblyShadowPhysicalMembershipSource.inl"'
    header='#include "AssemblyShadowPhysicalMembership.h"'
    apiInclude='#include "il2cpp-api.h"'
    declaration="""    // P1 INACTIVE: observed registry addresses vs physical class enumerator.
    // No pointer is dereferenced/returned; membership does not prove lifetime,
    // physical ownership, VM epoch, or complete execution diagnostics.
    static bool TryAuditObservedPhysicalClassMembership(
        assembly_shadow_physical_membership::Scratch& scratch,
        assembly_shadow_physical_membership::Receipt& receipt);
"""
    namespace="namespace assembly_shadow_physical_membership { class Scratch; struct Receipt; }\n"
    inlBlock="""// P1 inactive: compare opaque registry values to physical class enumeration only.
#include "AssemblyShadowPhysicalMembershipSource.inl"
"""
    if cpp.count(include)!=1 or cpp.count(header)!=1 or cpp.count(apiInclude)!=1 or \
       api.count("static bool TryAuditObservedPhysicalClassMembership(")!=1 or \
       source.count("bool AssemblyShadow::TryAuditObservedPhysicalClassMembership(")!=1:
        raise RuntimeError("Exact physical membership production source method missing")
    if cpp.count("TryAuditObservedPhysicalClassMembership(")!=0:
        raise RuntimeError("Inactive method unexpectedly called by normal VM source")
    for token in ("assembly_shadow_metadata_lifetime::Gate::Reader admission(",
                  "ExecutionObservationLock lock;",
                  "os::FastAutoLock metadataLock(&g_MetadataLock);",
                  "il2cpp_class_for_each(",
                  "scratch.SameRegistry(","scratch.FullyMatched()",
                  "receipt.enumeratedMembershipComplete=true;"):
        if source.count(token)<1:raise RuntimeError("Source membership contract changed: "+token)
    if fixture.count(header)!=1 or fixture.count(include)!=1 or \
       any(('"'+name+'"') not in fixture for name in GROUPS):
        raise RuntimeError("Fixture source does not compile actual methods/case groups")
    # The new inactive owned-name source is an additive, callerless method;
    # strip only its exact include, emission and native API declaration.
    nameHeader="#include \"AssemblyShadowOwnedPhysicalNames.h\""
    nameInline="// P1 inactive owned names from physical enumerator matches only.\n#include \"AssemblyShadowOwnedPhysicalNamesSource.inl\"\n"
    nameNamespace="namespace assembly_shadow_owned_physical_names { class Capture; struct Receipt; }\n"
    nameDeclaration="    // P1 INACTIVE: copy display names synchronously for matched physical classes.\n    // No live class pointer survives; this is not an execution TypeKey.\n    static bool TryCaptureOwnedObservedPhysicalNames(\n        assembly_shadow_physical_membership::Scratch& membershipScratch,\n        assembly_shadow_owned_physical_names::Capture& names,\n        assembly_shadow_owned_physical_names::Receipt& receipt);\n"
    if (cpp.count(nameHeader)!=1 or cpp.count(nameInline)!=1 or
        api.count(nameNamespace)!=1 or api.count(nameDeclaration)!=1):
        raise RuntimeError("Owned name capture source changed unexpectedly")
    cpp=cpp.replace(nameHeader+"\n","").replace(nameInline,"")
    api=api.replace(nameNamespace,"").replace(nameDeclaration,"")
    oldcpp=(cpp.replace(header+"\n","").replace(apiInclude+"\n","")
            .replace(inlBlock,""))
    oldapi=(api.replace(namespace,"").replace(declaration,""))
    if blob(oldcpp.encode())!=BASE_CPP_BLOB or blob(oldapi.encode())!=BASE_API_BLOB:
        raise RuntimeError("Original native VM getter, observer or declaration changed")
    return source

def execute(argv:list,output:Path,name:str,env:dict,rows:list,timeout:int=180)->bytes:
    receipt={"name":name,"argv":list(map(str,argv)),"startedUnix":time.time(),
             "status":"Failed","exit":None,"commandLifetimeUnverified":False,
             "outputCompleteness":"Unavailable"}
    stdout=stderr=None
    try:
        p=subprocess.run(list(map(str,argv)),capture_output=True,timeout=timeout,env=env)
        stdout,stderr=p.stdout,p.stderr
        receipt.update(exit=p.returncode,outputCompleteness="CompleteDirectChildStreams")
        if p.returncode==0:receipt["status"]="Passed"
    except subprocess.TimeoutExpired as error:
        stdout,stderr=error.stdout,error.stderr
        receipt.update(exception=repr(error),timedOut=True,
                       commandLifetimeUnverified=True,outputCompleteness="CapturedPrefix")
    except KeyboardInterrupt:
        receipt.update(exception="KeyboardInterrupt",interrupted=True,
                       commandLifetimeUnverified=True)
    except OSError as error:
        receipt["exception"]=repr(error)
    except Exception as error:
        receipt.update(exception=repr(error),commandLifetimeUnverified=True)
    finally:
        for stream,raw in (("stdout",stdout),("stderr",stderr)):
            data=raw if raw is not None else b""
            (output/(name+"."+stream+".bin")).write_bytes(data)
            receipt[stream+"Available"]=raw is not None
            receipt[stream+"CapturedBytes"]=len(data)
            receipt[stream+"Sha256"]=sha(data)
        receipt["finishedUnix"]=time.time()
        rows.append(receipt)
        write(output/(name+".json"),receipt)
    if receipt["status"]!="Passed":
        raise RuntimeError(name+" failed; inspect original command receipts")
    return stdout

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Python -O forbidden")
    native,output=a.native,a.output
    if (not native.is_absolute() or native!=native.resolve(strict=True) or
        not native.is_dir() or not output.is_absolute() or
        output!=output.resolve(strict=False) or output.exists() or
        not output.parent.is_dir() or output in native.parents or
        native in output.parents or native==output):
        raise RuntimeError("Unused disjoint canonical host evidence root required")
    before={s:binding(native/s) for s in SOURCES}
    method=verify(native)
    output.mkdir(mode=0o700)
    compiler=shutil.which(os.environ.get("CXX","clang++"))
    env=dict(os.environ,
        ASAN_OPTIONS=("detect_leaks=0:halt_on_error=1" if sys.platform=="darwin"
                      else "detect_leaks=1:halt_on_error=1"),
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    rows=[];profiles={};firstError=None;postflightError=None
    try:
        if not compiler:raise RuntimeError("Host C++ compiler unavailable")
        execute([compiler,"--version"],output,"compiler-version",env,rows,30)
        for name,extra in (("strict",["-O2"]),
                           ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                                         "-fsanitize=address,undefined"])):
            executable=output/(name+".exe")
            execute([compiler,"-std=c++17","-Wall","-Wextra","-Werror",
                     "-pedantic","-pthread","-iquote",str(native/"libil2cpp/vm"),
                     *extra,str(native/FIXTURE),"-o",str(executable)],
                    output,name+"-compile",env,rows)
            raw=execute([executable],output,name+"-run",env,rows).decode("utf-8")
            groups=tuple(line[5:] for line in raw.splitlines() if line.startswith("PASS "))
            if groups!=GROUPS or not raw.rstrip().endswith(
                    "P1 PHYSICAL ENUMERATION MEMBERSHIP SOURCE HOST PASS"):
                raise RuntimeError("Wrong/missing/duplicate physical-membership case group "+name)
            profiles[name]={"result":"PassedWithinExplicitVmMocks","caseGroups":list(groups)}
    except BaseException as error:
        firstError=repr(error)
    finally:
        lifetime=any(x["commandLifetimeUnverified"] for x in rows)
        try:
            if lifetime:raise RuntimeError("Unverified child lifetime; no postflight")
            after={s:binding(native/s) for s in SOURCES}
            preserved=before==after
        except Exception as error:
            after={};preserved=False;postflightError=repr(error)
        compiled=(preserved and all(any(x["name"]==k+"-compile" and
                  x["status"]=="Passed" and x["exit"]==0 for x in rows)
                  for k in ("strict","sanitized")))
        passed=(firstError is None and postflightError is None and
                preserved and not lifetime and compiled and len(rows)==5 and
                len(profiles)==2 and all(x["status"]=="Passed" for x in rows))
        result={"kind":"R03P1InactivePhysicalEnumeratorMembershipSourceHost",
                "schemaVersion":1,"result":"PassedWithinInactivePhysicalEnumerationHostScope" if passed
                else "FailedOrIncomplete",
                "sourceMethodSha256":sha(method.encode()),
                "sourceBefore":before,"sourceAfter":after,"sourcePreserved":preserved,
                "commands":rows,"profiles":profiles,"firstError":firstError,
                "postflightError":postflightError,"commandLifetimeUnverified":lifetime,
                "actualProductionHeaderAndMethodCompiled":compiled,
                "vmIteratorAndMetadataInputsExplicitlyMocked":True,
                "membershipDoesNotAuthorizeDereference":True,
                "physicalClassPointerOriginProven":False,
                "metadataPointerLifetimeProven":False,
                "coherentVmEpochProven":False,
                "completeExecutionSource":False,
                "officialUnitySdkCompiled":False,
                "UnityPlayerExecuted":False,"runtimeAcceptance":False}
        write(output/"RESULT.json",result)
        inventory=[{"path":str(f.relative_to(output)),"bytes":f.stat().st_size,
                    "sha256":sha(f.read_bytes())} for f in sorted(output.rglob("*"))
                   if f.is_file() and not f.is_symlink()]
        write(output/"MANIFEST.json",{"files":inventory,
             "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
             "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":result["result"],"caseGroupsPerProfile":len(GROUPS),
                      "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
