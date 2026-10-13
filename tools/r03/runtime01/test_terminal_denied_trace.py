#!/usr/bin/env python3
"""Compile actual source-extracted bounded denial trace; never run Unity."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'libil2cpp/vm/AssemblyShadow.cpp'

def main():
    s = SOURCE.read_text()
    start = s.index('            static std::atomic<uint64_t> s_deniedOrdinal{0};')
    end = s.index('#endif', start)
    excerpt = s[start:end]
    assert 's_deniedOrdinal.fetch_add(1, std::memory_order_relaxed)' in excerpt
    assert '(ordinal & (ordinal - 1)) == 0' in excerpt
    assert 'std::fprintf(stderr,' in excerpt
    assert 'RequireActive' not in excerpt and 'return true;' not in excerpt
    cc = shutil.which('clang++') or shutil.which('g++')
    assert cc, 'Strict C++17 compiler required'
    source = r"""
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
struct Il2CppClass { const char* namespaze; const char* name; };
struct MethodInfo { Il2CppClass* klass; const char* name; };
static void denied(const MethodInfo* method, const char* site)
{
""" + excerpt + r"""
}
int main()
{
 Il2CppClass owner{"UnityEngine","Debug"};
 MethodInfo method{&owner,"LogException"};
 for (int i=0;i<50623;++i) denied(&method,"Runtime::Invoke");
}
"""
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp).resolve(strict=True)
        cpp, exe = root/'test.cpp', root/'test'
        cpp.write_text(source)
        built = subprocess.run([cc,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic',
                                str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=40)
        assert built.returncode == 0, built.stderr
        run = subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
        assert run.returncode == 0, run.stderr
        rows = run.stderr.splitlines()
        expected = list(range(1,13)) + [1 << i for i in range(4,16)]
        assert len(rows)==len(expected)
        actual=[]
        for line in rows:
            m = re.search(r'ordinal=(\d+) site=Runtime::Invoke namespace=UnityEngine type=Debug method=LogException ptr=0x[0-9a-fA-F]+',line)
            assert m, line
            actual.append(int(m.group(1)))
        assert actual == expected, (actual,expected)
        print(json.dumps({'result':'Passed','source':'actual AssemblyShadow.cpp diagnostic fragment',
            'simulatedDeniedCalls':50623,'nativeLogLines':len(rows),'ordinals':actual,
            'unityLaunched':False,'runtimeAcceptance':False}))

if __name__=='__main__':
    main()
