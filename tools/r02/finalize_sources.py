#!/usr/bin/env python3
"""Reproduce and verify final R02 integration from immutable H1 source.

--verify is read-only. --prepare is Primary-only object preparation; Local must
never apply it to pass validation. Neither mode updates a Git ref or source pin.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import source_edits as recipe


def memoize(text: str) -> str:
    text = recipe.once(text, '#include "AssemblyShadowR02Diagnostics.h"',
        '#include "AssemblyShadowR02Diagnostics.h"\n#include "AssemblyShadowObservationMemo.h"')
    def edit(body: str) -> str:
        body = recipe.once(body,
            '        if (!klass || !klass->image || !klass->image->assembly) return;',
            '''        if (!klass || !klass->image || !klass->image->assembly) return;
        if (assembly_shadow_r02::ObservationMemo::Contains(klass))
        {
            assembly_shadow_r02::ObservationCounters::Add(
                assembly_shadow_r02::Metric::ObservationMemoHits, 1);
            return;
        }''')
        body = recipe.once(body, '            if (entry == klass) return;',
            '''            if (entry == klass)
            {
                assembly_shadow_r02::ObservationMemo::Remember(klass);
                return;
            }''')
        body = recipe.once(body, '                ++s_executionClassCount;\n                return;',
            '''                ++s_executionClassCount;
                assembly_shadow_r02::ObservationMemo::Remember(klass);
                return;''')
        # The dropped path is intentionally not memoized. Repeated drops remain visible.
        return body
    return recipe.replace_function(text, '    void ObserveExecutionClass(Il2CppClass* klass)', edit)


def expected_sources(root: Path) -> tuple[dict, dict]:
    output, safety = {}, {}
    for name, expected_blob in recipe.INPUTS.items():
        raw = subprocess.check_output(['git', '-C', str(root), 'show', recipe.BASE + ':' + name])
        if recipe.blob(raw) != expected_blob:
            raise ValueError('Immutable H1 source blob mismatch: ' + name)
        before = raw.decode('utf-8')
        after = memoize(recipe.assembly(before)) if name.endswith('/AssemblyShadow.cpp') else recipe.resolver(before)
        if name.endswith('/AssemblyShadow.cpp'):
            for signature in recipe.SAFETY_BODIES:
                a, b = recipe.function_span(before, signature)
                c, d = recipe.function_span(after, signature)
                if before[a:b] != after[c:d]:
                    raise ValueError('R02 changed a protected correctness guard: ' + signature)
                safety[signature] = hashlib.sha256(before[a:b].encode()).hexdigest()
        output[name] = after.encode('utf-8')
    return output, safety


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--verify', action='store_true')
    mode.add_argument('--prepare', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    receipt = {'kind': 'R02FinalSourceVerification', 'result': 'Failed',
        'readOnly': args.verify, 'runtimeAcceptance': False, 'files': []}
    try:
        sources, safety = expected_sources(root)
        mismatch = []
        for name, raw in sources.items():
            path = root / name
            matches = path.is_file() and path.read_bytes() == raw
            if args.prepare: path.write_bytes(raw)
            if not matches: mismatch.append(name)
            receipt['files'].append({'path': name, 'gitBlob': recipe.blob(raw),
                'sha256': hashlib.sha256(raw).hexdigest(), 'committedBytesMatchedBefore': matches})
        receipt['protectedGuardHashes'] = safety
        receipt['result'] = ('Passed' if not mismatch else 'Failed') if args.verify else 'PreparedNotCommitted'
        receipt['mismatchedPaths'] = mismatch
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        receipt['error'] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as f: f.write(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, separators=(',', ':')))
    return 0 if receipt['result'] in ('Passed', 'PreparedNotCommitted') else 1

if __name__ == '__main__': raise SystemExit(main())
