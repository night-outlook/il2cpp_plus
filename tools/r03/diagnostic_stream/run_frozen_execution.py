#!/usr/bin/env python3
"""Owned execution/R02 host contracts. No SDK, VM class acquisition or Player.

One invocation executes diagnostics levels 0/1/2, strict and ASan/UBSan.
Results bind exact current input bytes and unchanged legacy reference headers.
A content-only source copy is explicitly distinguished from a Git checkout.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

HEADERS = (
    'AssemblyShadowFrozenExecutionRecord.h', 'AssemblyShadowR02Snapshot.h',
    'AssemblyShadowFrozenR02Serializer.h',
    'AssemblyShadowR02Diagnostics.h', 'AssemblyShadowExecutionSerializer.h',
    'AssemblyShadowDiagnosticWriter.h', 'AssemblyShadowObservationCounters.h',
    'AssemblyShadowObservationMemo.h', 'AssemblyShadowDiagnosticsProfile.h',
)
REFERENCES = {
    'AssemblyShadowExecutionSerializer.h': '30c78cf0ac30f7f1e59f573b950f594ac2379afaa372f544b9fdcaa26cc608ef',
    'AssemblyShadowR02Diagnostics.h': 'b42e61cbd65dc26f2f9c207c318697b46d0c909e7d83f1e2b8b4b4e109ce56ba',
}
TESTS = (
    'snapshot-all-fields-and-read-order', 'every-metric-change-clears-destination',
    'every-source-metadata-change', 'invalid-subsets-rejected', 'disabled-policy',
    'coverage-precedence', 'empty-original-v1-parity', '256-boolean-row-original-v1-parity',
    'counted-nonterminated-state', 'unicode-and-control-byte-parity', 'owned-inputs-survive-mutation',
    'invalid-utf8-rejected', 'invalid-null-inputs', 'missing-extra-and-overlimit-rows',
    '1024-row-original-v1-parity', 'wrong-stages-and-reuse', 'whole-object-input-alias',
    'invalid-range-before-dereference', 'exact-field-byte-limit', 'one-byte-over-field-limit',
    'escape-expanded-byte-limit', 'nonclassic-locale-rejected', 'incomplete-counters-rejected',
    'late-source-revocation', 'sink-preflight-no-callback', 'bounded-chunked-emission',
    'sink-failure-no-continuation', 'reentrant-sink-rejected', 'revoking-sink-rejected',
    'writer-finish-contract-unchanged', 'zero-watched-operator-new',
    'actual-r02-source-and-live-wrapper-parity', 'actual-concurrent-counter-source',
    'actual-counter-saturation', 'actual-counter-thread-capacity',
)

def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()

def write(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True)+'\n', encoding='utf-8')

def source_identity(native: Path) -> dict:
    if not (native/'.git').exists():
        return {'mode': 'ContentHashOnly', 'gitHead': None, 'gitClean': None}
    def git(*args: str) -> str:
        proc = subprocess.run(['git', '-C', str(native), *args], capture_output=True, timeout=30)
        if proc.returncode:
            raise RuntimeError('Git source identity unavailable: '+repr(args))
        return proc.stdout.decode('utf-8').strip()
    return {'mode': 'GitCheckout', 'gitHead': git('rev-parse', 'HEAD'),
            'gitBranch': git('branch', '--show-current'),
            'gitClean': not bool(git('status', '--porcelain')), 'origin': git('remote', 'get-url', 'origin')}

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        raise RuntimeError('Python -O is forbidden')
    native, output = args.native, args.output
    if (not native.is_absolute() or native != native.resolve(strict=True) or not native.is_dir()
        or not output.is_absolute() or output != output.resolve(strict=False)
        or output.exists() or not output.parent.is_dir()
        or native == output or native in output.parents or output in native.parents):
        raise RuntimeError('Canonical source and unused external output with existing parent required')
    fixture = native/'tools/r03/diagnostic_stream/frozen_execution_fixture.cpp'
    paths = [native/'libil2cpp/vm'/name for name in HEADERS]
    allocations = fixture.parent/'frozen_execution_allocations.cpp'
    paths += [fixture, allocations, native/'tools/r03/diagnostic_stream/run_frozen_execution.py']
    paths += [fixture.parent/'reference'/name for name in REFERENCES]
    def hashes() -> dict:
        return {str(p.relative_to(native)): digest(p.read_bytes()) for p in paths}
    for name, expected in REFERENCES.items():
        if digest((fixture.parent/'reference'/name).read_bytes()) != expected:
            raise RuntimeError('Original reference bytes changed: '+name)
    before = hashes()
    identity = source_identity(native)
    compiler = shutil.which(os.environ.get('CXX', 'clang++'))
    if not compiler:
        raise RuntimeError('CXX compiler executable unavailable')
    output.mkdir(mode=0o700)
    # GCC may treat identical #pragma-once files as the same header. Prefix a
    # generated comment, retaining EVERY original source byte, so the renamed
    # reference is compiled independently. Repository reference files stay exact.
    reference_adapters = {}
    for name in REFERENCES:
        original = (fixture.parent/'reference'/name).read_bytes()
        generated = output/('reference-'+name)
        prefix = b'// Independent legacy reference; original bytes follow unchanged.\n'
        generated.write_bytes(prefix+original)
        reference_adapters[name] = {'path': generated.name, 'sha256': digest(prefix+original),
                                    'originalSha256': digest(original), 'onlyAddedCommentPrefix': True}
    rows, profiles = [], []
    result = {'kind': 'R03P1FrozenExecutionHost', 'schemaVersion': 1, 'result': 'FailedOrIncomplete',
              'platform': platform.platform(), 'python': sys.version,
              'sourceIdentity': identity, 'sourceSha256Before': before,
              'legacyReferenceCommit': '5667e3712f49e4b38c7dd7359e57f81e344626da',
              'referenceSha256': REFERENCES, 'referenceAdapters': reference_adapters, 'commands': rows, 'profiles': profiles,
              'actualR02CounterSourceExercised': False, 'classRowsAreHostFixtures': True,
              'coherentVmEpochProven': False, 'classPointerOriginProven': False,
              'originalLiveGettersReplaced': False, 'officialSdkCompile': False,
              'unityEditorRun': False, 'UnityPlayerExecuted': False, 'runtimeAcceptance': False,
              'leakSanitizer': 'UnavailableOnAppleHost' if sys.platform == 'darwin' else 'EnabledOnLinuxHost'}
    env = dict(os.environ)
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    # Apple ASan does not implement LeakSanitizer. Do not disable Linux leak checking.
    if sys.platform == 'darwin':
        env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
    else:
        env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
    result['sanitizerEnvironment'] = {k: env[k] for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS')}
    def command(label: str, argv: list[str], timeout: int) -> tuple[dict, bytes]:
        record = {'label': label, 'argv': argv, 'startedUnix': time.time(), 'status': 'Failed'}
        stdout, stderr = b'', b''
        try:
            proc = subprocess.run(argv, capture_output=True, timeout=timeout, env=env)
            stdout, stderr = proc.stdout, proc.stderr
            record.update(exit=proc.returncode, status='Passed' if proc.returncode == 0 else 'Failed')
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or b'', error.stderr or b''
            record['exception'] = repr(error)
        except OSError as error:
            record['exception'] = repr(error)
        for suffix, raw in (('stdout.bin', stdout), ('stderr.bin', stderr)):
            name = label+'.'+suffix
            (output/name).write_bytes(raw)
            record[suffix] = {'path': name, 'bytes': len(raw), 'sha256': digest(raw)}
        record['finishedUnix'] = time.time()
        rows.append(record)
        write(output/(label+'.json'), record)
        return record, stdout
    try:
        version, _ = command('compiler-version', [compiler, '--version'], 30)
        if version['status'] != 'Passed':
            raise RuntimeError('Compiler version unavailable')
        for level in (0, 1, 2):
            for sanitized in (False, True):
                label = 'level%d-%s' % (level, 'sanitized' if sanitized else 'strict')
                executable = output/label
                extra = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitized else []
                argv = [compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pedantic', '-pthread',
                        '-DHYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL='+str(level),
                        # Restrict vm include lookup to quoted headers. An -I entry
                        # shadows system <string.h> with vm/string.h on macOS libc++.
                        '-iquote', str(native/'libil2cpp/vm'),
                        '-DFROZEN_EXECUTION_REFERENCE_HEADER=\"'+str(output/'reference-AssemblyShadowExecutionSerializer.h')+'\"',
                        '-DFROZEN_R02_REFERENCE_HEADER=\"'+str(output/'reference-AssemblyShadowR02Diagnostics.h')+'\"',
                        str(fixture), str(allocations), *extra, '-o', str(executable)]
                compiled, _ = command(label+'-compile', argv, 180)
                profile = {'name': label, 'diagnosticsLevel': level, 'sanitized': sanitized,
                           'result': 'Failed', 'tests': [], 'compileReceipt': label+'-compile.json'}
                profiles.append(profile)
                if compiled['status'] != 'Passed':
                    profile['run'] = 'NotRun'
                    continue
                executed, raw = command(label+'-run', [str(executable)], 180)
                text = raw.decode('utf-8', errors='strict')
                found = re.findall(r'^PASS (.+)$', text, re.MULTILINE)
                summary = re.findall(r'^SUMMARY tests=(\d+) diagnosticsLevel=(\d+) runtimeAcceptance=false$', text, re.MULTILINE)
                allocation = re.findall(r'^OBSERVATION watchedOperatorNew=(\d+) snapshotBytes=(\d+) recordBytes=(\d+)$', text, re.MULTILINE)
                profile.update(tests=found, runReceipt=label+'-run.json', allocation=allocation)
                passed = (executed['status'] == 'Passed' and tuple(found) == TESTS
                          and summary == [(str(len(TESTS)), str(level))] and len(allocation) == 1
                          and allocation[0][0] == '0' and int(allocation[0][1]) <= 512
                          and int(allocation[0][2]) <= 3932160+768)
                profile['result'] = 'Passed' if passed else 'Failed'
    except Exception as error:
        result['error'] = repr(error)
    finally:
        try:
            result['sourceSha256After'] = hashes()
            result['sourceIdentityAfter'] = source_identity(native)
            result['sourcePreserved'] = result['sourceSha256After'] == before and result['sourceIdentityAfter'] == identity
        except Exception as error:
            result['sourcePreserved'] = False
            result['postflightError'] = repr(error)
        if (result['sourcePreserved'] and len(profiles) == 6
            and all(p['result'] == 'Passed' for p in profiles)
            and len(rows) == 13 and all(r['status'] == 'Passed' for r in rows)):
            result['result'] = 'PassedWithinOwnedExecutionHostScope'
        result['actualR02CounterSourceProfiles'] = [p['name'] for p in profiles
            if 'actual-r02-source-and-live-wrapper-parity' in p['tests']]
        result['actualR02CounterSourceExercised'] = bool(result['actualR02CounterSourceProfiles'])
        result['testGroupsExecuted'] = sum(len(p['tests']) for p in profiles)
        write(output/'RESULT.json', result)
        files = [{'path': p.name, 'bytes': p.stat().st_size, 'sha256': digest(p.read_bytes())}
                 for p in sorted(output.iterdir()) if p.is_file() and not p.is_symlink()]
        write(output/'MANIFEST.json', {'files': files})
    print(json.dumps({'result': result['result'], 'profiles': len(profiles),
                      'groups': result['testGroupsExecuted'], 'runtimeAcceptance': False}))
    if result['result'] != 'PassedWithinOwnedExecutionHostScope':
        raise SystemExit(1)

if __name__ == '__main__':
    main()
