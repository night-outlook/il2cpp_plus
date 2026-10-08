#!/usr/bin/env python3
"""Execute production terminal predicates using host metadata doubles."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
CASES = ('healthy','terminal','latched','generic','shapes','malformed','declaring',
         'no-allocation','first-failure','initializer','private-scope','empty-world')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();root=a.root.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    compilers=list(dict.fromkeys(filter(None,(shutil.which('clang++'),shutil.which('g++')))))
    rows=[]
    for i,cc in enumerate(compilers):
        for j,flags in enumerate((['-std=c++11','-O0'],['-std=c++17','-O2'],['-std=c++17','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
            label=f'{i}-{j}';exe=out/label
            command=[cc,*flags,'-Wall','-Wextra','-Werror','-pthread','-I'+str(root/'libil2cpp'),str(root/'tools/r03_ir/terminal_tests.cpp'),'-o',str(exe)]
            r=subprocess.run(command,capture_output=True,text=True,timeout=120)
            (out/(label+'-compile.log')).write_text(r.stdout+r.stderr)
            rows.append({'phase':'compile','argv':command,'exit':r.returncode})
            if r.returncode:continue
            for case in CASES:
                r=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=30)
                (out/(label+'-'+case+'.log')).write_text(r.stdout+r.stderr)
                value=json.loads(r.stdout) if r.returncode==0 else {}
                rows.append({'phase':'case','profile':label,'case':case,'exit':r.returncode,'observation':value})
    success=bool(compilers) and len([x for x in rows if x['phase']=='case'])==len(compilers)*3*len(CASES) and all(x['exit']==0 for x in rows)
    files=['libil2cpp/vm/AssemblyShadowTerminalExecution.inc','tools/r03_ir/terminal_tests.cpp','tools/r03_ir/run_terminal_tests.py']
    report={'kind':'R03IRTerminalHostValidation','result':'Passed' if success else 'Failed','cases':rows,
      'platform':platform.platform(),'python':sys.version,'sources':{f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},
      'runtimeAcceptance':False,'unityPlayerRun':False,'metadataRecords':'Host test doubles, not real MethodInfo'}
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'result':report['result'],'executed':sum(x['phase']=='case' for x in rows)}));return 0 if success else 1
if __name__=='__main__':raise SystemExit(main())
