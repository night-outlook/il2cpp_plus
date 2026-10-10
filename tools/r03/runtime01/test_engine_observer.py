#!/usr/bin/env python3
"""Test the actual native observer transport with mocked VM state and completion.
Not an engine-originated Unity execution; that requires the separate Players.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reporting import STUB, ROOT


def main():
    cc = shutil.which('clang++') or shutil.which('g++')
    assert cc
    stub = STUB.replace('#include <cstdint>', '#include <cstdint>\n#include <string>')
    stub = stub.replace('enum class AssemblyShadowState', 'enum class AssemblyShadowError { Success=0 };\nenum class AssemblyShadowState')
    stub = stub.replace('static void GetState(AssemblyShadowState& s) { s=denied?AssemblyShadowState::FailedAfterCommit:AssemblyShadowState::Committed; }',
        'static AssemblyShadowError GetState(AssemblyShadowState& s) { s=denied?AssemblyShadowState::FailedAfterCommit:AssemblyShadowState::Committed; return AssemblyShadowError::Success; }\n'
        'static AssemblyShadowError GetRecoveryInfoJson(std::string& json) { json=R"({"published":true,"stateCode":9,"terminalFailureCode":13,"disposition":"RestartRequired"})"; return AssemblyShadowError::Success; }')
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / 'stub.h').write_text(stub)
        for name in ('il2cpp-config.h','il2cpp-class-internals.h','il2cpp-object-internals.h',
                     'il2cpp-tabledefs.h','vm/AssemblyShadow.h','vm/Image.h',
                     'vm/MetadataCache.h','vm/Object.h','vm/Class.h'):
            p = root / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text('#include "stub.h"\n')
        header = ROOT / 'libil2cpp/vm/AssemblyShadowTerminalReporting.h'
        (root / 'vm/AssemblyShadowTerminalReporting.h').write_text('#include "' + str(header) + '"\n')
        source = ROOT / 'libil2cpp/vm/AssemblyShadowEngineReportingProbe.cpp'
        main_source = r'''
#include "stub.h"
#define IL2CPP_EXPORT
#include "PROBE_PATH"
#include <cassert>
int main(int argc, char** argv) {
 assert(argc == 3);
 const char* run="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 const char* sha="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
 R03_IR_EngineLoggerCalled();
 if(!std::strcmp(argv[2],"invalid")) {
  assert(R03_IR_ArmEngineWitness(argv[1],"short", "test",sha)==-1); return 0;
 }
 if(!std::strcmp(argv[2],"existing")) {
  assert(R03_IR_ArmEngineWitness(argv[1],run, "test",sha)==-3); return 0;
 }
 assert(R03_IR_ArmEngineWitness(argv[1],run,"test",sha)==1);
 denied=true;
 if(!std::strcmp(argv[2],"logger")) R03_IR_EngineLoggerCalled();
 il2cpp::vm::assembly_shadow_reporting::CompletedCounter().fetch_add(1,std::memory_order_release);
 for(;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}
'''.replace('PROBE_PATH', str(source))
        (root / 'main.cpp').write_text(main_source)
        exe = root / 'observer'
        cmd = [cc, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-pthread',
               '-DHYBRIDCLR_R03_RUNTIME_PROBE=1','-DHYBRIDCLR_ENABLE_ASSEMBLY_SHADOW=1',
               '-I' + str(root), str(root / 'main.cpp'), '-o', str(exe)]
        if os.environ.get('R03_SANITIZERS') == '1':
            cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(cmd, check=True)
        for mode, expected in [('positive',0), ('logger',71), ('invalid',0), ('existing',0)]:
            out = root / (mode + '.json')
            if mode == 'existing': out.write_text('unchanged')
            run = subprocess.run([str(exe),str(out),mode],capture_output=True,text=True,timeout=15)
            assert run.returncode == expected,(mode,run.returncode,run.stderr)
            if mode in ('positive','logger'):
                raw=json.loads(out.read_text())
                assert raw['processId'] > 0 and raw['completedReports']==1
                assert raw['runtimeAcceptance'] is False and raw['timedOut'] is False
                assert raw['finalManagedLoggerCalls'] == (2 if mode=='logger' else 1)
            elif mode == 'existing': assert out.read_text()=='unchanged'
            else: assert not out.exists()
    print(json.dumps({'result':'Passed','cases':4,'actualNativeObserver':True,
        'vmCompletionAndState':'Mocked','unityRun':False,
        'sanitizers':os.environ.get('R03_SANITIZERS')=='1'}))

if __name__=='__main__': main()
