#!/usr/bin/env python3
"""Primary-authored one-shot source transformation of exact E native blobs.

CI uses an isolated source tree. This script never modifies Git refs, historical
Player data, or the user's checkouts. Resulting production blobs require separate
Connector publication after tests and diff review.
"""
import hashlib
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
INPUTS = {
    'libil2cpp/vm/Runtime.cpp': '4cef5fd59aecbc2ad5ba69fb6a33ad3f044f6fde',
    'libil2cpp/vm/AssemblyShadow.cpp': '38f85a37ddd45f0d1675e2d9125d1ce46d2b4408',
}

def git_blob(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Source context not unique: ' + old[:100])
    return text.replace(old, new, 1)

def main():
    original = {}
    for name, expected in INPUTS.items():
        data = (ROOT / name).read_bytes()
        if git_blob(data) != expected:
            raise RuntimeError('Exact E source blob required: ' + name)
        original[name] = data.decode('utf-8')
    runtime = original['libil2cpp/vm/Runtime.cpp']
    runtime = '#include "AssemblyShadowTerminalReporting.h"\n' + runtime
    marker = '''        try
        {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
            // Unity caches callback MethodInfo pointers'''
    runtime = replace_once(runtime, marker, '''        try
        {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
            // A terminal native diagnostic transport is not managed business
            // execution. No handler or subscriber body is allowed by this path.
            Il2CppObject* terminalReport = nullptr;
            if (assembly_shadow_reporting::TryHandle(method, obj, params, terminalReport))
                return terminalReport;
            // Unity caches callback MethodInfo pointers''')
    shadow = original['libil2cpp/vm/AssemblyShadow.cpp']
    shadow = replace_once(shadow,
        '        if (s_typeFailure.compare_exchange_strong(expected, failure.get(), std::memory_order_acq_rel)) failure.release();',
        '''        if (s_typeFailure.compare_exchange_strong(expected, failure.get(), std::memory_order_acq_rel))
        {
            failure.release();
            // First-writer-only native evidence; no managed ToString/logger.
            // Original recovery facts stay immutable. Never infer a Player PASS
            // from this line; it is an originating failure diagnostic only.
            std::fprintf(stderr, "[AssemblyShadowFirstFailure] code=%d detail=%.*s truncated=%d\\n",
                static_cast<int32_t>(error), 4096, detail.c_str(), detail.size() > 4096 ? 1 : 0);
            std::fflush(stderr);
        }''')
    for name, text in [('libil2cpp/vm/Runtime.cpp', runtime), ('libil2cpp/vm/AssemblyShadow.cpp', shadow)]:
        (ROOT / name).write_bytes(text.encode('utf-8'))
        print(name, git_blob(text.encode('utf-8')))
    subprocess.run(['git', '-C', str(ROOT), 'diff', '--check'], check=True)

if __name__ == '__main__':
    main()
