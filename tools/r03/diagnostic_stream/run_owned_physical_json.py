#!/usr/bin/env python3
"""R03 P1 source-bound, inactive owned physical display-name JSON host check.

Compiles ACTUAL production Capture+Json headers against typed caller-owned
fixtures. Does not read VM class memory or establish TypeKeys, GC/RSS,
global VM epoch, linked Player or Unity SDK acceptance.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys

from run_owned_physical_names import execute, sha, source_identity, write

HEADER="libil2cpp/vm/AssemblyShadowOwnedPhysicalNamesJson.h"
NAMES="libil2cpp/vm/AssemblyShadowOwnedPhysicalNames.h"
WITNESS="libil2cpp/vm/AssemblyShadowExecutionRegistrationWitness.h"
WRITER="libil2cpp/vm/AssemblyShadowDiagnosticWriter.h"
FIXTURE="tools/r03/diagnostic_stream/owned_physical_json_fixture.cpp"
RUNNER="tools/r03/diagnostic_stream/run_owned_physical_json.py"
SOURCES=(HEADER,NAMES,WITNESS,WRITER,FIXTURE,RUNNER,
         "tools/r03/diagnostic_stream/run_owned_physical_names.py")
GROUPS=(
 "empty-owned-json",
 "one-candidate-exact-json",
 "reverse-source-order-and-partition",
 "same-display-name-distinct-registered-slots",
 "escaping-quotes-backslash-and-controls",
 "valid-utf8-retained-unchanged",
 "invalid-utf8-continuation-refused",
 "overlong-and-surrogate-utf8-refused",
 "empty-required-physical-assembly-refused",
 "empty-required-simple-name-refused",
 "invalid-source-capture-refused",
 "missing-source-witness-flag-refused",
 "mismatched-row-count-and-text-byte-receipts-refused",
 "revision-and-saturated-revision-refused",
 "forbidden-success-scope-refused",
 "small-byte-budget-atomically-refused",
 "output-limit-above-fixed-cap-refused",
 "single-use-output-revokes-on-reuse",
 "owned-json-survives-source-revoke",
 "source-string-mutation-before-compose-independent",
 "worst-case-escaped-text-under-fixed-budget",
)

def verify_source(root:Path)->dict:
    code=(root/HEADER).read_text()
    fixture=(root/FIXTURE).read_text()
    names=(root/NAMES).read_text()
    writer=(root/WRITER).read_text()
    required=(
        "inline bool Compose(",
        "if(!source.Complete())",
        "sourceReceipt.allRowsBoundToObservedRegistry",
        "sourceReceipt.boundRegistrationRows!=count",
        "sourceReceipt.registryRevisionBefore!=sourceReceipt.registryRevisionAfter",
        "std::array<uint8_t,names::kRegistrationSlots> seen{}",
        "const Error assembly=CheckUtf8(",
        "const Error ns=CheckUtf8(",
        "const Error type=CheckUtf8(",
        "if(seen[row.registrySlot])",
        "diagnostic::Writer writer(output.Sink(),maximumBytes);",
        "writer.Finish()",
        "if(!output.Seal(finished))",
        "receipt.ownedNonCanonicalJsonComplete=true;",
        "bool canonicalExecutionTypeKeys=false;",
        "bool physicalClassPointerOriginProven=false;",
        "bool coherentVmEpochProven=false;",
        "bool runtimeAcceptance=false;",
        "bytes_.fill(0);used_=0;",
        "static_assert(sizeof(Record)<=Record::kMaximumObjectBytes",
    )
    for token in required:
        if token not in code:
            raise RuntimeError("Production pure-owned serializer contract missing: "+token)
    if code.count('\"R03P1ObservedPhysicalDisplayNames\"') != 0:
        # Kind literal is in a JSON-escaped C++ string, never C++ user data.
        raise RuntimeError("Unexpected raw serializer kind form")
    if code.count("R03P1ObservedPhysicalDisplayNames")!=1:
        raise RuntimeError("Exact independent evidence schema kind changed")
    if names.count("bool allRowsBoundToObservedRegistry=false;")!=1:
        raise RuntimeError("Source capture scope flag changed")
    if "struct QuotedText" not in writer or "class Writer" not in writer:
        raise RuntimeError("Actual shared escaped bounded Writer unavailable")
    actual=tuple(re.findall(r'\bcheck\("([^"]+)"',fixture))
    if actual!=GROUPS:
        raise RuntimeError("Missing/duplicated/unordered actual host cases: "+repr(actual))
    for token in ('#include "AssemblyShadowOwnedPhysicalNamesJson.h"',
                  "#include \"AssemblyShadowOwnedPhysicalNames.h\"",
                  'P1 OWNED PHYSICAL JSON HOST PASS'):
        if token not in fixture:raise RuntimeError("Fixture does not compile actual source: "+token)
    # Strip line comments and prohibit new heap/VM/file work inside pure
    # serializer. It may only touch an already completed immutable Capture.
    body="\n".join(line.split("//",1)[0] for line in code.splitlines())
    for token in ("il2cpp_class_for_each(", "g_MetadataLock", "ExecutionObservationLock ",
                  "fopen(", "std::fstream", "std::vector<", "std::string ",
                  "new ", "Class::", "MetadataCache::"):
        if token in body:raise RuntimeError("Owned serializer has forbidden VM/heap/file access: "+token)
    return {"sourceHeadersVerified":4,"actualFixtureCaseIds":list(GROUPS),
            "noVmMetadataOrFileOperations":True,
            "immutableOwnedInputRequired":True,
            "nonCanonicalScopeOnly":True}

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if not __debug__:raise RuntimeError("Python optimization forbidden")
    native=a.native
    dest=a.output
    if (not native.is_absolute() or not native.is_dir() or
        native!=native.resolve(strict=True) or not dest.is_absolute() or
        dest!=dest.resolve(strict=False) or dest.exists() or
        not dest.parent.is_dir() or native==dest or
        native in dest.parents or dest in native.parents):
        raise RuntimeError("Fresh, disjoint canonical native and evidence paths required")
    before={path:source_identity(native/path) for path in SOURCES}
    contract=verify_source(native)
    dest.mkdir(mode=0o700)
    cxx=shutil.which(os.environ.get("CXX","clang++"))
    env=dict(os.environ,
             ASAN_OPTIONS=("detect_leaks=0:halt_on_error=1" if sys.platform=="darwin"
                           else "detect_leaks=1:halt_on_error=1"),
             UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    commands=[];profiles={};first_error=None;postflight_error=None
    try:
        if not cxx:raise RuntimeError("Host C++17 compiler is unavailable")
        execute([cxx,"--version"],dest,"compiler-version",env,commands,30)
        for label,opts in (("strict",["-O2"]),
                           ("sanitized",["-O1","-g","-fno-omit-frame-pointer",
                                          "-fsanitize=address,undefined"])):
            exe=dest/(label+".exe")
            execute([cxx,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                     "-pthread","-iquote",native/"libil2cpp/vm",*opts,
                     native/FIXTURE,"-o",exe],dest,label+"-compile",env,commands,180)
            output=execute([exe],dest,label+"-run",env,commands,180).decode("utf-8")
            actual=tuple(line[5:] for line in output.splitlines() if line.startswith("PASS "))
            if actual!=GROUPS or not output.rstrip().endswith("P1 OWNED PHYSICAL JSON HOST PASS"):
                raise RuntimeError("Missing, repeated or unordered owned JSON host group")
            profiles[label]={"result":"PassedWithinOwnedNonCanonicalJsonHostScope",
                             "caseGroups":list(actual)}
    except BaseException as exc:
        first_error=repr(exc)
    finally:
        lifetime=any(cmd["commandLifetimeUnverified"] for cmd in commands)
        try:
            if lifetime:raise RuntimeError("Unknown child lifetime; source/Git postflight forbidden")
            after={path:source_identity(native/path) for path in SOURCES}
            preserved=before==after
        except Exception as exc:
            after={};preserved=False;postflight_error=repr(exc)
        compiled=preserved and all(any(cmd["name"]==profile+"-compile" and
            cmd["status"]=="Passed" and cmd["exit"]==0 for cmd in commands)
            for profile in ("strict","sanitized"))
        passed=(first_error is None and postflight_error is None and preserved
                and not lifetime and compiled and len(commands)==5
                and set(profiles)=={"strict","sanitized"} and
                all(cmd["status"]=="Passed" for cmd in commands))
        result={"kind":"R03P1OwnedPhysicalNamesJsonActualSourceHost","schemaVersion":1,
                "result":"PassedWithinOwnedPhysicalNamesJsonHostScope" if passed else "FailedOrIncomplete",
                "sourceContract":contract,"sourceBefore":before,"sourceAfter":after,
                "sourcePreserved":preserved,"commands":commands,"profiles":profiles,
                "firstError":first_error,"postflightError":postflight_error,
                "commandLifetimeUnverified":lifetime,"actualSourceCompiled":compiled,
                "ownedJsonSchema":"R03P1ObservedPhysicalDisplayNames",
                "rowScope":"OwnedSnapshotLocalDisplayNamesOnly",
                "canonicalExecutionTypeKeys":False,"physicalClassPointerOriginProven":False,
                "coherentVmEpochProven":False,"officialUnitySdkCompiled":False,
                "UnityPlayerExecuted":False,"runtimeAcceptance":False}
        write(dest/"RESULT.json",result)
        files=[{"path":str(path.relative_to(dest)),"bytes":path.stat().st_size,
                "sha256":sha(path.read_bytes())} for path in sorted(dest.rglob("*"))
               if path.is_file() and not path.is_symlink()]
        write(dest/"MANIFEST.json",{"files":files,
             "inventoryScope":"UnsealedObservedFiles" if lifetime else "CompletedHostFiles",
             "commandLifetimeUnverified":lifetime})
    print(json.dumps({"result":result["result"],"groupsPerProfile":len(GROUPS),
                      "profiles":list(profiles)}))
    if not passed:raise SystemExit(1)

if __name__=="__main__":main()
