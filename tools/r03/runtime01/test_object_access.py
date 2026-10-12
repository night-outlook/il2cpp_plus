#!/usr/bin/env python3
"""Compile the real Object.h access surface; mock only dependent type declarations.

This proves NewPtrFree remains private to ordinary and diagnostic callers.
The reporting response is now reserved during healthy Runtime initialization. Neither test case executes Unity or allocates a runtime object.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
PRELUDE = r'''
#include <cstdint>
#include <cstddef>
#define LIBIL2CPP_CODEGEN_API
#define IL2CPP_ASSERT(...) ((void)klass)
struct Il2CppObject {};
struct Il2CppClass {};
namespace il2cpp { namespace vm {} }
'''
FRIEND = r'''
namespace il2cpp { namespace vm { namespace assembly_shadow_reporting {
inline Il2CppObject* FixedHandledResult() noexcept {
    return Object::NewPtrFree(nullptr);
}
}}}
'''
UNRELATED = r'''
Il2CppObject* NotDiagnostic() { return il2cpp::vm::Object::NewPtrFree(nullptr); }
'''

def main():
    source = (ROOT / 'libil2cpp/vm/Object.h').read_bytes()
    compiler = shutil.which('clang++') or shutil.which('g++')
    assert compiler, 'C++ compiler required'
    rows = []
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        for name in ('il2cpp-config.h', 'Class.h', 'ClassInlines.h'):
            (root / name).write_text('#pragma once\n')
        (root / 'Object.h').write_bytes(source)
        cases = [('on-diagnostic-private-denied', 1, FRIEND, False),
                 ('on-unrelated-denied', 1, UNRELATED, False),
                 ('off-header', 0, '', True),
                 ('off-unrelated-denied', 0, UNRELATED, False)]
        for name, enabled, body, should_pass in cases:
            path = root / (name + '.cpp')
            path.write_text(PRELUDE + '\n#include "Object.h"\n' + body)
            cmd = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                   '-fsyntax-only', '-DHYBRIDCLR_ENABLE_ASSEMBLY_SHADOW=' + str(enabled),
                   '-I' + str(root), str(path)]
            run = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            assert (run.returncode == 0) == should_pass, (name, run.stderr)
            if not should_pass:
                assert 'private' in run.stderr and 'NewPtrFree' in run.stderr, run.stderr
            rows.append({'case': name, 'expectedCompilePass': should_pass, 'result': 'Passed'})
        assert 'friend Il2CppObject* assembly_shadow_reporting::FixedHandledResult' not in source.decode()
        rows.append({'case':'diagnostic-private-allocator-access-removed','result':'Passed'})
    print(json.dumps({'kind': 'R03ObjectAccessRegression', 'result': 'Passed',
        'objectHeaderSha256': hashlib.sha256(source).hexdigest(), 'cases': rows,
        'dependencyModel': 'Actual Object.h; dependent type stubs only', 'unityRun': False}, sort_keys=True))

if __name__ == '__main__':
    main()
