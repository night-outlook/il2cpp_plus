#!/usr/bin/env python3
"""Host tests for exact-source refusal and factual early-failure receipts.

Copies only this runner's declared source inputs to fresh temporary trees;
never changes a checkout or runs Unity. These tests do not compile the VM.
"""
from __future__ import annotations
import ast
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
RUNNER = HERE / "run_bounded_first_use.py"
spec = importlib.util.spec_from_file_location("first_use_runner_contract", RUNNER)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def declared_paths() -> list[str]:
    """Use the actual runner input list, rather than a parallel file contract."""
    tree = ast.parse(RUNNER.read_text())
    main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "main")
    matches = [n.value for n in ast.walk(main) if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == "paths" for t in n.targets)]
    if len(matches) != 1:
        raise RuntimeError("Ambiguous declared runner source inputs")
    result = ast.literal_eval(matches[0])
    if not isinstance(result, list) or not all(isinstance(p, str) for p in result):
        raise RuntimeError("Unexpected source input list")
    return result


class FirstUseRunnerContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        # macOS /var is a symlink; the runner requires a canonical path.
        self.root = Path(self.temp.name).resolve()
        self.native = self.root / "native"
        self.native.mkdir()
        self.output = self.root / "evidence"

    def tearDown(self):
        self.temp.cleanup()

    def copy_sources(self):
        for relative in declared_paths():
            target = self.native / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO / relative, target)

    def invoke(self):
        argv = [str(RUNNER), "--native", str(self.native), "--output", str(self.output)]
        with mock.patch.object(sys, "argv", argv), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            runner.main()

    def assert_failed_ledger(self, text: str):
        record = json.loads((self.output / "RESULT.json").read_text())
        self.assertEqual(record["result"], "FailedOrIncomplete")
        self.assertFalse(record["compiledActualProductionMethod"])
        self.assertEqual(record["profilesCompleted"], [])
        self.assertEqual(record["commands"], [])
        self.assertIn(text, record["firstError"])
        self.assertFalse(record["classBearingSourceContractPassedUnderMocks"])
        self.assertFalse(record["semanticAcceptance"])
        manifest = json.loads((self.output / "MANIFEST.json").read_text())
        for item in manifest["files"]:
            data = (self.output / item["path"]).read_bytes()
            self.assertEqual(item["bytes"], len(data))
            self.assertEqual(item["sha256"], runner.sha(data))
        self.assertIn("RESULT.json", [x["path"] for x in manifest["files"]])

    def test_actual_method_matches_reviewed_contract(self):
        actual = runner.extract((REPO / "libil2cpp/vm/AssemblyShadow.cpp").read_text())
        runner.verify_method(actual)
        self.assertEqual(runner.sha(actual.encode()), runner.EXPECTED_METHOD_SHA256)

    def test_changed_metadata_operation_is_rejected(self):
        actual = runner.extract((REPO / "libil2cpp/vm/AssemblyShadow.cpp").read_text())
        changed = actual.replace("klass->namespaze,klass->name", "klass->name,klass->namespaze")
        self.assertNotEqual(changed, actual)
        with self.assertRaisesRegex(RuntimeError, "source body changed"):
            runner.verify_method(changed)

    def test_duplicate_or_unbalanced_method_is_rejected(self):
        actual = runner.extract((REPO / "libil2cpp/vm/AssemblyShadow.cpp").read_text())
        for changed in (actual + "\n" + actual, actual[:-1]):
            with self.subTest(source=changed[-50:]), self.assertRaises(RuntimeError):
                runner.extract(changed)

    def test_missing_input_has_factual_failed_ledger(self):
        with self.assertRaises(OSError):
            self.invoke()
        self.assert_failed_ledger("FileNotFoundError")

    def test_changed_body_has_factual_failed_ledger(self):
        self.copy_sources()
        source = self.native / "libil2cpp/vm/AssemblyShadow.cpp"
        source.write_text(source.read_text().replace("klass->namespaze,klass->name", "klass->name,klass->namespaze"))
        with self.assertRaisesRegex(RuntimeError, "source body changed"):
            self.invoke()
        self.assert_failed_ledger("source body changed")

    def test_missing_declaration_has_factual_failed_ledger(self):
        self.copy_sources()
        (self.native / "libil2cpp/vm/AssemblyShadow.h").write_text("// deliberately missing API\n")
        with self.assertRaisesRegex(RuntimeError, "declaration missing"):
            self.invoke()
        self.assert_failed_ledger("declaration missing")

    def test_missing_compiler_does_not_claim_source_compilation(self):
        self.copy_sources()
        with mock.patch.dict(os.environ, {"CXX": "__missing_r03_compiler_20261016__"}), \
                self.assertRaisesRegex(RuntimeError, "No host compiler"):
            self.invoke()
        self.assert_failed_ledger("No host compiler")

    def test_consumed_output_is_not_reused_or_modified(self):
        self.output.mkdir()
        sentinel = self.output / "sentinel"
        sentinel.write_bytes(b"preserved")
        with self.assertRaisesRegex(RuntimeError, "external output path"):
            self.invoke()
        self.assertEqual(sentinel.read_bytes(), b"preserved")
        self.assertFalse((self.output / "RESULT.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
