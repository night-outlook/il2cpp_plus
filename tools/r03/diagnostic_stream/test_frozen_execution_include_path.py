#!/usr/bin/env python3
"""Host regression: -iquote resolves VM headers without shadowing C system string.h.

No Unity SDK, native Player, or VM lifetime/epoch acceptance is exercised.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
RUNNER = HERE / 'run_frozen_execution.py'


class QuotedIncludeRegression(unittest.TestCase):
    def test_actual_runner_confines_vm_include_path_to_quoted_lookup(self):
        source = RUNNER.read_text(encoding='utf-8')
        self.assertEqual(source.count("'-iquote', str(native/'libil2cpp/vm')"), 1)
        self.assertNotIn("'-I'+str(native/'libil2cpp/vm')", source)

    def test_cstring_remains_system_header_and_vm_quotes_still_work(self):
        compiler = shutil.which(os.environ.get('CXX', 'clang++'))
        self.assertIsNotNone(compiler, 'Host C++ compiler is required; do not skip')
        with tempfile.TemporaryDirectory(prefix='r03-vm-include-') as directory:
            root = Path(directory)
            vm = root / 'vm'
            vm.mkdir()
            # On macOS libc++, <cstring> pulls system <string.h>.
            # -I incorrectly selects this VM header and fails the compiler.
            (vm / 'string.h').write_text('#error VM_STRING_H_SHADOWED_STANDARD_STRING_H\n')
            (vm / 'OwnedQuotedHeader.h').write_text('#define QUOTED_VM_HEADER_FOUND 1\n')
            probe = root / 'probe.cpp'
            probe.write_text(
                '#include <cstring>\n'
                '#include "OwnedQuotedHeader.h"\n'
                'int main() { return QUOTED_VM_HEADER_FOUND == 1 && '
                'std::strlen("a") == 1 ? 0 : 1; }\n'
            )
            base = [compiler, '-std=c++17', '-fsyntax-only']
            scoped = subprocess.run([*base, '-iquote', str(vm), str(probe)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(scoped.returncode, 0, scoped.stderr)
            # Prove the fixture detects the exact old collision, not merely
            # that an unrelated positive translation unit compiles.
            legacy = subprocess.run([*base, '-I' + str(vm), str(probe)],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(legacy.returncode, 0)
            self.assertIn('VM_STRING_H_SHADOWED_STANDARD_STRING_H', legacy.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
