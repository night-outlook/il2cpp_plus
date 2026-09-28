"""Deterministic, bounded H readiness correction; never initializes baseline state."""
from source_edits import once

HELPERS = r"""assembly_shadow_r02::PhysicalLayoutState PhysicalLayout(Il2CppClass* klass)
{
    return {klass->size_inited != 0, klass->size_init_pending != 0,
        klass->image != nullptr, klass->typeMetadataHandle != nullptr,
        klass->image && hybridclr::metadata::IsInterpreterImage(klass->image),
        klass->generic_class != nullptr, klass->is_generic != 0, klass->rank != 0,
        klass->byval_arg.type == IL2CPP_TYPE_CLASS || klass->byval_arg.type == IL2CPP_TYPE_VALUETYPE,
        klass->instance_size};
}

void ObserveLayoutReadiness(Il2CppClass* source, Il2CppClass* target, bool sourceReady,
    const char* site)
{
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL >= 2
    // Diagnostic-only, bounded to sixteen cold decisions per process. No
    // managed log callback, class initialization or schema mutation is involved.
    static std::atomic<uint32_t> emitted{0};
    uint32_t slot = emitted.load(std::memory_order_relaxed);
    do { if (slot >= 16) return; }
    while (!emitted.compare_exchange_weak(slot, slot + 1, std::memory_order_relaxed));
    std::fprintf(stderr, "[R02LayoutReadiness] sample=%u source=%p target=%p "
        "sourceToken=%u targetToken=%u sourceImage=%p targetImage=%p "
        "sourceSizeInited=%u targetSizeInited=%u sourcePending=%u targetPending=%u "
        "sourceGeneric=%u targetGeneric=%u sourceRank=%u targetRank=%u "
        "sourceSize=%u targetSize=%u sourceReady=%u site=%.96s\n",
        slot, static_cast<void*>(source), static_cast<void*>(target), source->token, target->token,
        static_cast<const void*>(source->image), static_cast<const void*>(target->image),
        unsigned(source->size_inited), unsigned(target->size_inited),
        unsigned(source->size_init_pending), unsigned(target->size_init_pending),
        unsigned(source->generic_class != nullptr || source->is_generic),
        unsigned(target->generic_class != nullptr || target->is_generic),
        unsigned(source->rank), unsigned(target->rank), source->instance_size,
        target->instance_size, unsigned(sourceReady), site);
#else
    (void)source; (void)target; (void)sourceReady; (void)site;
#endif
}

"""

def apply(text):
    text = once(text, '#include "AssemblyShadowAllocationProof.h"',
        '#include "AssemblyShadowAllocationProof.h"\n#include "AssemblyShadowLayoutReadiness.h"\n#include <cstdio>')
    marker = 'void CheckLayout(Il2CppClass* source, Il2CppClass* target, const char* site, uint32_t depth = 0, bool structural = false)'
    text = once(text, marker, HELPERS + marker)
    text = once(text,
        '        if (!structural && (!source->size_inited || !target->size_inited)) trace->complete = false;',
        """        if (!structural)
        {
            const bool sourceReady = assembly_shadow_r02::BaselineLayoutReady(
                PhysicalLayout(source), static_cast<uint32_t>(sizeof(Il2CppObject)));
            if (!sourceReady || !target->size_inited) trace->complete = false;
            if (!source->size_inited || !target->size_inited)
                ObserveLayoutReadiness(source, target, sourceReady, site);
        }""")
    return text
