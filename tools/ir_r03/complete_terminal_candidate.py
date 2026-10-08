#!/usr/bin/env python3
"""Derive the reviewed final candidate and extend, never relax, its native tests.

Only disposable CI workspaces use this preparation recipe. Feature publication
is a separate Connector commit of the resulting source/test blobs.
"""
from pathlib import Path
import hashlib

def replace_once(text,old,new):
    if text.count(old)!=1:raise ValueError('Unique candidate anchor required: '+old[:70])
    return text.replace(old,new)
p=Path('libil2cpp/vm/AssemblyShadow.cpp');data=p.read_bytes()
assert hashlib.sha256(data).hexdigest()=='51f2a0c8f887f5a9b1453769243d7dcaaccbd9d6f6bc2cdbb4ec39a4c0b8e664'
s=replace_once(data.decode(),
 '            s_typeFailure.load(std::memory_order_acquire) || s_unexpectedFailure.load(std::memory_order_acquire);\n',
 '            s_typeFailure.load(std::memory_order_acquire) || s_unexpectedFailure.load(std::memory_order_acquire) ||\n            s_lateBaselineUse.load(std::memory_order_acquire) || s_referenceViolation.load(std::memory_order_acquire);\n')
b=s.encode();assert hashlib.sha256(b).hexdigest()=='f33c0704b97c8dfc6abace699a15e099e4e3d66968dc188246e74667e9cb43d6'
assert hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()=='0149803ef35ce6d92296d913cbccbb57e5c5144a'
p.write_bytes(b)
t=Path('tools/ir_r03/test_terminal_entry.py');s=t.read_text()
s=replace_once(s,'s_unexpectedFailure{false}, s_lateBaselineUse{false};','s_unexpectedFailure{false}, s_lateBaselineUse{false}, s_referenceViolation{false};')
s=replace_once(s,'else if(scenario=="unexpected"){s_unexpectedFailure.store(true);}', 'else if(scenario=="unexpected"){s_unexpectedFailure.store(true);}\n  else if(scenario=="late-use"){s_lateBaselineUse.store(true);}\n  else if(scenario=="reference"){s_referenceViolation.store(true);}')
s=replace_once(s,"'latched','unexpected'):","'latched','unexpected','late-use','reference'):")
t.write_text(s)
print('Exact native candidate and two additional first-cause scenarios prepared.')
