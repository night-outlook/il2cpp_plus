#!/usr/bin/env python3
"""Compare the pinned current native execution function with immutable N output.

The historical function and VM-mock fixture stay byte-for-byte untouched.
This runs both real function bodies, not a stand-in rewritten formatter.
No live VM census, Unity, Player or runtime acceptance is implied.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

FROZEN_HASH = "051497faf816e4094b8f4e8f194c5b877812cee034c6901c755e8b6efdff9c40"
CURRENT_HASH = "4829a3ae7b9b0df57d054738f99d940b4ad7644a364df0502c9976469819d372"
# These Git blobs are exact source-identity checks, NOT a fallback to old code.
SOURCE_BLOBS = {
    "libil2cpp/vm/AssemblyShadow.cpp": "2aaa351a365d6b4ff80d01264a4ba958bc649cac",
    "libil2cpp/vm/AssemblyShadowExecutionSerializer.h": "3a0e7b1cc25bdeb16d0f341b67715b3433f04689",
    "libil2cpp/vm/AssemblyShadowDiagnosticWriter.h": "9dee4c9ea6febe4a919d1c7eb8faf2ad00b8a8e5",
}
FROZEN_BLOBS = {
    "test_diagnostic_size.py": "2a60eecdd3e9de7ae03556495d683cbdb52930e2",
    "test_snapshot_integration.py": "921f286002fe76ed43aceee8912d4c55f1378bf0",
    "source-extracts/GetExecutionDiagnosticsJson.function.cpp": "19d8c6ac259ea8200fc570136d4df25874c663ca",
}
CHECKS = "Docs/AssemblyShadow/History/M07R/R03/IR_Remediation/N_SNAPSHOT_PUBLICATION_91b7/checks"
MOCK_LOOP = "for(size_t i=0;i<classes.size();++i){classes[i].image=&image;s_executionClasses[i]=&classes[i];}"
CONTRACT = (
    "assembly_shadow_execution_diagnostic::Begin(output, heading)",
    "assembly_shadow_execution_diagnostic::AppendClass(output, row, index != 0)",
    "assembly_shadow_execution_diagnostic::EndClasses(output)",
    "assembly_shadow_r02::AppendDiagnostics(output)",
    "FormatMetadataOnly(&klass->byval_arg, images)",
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def pinned(path: Path, sha: str) -> bytes:
    data = path.read_bytes()
    if git_blob(data) != sha:
        raise RuntimeError("Source Git blob changed; review required: " + str(path))
    return data


def fixture_strings(text: str) -> tuple[str, str]:
    # AST gives literal, unchanged fixture strings without importing and running
    # the historical script or editing historical source extracts.
    assignments = {}
    for node in ast.parse(text).body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in ("stub", "main"):
                    assignments[target.id] = ast.literal_eval(node.value)
    if set(assignments) != {"stub", "main"} or MOCK_LOOP not in assignments["main"]:
        raise RuntimeError("Historical synthetic VM fixture changed")
    if "#define IL2CPP_DEBUG 0" not in assignments["stub"]:
        raise RuntimeError("Historical fixture debug macro changed")
    return assignments["stub"], assignments["main"]


def fixture(stub: str, main: str, variant: str) -> tuple[str, str, int]:
    if variant == "n-large":
        return stub, main, 1024
    if variant == "empty":
        return stub, main.replace(MOCK_LOOP,
            "for(size_t i=0;i<0;++i){classes[i].image=&image;s_executionClasses[i]=&classes[i];}"), 0
    if variant == "debug-class-flags":
        stub = stub.replace("#define IL2CPP_DEBUG 0", "#define IL2CPP_DEBUG 1")
        rewritten = """for(size_t i=0;i<classes.size();++i){
            classes[i].image=&image;
            classes[i].cctor_started=int32_t(i%2);
            classes[i].cctor_finished_or_no_cctor=int32_t(i%3==0);
            classes[i].initializationExceptionGCHandle=int32_t(i%5==0);
            classes[i].static_fields=reinterpret_cast<void*>(uintptr_t(0x1234));
            s_executionClasses[i]=&classes[i];
        }
        active.byAssembly[&assembly]=&assembly;"""
        return stub, main.replace(MOCK_LOOP, rewritten), 1024
    raise ValueError(variant)


def process(argv: list[str], name: str, out: Path, records: list[dict], env: dict) -> None:
    started = time.time()
    p = subprocess.run(argv, capture_output=True, timeout=120, env=env)
    stdout, stderr = out / (name + ".stdout.bin"), out / (name + ".stderr.bin")
    stdout.write_bytes(p.stdout)
    stderr.write_bytes(p.stderr)
    record = {"name": name, "argv": [str(x) for x in argv],
              "startedUnix": started, "finishedUnix": time.time(),
              "exit": p.returncode,
              "stdout": {"file": stdout.name, "sha256": digest(p.stdout), "bytes": len(p.stdout)},
              "stderr": {"file": stderr.name, "sha256": digest(p.stderr), "bytes": len(p.stderr)}}
    records.append(record)
    (out / (name + ".command.json")).write_text(json.dumps(record, indent=2) + "\n")
    if p.returncode != 0:
        raise RuntimeError("Compile/run failed; retained command: " + name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--demo", required=True, type=Path)
    parser.add_argument("--historical-output", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not __debug__:
        raise RuntimeError("Python assertions must remain enabled")
    native, demo = args.native.resolve(strict=True), args.demo.resolve(strict=True)
    historical = args.historical_output.resolve(strict=True)
    out = args.output
    if not out.is_absolute() or out.exists() or out != out.resolve(strict=False) or not out.parent.is_dir():
        raise RuntimeError("Unused canonical absolute output directory required")
    if any(p == out or p in out.parents or out in p.parents for p in (native, demo, historical)):
        raise RuntimeError("Output may not alias source or historical evidence")
    out.mkdir(mode=0o700)
    rows: list[dict] = []
    cases: list[dict] = []
    source_receipts = {}
    complete = False
    error = None
    try:
        for p, sha in SOURCE_BLOBS.items():
            data = pinned(native / p, sha)
            source_receipts[str(native / p)] = {"gitBlob": sha, "sha256": digest(data)}
        checks = demo / CHECKS
        for p, sha in FROZEN_BLOBS.items():
            data = pinned(checks / p, sha)
            source_receipts[str(checks / p)] = {"gitBlob": sha, "sha256": digest(data)}
        path = native / "libil2cpp/vm/AssemblyShadow.cpp"
        source = path.read_text()
        spec = importlib.util.spec_from_file_location("historical_body_parser", checks / "test_snapshot_integration.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        marker = "AssemblyShadowError AssemblyShadow::GetExecutionDiagnosticsJson(std::string& json)"
        current = module.body(source, marker)
        original = (checks / "source-extracts/GetExecutionDiagnosticsJson.function.cpp").read_text()
        if digest(current.encode()) != CURRENT_HASH or digest(original.encode()) != FROZEN_HASH:
            raise RuntimeError("Original or current function no longer matches the reviewed source")
        if not all(token in current for token in CONTRACT) or any(token in original for token in CONTRACT[:3]):
            raise RuntimeError("Execution producer structure not reviewed")
        stub, sample = fixture_strings((checks / "test_diagnostic_size.py").read_text())
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if not compiler:
            raise RuntimeError("Configured native C++ compiler unavailable")
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1",
                   ASAN_OPTIONS=("abort_on_error=1:detect_leaks=0" if sys.platform == "darwin"
                                 else "abort_on_error=1:detect_leaks=1"),
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        original_full = (historical / "strict.json").read_bytes()
        if len(original_full) != 331078 or original_full != (historical / "sanitized.json").read_bytes():
            raise RuntimeError("Immutable N current-run historical producer output mismatch")
        for profile, flags in (
            ("strict", []),
            ("sanitized", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]),
        ):
            for variant in ("n-large", "empty", "debug-class-flags"):
                vm, scenario, expected_count = fixture(stub, sample, variant)
                for impl, function in (("frozen", original), ("current", current)):
                    name = profile + "-" + variant + "-" + impl
                    unit = out / (name + ".cpp")
                    # Same historical synthetic VM types, main, and compile flags.
                    # The new header is included only for the changed formatter.
                    prologue = ('#include "AssemblyShadowExecutionSerializer.h"\n'
                                if impl == "current" else "")
                    using = "\nusing namespace il2cpp::vm;\n" if impl == "current" else ""
                    unit.write_text(prologue + vm + using + function + "\n" + scenario)
                    binary = out / (name + ".exe")
                    flags_common = [compiler, "-std=c++17", "-O1", "-g", "-Wall",
                                    "-Wextra", "-Werror", "-pedantic", *flags]
                    if impl == "current":
                        flags_common += ["-iquote", str(native / "libil2cpp/vm")]
                    process([*flags_common, str(unit), "-o", str(binary)], name + "-compile", out, rows, env)
                    process([str(binary), str(out / (name + ".json"))], name + "-run", out, rows, env)
                prev = (out / (profile + "-" + variant + "-frozen.json")).read_bytes()
                changed = (out / (profile + "-" + variant + "-current.json")).read_bytes()
                if prev != changed:
                    raise RuntimeError("Immutable original/new execution JSON differs: " + profile + "/" + variant)
                parsed = json.loads(changed)
                if (len(parsed["classes"]) != expected_count or parsed["stateCode"] != 9
                        or parsed["schemaVersion"] != 1 or "r02" in parsed):
                    raise RuntimeError("Synthetic current function output contract changed")
                if variant == "n-large" and changed != original_full:
                    raise RuntimeError("Fresh frozen/current source differs from original N golden output")
                if variant == "empty" and parsed["classes"]:
                    raise RuntimeError("Empty inventory not represented")
                if variant == "debug-class-flags":
                    row = parsed["classes"][0]
                    if (row["staticStoragePointer"] != "0x1234" or not row["pointerDetailsAvailable"]
                            or row["executionModeCode"] != 1 or row["isActive"]):
                        raise RuntimeError("Debug/cctor/baseline scenario was not exercised")
                cases.append({"profile": profile, "variant": variant,
                              "producer": "Actual pinned original and current function bodies",
                              "mockClassCount": expected_count, "bytes": len(changed),
                              "sha256": digest(changed), "exactByteParity": True})
        if len(cases) != 6 or len(rows) != 24:
            raise RuntimeError("Incomplete original/current compiler and process profile matrix")
        complete = True
    except Exception as exc:
        error = repr(exc)
        raise
    finally:
        report = {"kind": "CurrentExecutionProducerVsImmutableN",
                  "result": "PassedHostOnly" if complete else "FailedOrIncomplete",
                  "sourceBindings": source_receipts, "oldFunctionSha256": FROZEN_HASH,
                  "currentFunctionSha256": CURRENT_HASH,
                  "historicalMockOnly": True, "currentProductionFunctionBody": True,
                  "immutableHistoricalSourceUntouched": True, "cases": cases,
                  "commands": rows, "error": error,
                  "liveVmCaptureProven": False, "unityPlayerRun": False,
                  "runtimeAcceptance": False}
        (out / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
        entries = [{"path": str(p.relative_to(out)), "bytes": p.stat().st_size,
                    "sha256": digest(p.read_bytes())} for p in sorted(out.rglob("*")) if p.is_file()]
        (out / "MANIFEST.json").write_text(json.dumps({"files": entries}, indent=2) + "\n")


if __name__ == "__main__":
    main()
