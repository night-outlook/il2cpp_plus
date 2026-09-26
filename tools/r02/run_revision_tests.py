#!/usr/bin/env python3
"""Run focused production-header regressions added during R02 continuation."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import run_tests

CASES = ('nested', 'unready', 'publication-failure', 'coverage', 'memo')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitizers', action='store_true')
    args = parser.parse_args()
    root, out = args.root.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    rows, compiles, versions = [], [], []
    compilers = list(dict.fromkeys(filter(None, (shutil.which('g++'), shutil.which('clang++')))))
    for ci, compiler in enumerate(compilers):
        versions.append(run_tests.run([compiler, '--version'], out / ('compiler-' + str(ci))))
        configs = [(std, opt, level, []) for std, opt in (('c++11', '-O0'), ('c++17', '-O2')) for level in (0, 1, 2)]
        if args.sanitizers: configs.append(('c++17', '-O1', 2, ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']))
        for ordinal, (std, opt, level, extra) in enumerate(configs):
            label = f'c{ci}-{ordinal}-L{level}'
            exe = out / (label + '.bin')
            command = [compiler, '-std=' + std, opt, '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-DHYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL=' + str(level), '-I' + str(root / 'libil2cpp'),
                str(root / 'tools/r02/native_revision_tests.cpp'), str(root / 'tools/r02/native_revision_allocator.cpp'), '-o', str(exe)] + extra
            compiled = run_tests.run(command, out / (label + '-compile'))
            compiles.append({'label': label, 'exitCode': compiled['exitCode']})
            if compiled['exitCode']: continue
            for case in CASES:
                result = run_tests.run([str(exe), case], out / (label + '-' + case))
                try: raw = json.loads(result['stdout'])
                except ValueError: raw = {}
                valid = result['exitCode'] == 0 and raw.get('kind') == 'R02NativeRevisionUnit' and raw.get('case') == case and raw.get('level') == level and raw.get('result') == 'Passed' and raw.get('checks', 0) > 0
                rows.append({'label': label, 'case': case, 'result': 'Passed' if valid else 'Failed', 'checks': raw.get('checks', 0)})
    expected = len(compilers) * (6 + int(args.sanitizers)) * len(CASES)
    passed = sum(row['result'] == 'Passed' for row in rows)
    success = bool(compilers) and passed == expected and len(rows) == expected and all(row['exitCode'] == 0 for row in compiles)
    paths = sorted((root / 'libil2cpp/vm').glob('AssemblyShadow*'))
    paths += [Path(__file__), root / 'tools/r02/native_revision_tests.cpp', root / 'tools/r02/native_revision_allocator.cpp', root / 'tools/r02/run_tests.py']
    inventory = [{'path': str(p.relative_to(root)), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest(), 'sizeBytes': p.stat().st_size} for p in paths if p.is_file()]
    summary = {'kind': 'R02NativeRevisionValidation', 'result': 'Passed' if success else ('Failed' if compilers else 'Blocked'),
        'platform': platform.platform(), 'python': sys.version, 'expectedProcessCases': expected, 'passedProcessCases': passed,
        'compilations': compiles, 'cases': rows, 'sourceInventory': inventory, 'runtimeAcceptance': False, 'unityPlayerRun': False}
    (out / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: summary[k] for k in ('kind', 'result', 'expectedProcessCases', 'passedProcessCases', 'runtimeAcceptance')}))
    return 0 if success else 1

if __name__ == '__main__': raise SystemExit(main())
