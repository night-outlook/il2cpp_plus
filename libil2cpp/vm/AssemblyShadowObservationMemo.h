#pragma once
#include "AssemblyShadowObservationCounters.h"
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

// A memo of successful registration in the process-lifetime class inventory,
// not permission to execute or allocate a type. Collision means a normal slow
// observation. Never remember an unregistered or dropped class.
class ObservationMemo
{
    static constexpr size_t Capacity = 64;
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL >= 2
    struct Slots { const void* values[Capacity] = {}; };
    static Slots& Data() { static thread_local Slots slots; return slots; }
    static size_t Index(const void* value) noexcept
    {
        uintptr_t key = reinterpret_cast<uintptr_t>(value) >> 3;
        key ^= key >> 11;
        return static_cast<size_t>(key % Capacity);
    }
#endif
public:
    static bool Contains(const void* value) noexcept
    {
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL >= 2
        return value && Data().values[Index(value)] == value;
#else
        (void)value;
        return false;
#endif
    }
    static void Remember(const void* value) noexcept
    {
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL >= 2
        if (value) Data().values[Index(value)] = value;
#else
        (void)value;
#endif
    }
    static size_t TlsBytesPerThread() noexcept
    {
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL >= 2
        return sizeof(Slots);
#else
        return 0;
#endif
    }
};

}}}
