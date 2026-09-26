#!/usr/bin/env python3
"""Reproduce the narrowly anchored R02 VM edits from immutable H1 source.

Used by Primary CI to prepare Git blobs; never a Local Validation patch step.
--verify requires the generated production sources already be committed.
No branch/ref writes, source-pin changes or network operations are performed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

BASE = "6be7f38bec2fa4677d24efc1a4a1294240789933"
INPUTS = {
    "libil2cpp/vm/AssemblyShadowTypeResolver.cpp": "0ab8f0c72322a722f7fab1afde3fff3c902ea86d",
    "libil2cpp/vm/AssemblyShadow.cpp": "7ef81545ed5ed59abe9eb993d31dec4bcabe8f32",
}


def blob(raw: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()


def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"Expected one exact source anchor, got {text.count(old)}: {old[:100]!r}")
    return text.replace(old, new, 1)


def function_span(text: str, signature: str) -> tuple[int, int]:
    """C++ brace scan ignoring comments, ordinary strings and char literals."""
    if text.count(signature) != 1:
        raise ValueError(f"Non-unique function signature: {signature}")
    start = text.index(signature)
    pos = text.index("{", start)
    depth = 0
    state = "code"
    i = pos
    while i < len(text):
        c = text[i]
        pair = text[i:i + 2]
        if state == "line":
            if c == "\n": state = "code"
        elif state == "block":
            if pair == "*/": state = "code"; i += 1
        elif state in ('"', "'"):
            if c == "\\": i += 1
            elif c == state: state = "code"
        elif pair == "//": state = "line"; i += 1
        elif pair == "/*": state = "block"; i += 1
        elif c in ('"', "'"): state = c
        elif c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: return start, i + 1
        i += 1
    raise ValueError(f"Unclosed function: {signature}")


def replace_function(text: str, signature: str, edit) -> str:
    start, end = function_span(text, signature)
    return text[:start] + edit(text[start:end]) + text[end:]


COUNTERPART = r'''Il2CppClass* BaselineCounterpart(Il2CppClass* active, const Il2CppImage* image)
{
    using namespace assembly_shadow_r02;
    const uint64_t generation = AssemblyShadow::ActiveGeneration();
    if (!generation || Private())
        return FindDefinition(image, AssemblyShadowTypeKey::Make(active), true);
    // A null value is an authenticated absence, not an allocation certificate.
    static AdmissionCache<Il2CppClass*>* cache = [] {
        auto* value = new AdmissionCache<Il2CppClass*>();
        ObservationCounters::Add(Metric::CacheFixedBytes, sizeof(*value));
        return value;
    }();
    const AdmissionKey key{generation, active, image, 2};
    if (const auto* found = cache->Find(key))
    {
        ObservationCounters::Add(Metric::CounterpartHits, 1);
        return *found;
    }
    ObservationCounters::Add(Metric::CounterpartMisses, 1);
    il2cpp::os::FastAutoLock metadataLock(&g_MetadataLock);
    if (const auto* found = cache->Find(key))
    {
        ObservationCounters::Add(Metric::CounterpartHits, 1);
        return *found;
    }
    Il2CppClass* baseline = FindDefinition(image, AssemblyShadowTypeKey::Make(active), true);
    bool inserted = false;
    const auto* result = cache->Publish(key, baseline, inserted);
    if (inserted)
    {
        ObservationCounters::Add(Metric::CounterpartEntries, 1);
        ObservationCounters::Add(Metric::CounterpartRetainedBytes, cache->EntryBytes());
        if (!baseline) ObservationCounters::Add(Metric::CounterpartAbsent, 1);
    }
    return *result;
}

'''

RESOLVE = r'''Il2CppClass* AssemblyShadowTypeResolver::ResolveAllocation(Il2CppClass* klass, const char* site)
{
    using namespace assembly_shadow_r02;
    using Certificate = AllocationCertificate<Il2CppClass>;
    if (!klass) return nullptr;
    if (!site) site = "<unspecified>";
    if (AssemblyShadow::IsResolvingTypeMetadata())
        Fail("ShadowAllocationDuringMetadataResolution", site, AssemblyShadowError::BaselineAlreadyUsed);
    const uint64_t generation = AssemblyShadow::ActiveGeneration();
    auto build = [&](Certificate& certificate) {
        Il2CppClass* target = ResolveClass(klass);
        if (target != klass)
        {
            AssemblyShadowTypeMetadataScope scope;
            CheckLayout(klass, target, site);
            Increment(State().allocations);
            certificate.involvesShadow = true;
        }
        if (generation)
        {
            AssemblyShadowTypeMetadataScope scope;
            CheckActiveComponents(&target->byval_arg, site);
        }
        // A first allocation can precede lazy size finalization. Preserve its
        // successful uncached behavior, but retry proof after metadata is ready.
        certificate.complete = certificate.complete && target->size_inited;
        return target;
    };
    if (!generation || Private())
    {
        Certificate temporary;
        return build(temporary);
    }
    static AllocationProofCache<Il2CppClass>* cache = [] {
        auto* value = new AllocationProofCache<Il2CppClass>();
        ObservationCounters::Add(Metric::CacheFixedBytes, sizeof(*value));
        return value;
    }();
    // Conservative allocation profile 1; full physical class identity keeps
    // closed generics, arrays, baseline inputs and active inputs distinct.
    const AdmissionKey key{generation, klass, nullptr, 1};
    auto validate = [&](const Certificate* certificate) {
        if (AssemblyShadow::IsResolvingTypeMetadata())
            Fail("ShadowAllocationDuringMetadataResolution", site, AssemblyShadowError::BaselineAlreadyUsed);
        if (Private() || AssemblyShadow::ActiveGeneration() != generation)
            Fail("ShadowAdmissionContextChanged", site, AssemblyShadowError::InvalidState);
        if (!certificate) return;
        if (certificate->involvesShadow)
        {
            AssemblyShadowState state;
            AssemblyShadow::GetState(state); // Atomic acquire, no transaction lock.
            if (state == AssemblyShadowState::Failed || state == AssemblyShadowState::FailedAfterCommit)
                Fail("ShadowAllocationAfterFailure", site, AssemblyShadowError::InvalidState);
        }
        // These are the same mutable baseline-state conditions as CheckLayout,
        // including every recursively checked parent/component. Guarded engine
        // entry points remain authoritative; no old object's class is remapped.
        for (const auto& dependency : certificate->dependencies)
        {
            Il2CppClass* baseline = dependency.baseline;
            if (!baseline) continue;
            ObservationCounters::Add(Metric::BaselineChecks, 1);
            if (baseline->initialized || baseline->is_vtable_initialized ||
                os::Atomic::LoadRelaxed(reinterpret_cast<const int32_t*>(&baseline->cctor_started)) != 0)
                Fail("ShadowBaselineAlreadyInitialized", AssemblyShadowTypeKey::Format(&baseline->byval_arg) +
                    " Site=" + site, AssemblyShadowError::BaselineAlreadyUsed);
        }
    };
    // Exceptions used to report failure are unrelated BCL allocations. They
    // must remain constructible; poison rejection applies to Shadow proofs.
    return cache->Resolve<il2cpp::os::FastAutoLock>(key, &g_MetadataLock, build, validate);
}'''


def resolver(text: str) -> str:
    text = once(text, '#include "AssemblyShadowDiagnostics.h"',
        '#include "AssemblyShadowDiagnostics.h"\n#include "AssemblyShadowAllocationProof.h"\n#include "AssemblyShadowR02Diagnostics.h"\n#include "vm/MetadataLock.h"\n#include "os/Atomic.h"')
    text = once(text, '    std::atomic<uint64_t> hits{0}, misses{0}, rebuilds{0}, allocations{0}, failures{0};',
        '''    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::DefinitionHits> hits;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::DefinitionMisses> misses;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::CompositeRebuilds> rebuilds;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::AllocationRemaps> allocations;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::GuardFailures> failures;''')
    text = replace_function(text, 'void Increment(std::atomic<uint64_t>& counter)', lambda _: '''template<assembly_shadow_r02::Metric Id>
void Increment(assembly_shadow_r02::ObservationCounter<Id>& counter)
{
    counter.fetch_add(1, std::memory_order_relaxed);
}''')
    text = once(text, '    Il2CppClass* result = nullptr;\n    for (size_t depth',
        '    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::DefinitionSearches, 1);\n    Il2CppClass* result = nullptr;\n    for (size_t depth')
    text = once(text, '                auto handle = MetadataCache::GetAssemblyTypeHandle(image, index);',
        '                assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::DefinitionRows, 1);\n                auto handle = MetadataCache::GetAssemblyTypeHandle(image, index);')
    text = once(text, '            while (auto handle = MetadataCache::GetNestedTypes(result->typeMetadataHandle, &iterator)) consider(handle);',
        '''            while (auto handle = MetadataCache::GetNestedTypes(result->typeMetadataHandle, &iterator))
            {
                assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::DefinitionRows, 1);
                consider(handle);
            }''')
    text = once(text, '    std::vector<InstanceFieldLayout> fields;',
        '    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::FieldWorkspaces, 1);\n    std::vector<InstanceFieldLayout> fields;')
    text = once(text, '    std::vector<std::string> interfaces;',
        '    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::InterfaceWorkspaces, 1);\n    std::vector<std::string> interfaces;')
    text = once(text, '    const std::string key = AssemblyShadowTypeKey::Format(&source->byval_arg) + " Site=" + site;',
        '''    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::LayoutChecks, 1);
    if (auto* trace = assembly_shadow_r02::AllocationProofTrace<Il2CppClass>::Current())
    {
        trace->Observe(source, target, structural);
        if (!structural && (!source->size_inited || !target->size_inited)) trace->complete = false;
    }
    const std::string key = AssemblyShadowTypeKey::Format(&source->byval_arg) + " Site=" + site;''')
    anchor = 'void CheckActiveComponents(const Il2CppType* type, const char* site, uint32_t depth = 0)'
    text = once(text, anchor, COUNTERPART + anchor)
    text = once(text, '    Il2CppClass* baseline = FindDefinition(baselineAssembly->image, AssemblyShadowTypeKey::Make(active), true);',
        '''    Il2CppClass* baseline = BaselineCounterpart(active, baselineAssembly->image);
    if (auto* trace = assembly_shadow_r02::AllocationProofTrace<Il2CppClass>::Current())
        trace->Observe(baseline, active, AssemblyShadowTypeKey::GenericArity(active) != 0);''')
    text = replace_function(text, 'Il2CppClass* AssemblyShadowTypeResolver::ResolveAllocation(', lambda _: RESOLVE)
    text = once(text, '            << ",\\"guardFailures\\":" << state.failures.load() << "}";',
        '            << ",\\"guardFailures\\":" << state.failures.load();\n        assembly_shadow_r02::AppendDiagnostics(output);\n        output << "}";')
    return text


COUNTERS = {
    's_methodChecks': 'MethodChecks', 's_shadowMethodChecks': 'ShadowMethodChecks',
    's_rejectedBaselineMethods': 'RejectedBaselineMethods',
    's_baselineClassCctorStarted': 'BaselineCctors', 's_shadowClassCctorStarted': 'ShadowCctors',
    's_interpreterTransformations': 'Transformations',
    's_shadowInterpreterTransformations': 'ShadowTransformations',
    's_droppedClassObservations': 'DroppedClasses',
}


def assembly(text: str) -> str:
    text = once(text, '#include "AssemblyShadowRecovery.h"',
        '#include "AssemblyShadowRecovery.h"\n#include "AssemblyShadowR02Diagnostics.h"')
    for name, metric in COUNTERS.items():
        text = once(text, f'    std::atomic<uint64_t> {name}{{0}};',
            f'    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::{metric}> {name};')
    text = once(text, '            while (s_executionObservationLock.test_and_set(std::memory_order_acquire)) std::this_thread::yield();',
        '''            bool contended = false;
            while (s_executionObservationLock.test_and_set(std::memory_order_acquire))
            {
                contended = true;
                std::this_thread::yield();
            }
            if (contended) assembly_shadow_r02::ObservationCounters::Add(
                assembly_shadow_r02::Metric::ObservationLockContentions, 1);''')
    text = once(text, '    void ObserveExecutionClass(Il2CppClass* klass)\n    {',
        '''    void ObserveExecutionClass(Il2CppClass* klass)
    {
        if (assembly_shadow_r02::ObservationCounters::Level < 2) return;''')
    text = once(text, '        else if (method->is_inflated || method->klass->generic_class)\n        {',
        '''        else if (method->is_inflated || method->klass->generic_class)
        {
            assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::GenericContextChecks, 1);''')
    def diagnostics(body: str) -> str:
        body = once(body, '    {\n        ExecutionObservationLock lock;',
            '    if (assembly_shadow_r02::ObservationCounters::Level >= 2)\n    {\n        ExecutionObservationLock lock;')
        body = once(body, '    Assembly::CaptureShadowEnumeration(assemblies);',
            '    if (count) Assembly::CaptureShadowEnumeration(assemblies);')
        return once(body, '    output << "]}";',
            '    output << "]";\n    assembly_shadow_r02::AppendDiagnostics(output);\n    output << "}";')
    text = replace_function(text, 'AssemblyShadowError AssemblyShadow::GetExecutionDiagnosticsJson(std::string& json)\n{\n    json.clear();\n    std::array<', diagnostics)
    return text


SAFETY_BODIES = [
    'void AssemblyShadow::RecordBaselineUse(', 'void AssemblyShadow::RequireActiveMethod(',
    'void AssemblyShadow::RequireUserCodeAllowed(', 'void AssemblyShadow::FailTypeResolution(',
    'Il2CppClass* AssemblyShadow::ResolveAllocationClass(', 'void AssemblyShadow::RequireActiveClass(',
    'void AssemblyShadow::RecordTypeUse(', 'void AssemblyShadow::TraceClass(',
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    changes = []
    pending = []
    for path, expected_blob in INPUTS.items():
        raw = subprocess.check_output(['git', '-C', str(root), 'show', f'{BASE}:{path}'])
        if blob(raw) != expected_blob: raise ValueError(f'Wrong immutable input: {path}')
        before = raw.decode('utf-8')
        after = (resolver if 'TypeResolver' in path else assembly)(before)
        if path.endswith('/AssemblyShadow.cpp'):
            for signature in SAFETY_BODIES:
                a, b = function_span(before, signature); c, d = function_span(after, signature)
                if before[a:b] != after[c:d]: raise ValueError(f'Safety body changed: {signature}')
            sig = 'bool AssemblyShadow::AssertMethodIsActive('
            a, b = function_span(before, sig); c, d = function_span(after, sig)
            permitted = '            assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::GenericContextChecks, 1);\n'
            if before[a:b] != after[c:d].replace(permitted, ''):
                raise ValueError('Method ownership/context guard changed')
        new_raw = after.encode('utf-8')
        target = root / path
        current = target.read_bytes()
        if current not in (raw, new_raw): raise ValueError(f'Unrelated source edits would be overwritten: {path}')
        if args.verify and current != new_raw: raise ValueError(f'R02 adapter not installed: {path}')
        pending.append((target, new_raw))
        changes.append({'path': path, 'baseBlob': expected_blob, 'resultBlob': blob(new_raw),
            'sha256': hashlib.sha256(new_raw).hexdigest(), 'sizeBytes': len(new_raw)})
    if not args.verify:
        for target, raw in pending: target.write_bytes(raw)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'kind': 'R02ExactSourceEdits', 'baseCommit': BASE,
        'result': 'Verified' if args.verify else 'Prepared', 'files': changes,
        'unchangedSafetyBodies': SAFETY_BODIES, 'runtimeAcceptance': False}, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
