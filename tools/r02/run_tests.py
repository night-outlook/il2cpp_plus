#!/usr/bin/env python3
"""Run the production R02 header tests. No Unity/Player acceptance is claimed."""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

CASES = ('basic', 'failures', 'concurrent', 'counters', 'saturation')


def run(argv: list[str], folder: Path, timeout: int = 120) -> dict:
    folder.mkdir(parents=True, exist_ok=False)
    start = dt.datetime.now(dt.timezone.utc).isoformat()
    begin = time.monotonic()
    try:
        p = subprocess.run(argv, text=True, capture_output=True, timeout=timeout)
        code, stdout, stderr = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as error:
        code = -1
        stdout = (error.stdout or b'').decode(errors='replace') if isinstance(error.stdout, bytes) else error.stdout or ''
        stderr = 'Timeout\n' + ((error.stderr or b'').decode(errors='replace') if isinstance(error.stderr, bytes) else error.stderr or '')
    (folder/'stdout.txt').write_text(stdout)
    (folder/'stderr.txt').write_text(stderr)
    receipt = {'argv': argv, 'startedUtc': start, 'durationSeconds': time.monotonic()-begin,
        'exitCode': code, 'stdoutSha256': hashlib.sha256(stdout.encode()).hexdigest(),
        'stderrSha256': hashlib.sha256(stderr.encode()).hexdigest()}
    (folder/'command.json').write_text(json.dumps(receipt, indent=2)+'\n')
    return dict(receipt, stdout=stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--compiler', choices=('g++', 'clang++'), help='Restrict host execution; default runs both available compilers')
    parser.add_argument('--standard', choices=('c++11', 'c++17'), help='Restrict host execution; default runs both language modes')
    args = parser.parse_args()
    root, out = args.root.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    compilers = [shutil.which(x) for x in ((args.compiler,) if args.compiler else ('g++', 'clang++'))]
    compilers = list(dict.fromkeys(x for x in compilers if x))
    rows, compile_rows = [], []
    if not compilers:
        (out/'results.json').write_text(json.dumps({'result':'Blocked','reason':'No C++ compiler','runtimeAcceptance':False})+'\n')
        return 2
    for compiler_index, compiler in enumerate(compilers):
        version = run([compiler, '--version'], out/f'compiler-{compiler_index}')
        for standard, optimization in (('c++11','-O0'),('c++17','-O2')):
            if args.standard and standard != args.standard: continue
            for level in (0,1,2):
                label = f'c{compiler_index}-{standard}-L{level}'
                executable = out/(label+'.bin')
                command = [compiler, '-std='+standard, optimization, '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
                    '-DHYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL='+str(level), '-I'+str(root/'libil2cpp'),
                    str(root/'tools/r02/native_tests.cpp'), str(root/'tools/r02/native_other_tu.cpp'), '-o', str(executable)]
                compiled = run(command, out/(label+'-compile'))
                compile_rows.append({'label':label,'exitCode':compiled['exitCode']})
                if compiled['exitCode']: continue
                for case in CASES:
                    receipt = run([str(executable),case], out/(label+'-'+case))
                    try: payload = json.loads(receipt['stdout'])
                    except (ValueError, TypeError): payload = {}
                    valid = receipt['exitCode']==0 and payload.get('kind')=='R02NativeUnit' and payload.get('result')=='Passed' and payload.get('case')==case and payload.get('level')==level and payload.get('checks',0)>0
                    rows.append({'label':label,'case':case,'result':'Passed' if valid else 'Failed','checks':payload.get('checks',0)})
        if args.sanitizers:
            label = f'c{compiler_index}-asan-ubsan'
            executable = out/(label+'.bin')
            command = [compiler,'-std=c++17','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                '-I'+str(root/'libil2cpp'),str(root/'tools/r02/native_tests.cpp'),str(root/'tools/r02/native_other_tu.cpp'),'-o',str(executable)]
            compiled = run(command,out/(label+'-compile'))
            compile_rows.append({'label':label,'exitCode':compiled['exitCode']})
            if compiled['exitCode']==0:
                for case in CASES:
                    receipt=run([str(executable),case],out/(label+'-'+case))
                    try: payload=json.loads(receipt['stdout'])
                    except (ValueError,TypeError): payload={}
                    valid=receipt['exitCode']==0 and payload.get('result')=='Passed' and payload.get('case')==case and payload.get('checks',0)>0
                    rows.append({'label':label,'case':case,'result':'Passed' if valid else 'Failed','checks':payload.get('checks',0)})
    expected = len(compilers) * ((3 if args.standard else 6) + int(args.sanitizers)) * len(CASES)
    passed = sum(x['result']=='Passed' for x in rows)
    success = passed==expected and len(rows)==expected and all(x['exitCode']==0 for x in compile_rows)
    inventory=[]
    for path in sorted(list((root/'tools/r02').glob('*.cpp')) + list((root/'libil2cpp/vm').glob('AssemblyShadow*Proof.h')) +
        [root/'libil2cpp/vm/AssemblyShadowAdmissionCache.h',root/'libil2cpp/vm/AssemblyShadowObservationCounters.h',root/'libil2cpp/vm/AssemblyShadowR02Diagnostics.h']):
        raw=path.read_bytes(); inventory.append({'path':str(path.relative_to(root)),'sha256':hashlib.sha256(raw).hexdigest(),'sizeBytes':len(raw)})
    summary={'kind':'R02PrimaryNativeValidation','result':'Passed' if success else 'Failed','platform':platform.platform(),
        'python':sys.version,'compilerFilter':args.compiler,'standardFilter':args.standard,'expectedProcessCases':expected,'passedProcessCases':passed,'failedProcessCases':len(rows)-passed,
        'compilations':compile_rows,'cases':rows,'sourceInventory':inventory,'runtimeAcceptance':False,'unityPlayerRun':False,
        'scope':'Production cache/proof/counter headers with synthetic physical classes; not IL2CPP VM integration'}
    (out/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:summary[k] for k in ('kind','result','expectedProcessCases','passedProcessCases','failedProcessCases','runtimeAcceptance')}))
    return 0 if success else 1


if __name__=='__main__': raise SystemExit(main())
