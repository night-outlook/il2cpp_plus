#!/usr/bin/env python3
"""Apply one exact native source edit in an isolated CI checkout only."""
from pathlib import Path
import hashlib

p = Path('libil2cpp/vm/AssemblyShadow.cpp')
old = p.read_bytes()
def blob(raw):
    return hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest()
if blob(old) != '08fd924a91fbd9701b50ac45ea7846f0bedd9a8c':
    raise ValueError('Exact reviewed native source required')
s = old.decode()
def once(a, b):
    global s
    if s.count(a) != 1:
        raise ValueError('Native edit anchor is missing or ambiguous')
    s = s.replace(a, b)
once('    bool Interpreter(const Il2CppAssembly* assembly)\n',
     '#include "AssemblyShadowTerminalExecution.inc"\n\n    bool Interpreter(const Il2CppAssembly* assembly)\n')
once('    s_methodChecks.fetch_add(1, std::memory_order_relaxed);\n',
     '    s_methodChecks.fetch_add(1, std::memory_order_relaxed);\n    if (RejectTerminalMethod(method)) return false;\n')
once('    if (error == AssemblyShadowError::Success) return true;\n\n    // No managed exception',
     '    // Identity is necessary, but a concurrently published terminal state must\n    // also be observed before this supported business entry is admitted.\n    if (error == AssemblyShadowError::Success) return !RejectTerminalMethod(method);\n\n    // No managed exception')
once('    if (!AssertMethodIsActive(method, site))\n    {\n',
     '    if (!AssertMethodIsActive(method, site))\n    {\n        if (TerminalExecutionObserved()) RaiseTerminalMethodFailure();\n')
if blob(s.encode()) != '67e5f070438a4355c7572771f73b24a8575f8271':
    raise ValueError('Prepared native bytes differ from Primary authoring')
p.write_bytes(s.encode())
print('Prepared native source blob=' + blob(s.encode()) + '; no commit/ref modified')
