#pragma once

// Compile-time observation policy, independent of correctness guards.
// 0: correctness only; 1: bounded counters; 2: detailed class observations.
// Keep level 2 as the compatibility default for existing diagnostic fixtures.
#ifndef HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL
#define HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL 2
#endif
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL < 0 || HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL > 2
#error Invalid HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

enum class Metric : size_t
{
    MethodChecks, ShadowMethodChecks, RejectedBaselineMethods,
    BaselineCctors, ShadowCctors, Transformations, ShadowTransformations,
    DroppedClasses, ObservationLockContentions, GenericContextChecks,
    DefinitionHits, DefinitionMisses, CompositeRebuilds, AllocationRemaps,
    GuardFailures, DefinitionSearches, DefinitionRows,
    AdmissionHits, AdmissionMisses, AdmissionBuilds, AdmissionRejects,
    AdmissionEntries, AdmissionRetainedBytes, AdmissionUnready, BaselineChecks,
    FieldWorkspaces, InterfaceWorkspaces, LayoutChecks,
    Count
};

class ObservationCounters
{
public:
    static constexpr size_t kThreadCapacity = 128;
    static constexpr int Level = HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL;

private:
    // Each slot has one process-lifetime owner. Never reuse a departed thread's
    // slot: snapshots include departed threads and cannot race with recycling.
    struct alignas(64) ThreadSlot
    {
        std::atomic<uint64_t> values[static_cast<size_t>(Metric::Count)];
        ThreadSlot()
        {
            for (auto& value : values) value.store(0, std::memory_order_relaxed);
        }
    };
    struct State
    {
        ThreadSlot slots[kThreadCapacity];
        std::atomic<uint32_t> claimed{0};
        std::atomic<uint64_t> droppedThreads{0};
    };

    static State& Data()
    {
        // Function-local static initialization is thread safe. No native heap
        // allocation and no OS-thread identifier or TLS map are required.
        static State state;
        return state;
    }

    static uint32_t Claim()
    {
        State& state = Data();
        uint32_t next = state.claimed.load(std::memory_order_relaxed);
        while (next < kThreadCapacity)
        {
            if (state.claimed.compare_exchange_weak(next, next + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) return next;
        }
        uint64_t dropped = state.droppedThreads.load(std::memory_order_relaxed);
        while (dropped != UINT64_MAX && !state.droppedThreads.compare_exchange_weak(
            dropped, dropped + 1, std::memory_order_relaxed)) {}
        return static_cast<uint32_t>(kThreadCapacity);
    }

    static uint32_t ThreadIndex()
    {
        static thread_local const uint32_t slot = Claim();
        return slot;
    }

public:
    static uint64_t Add(Metric metric, uint64_t amount,
        std::memory_order order = std::memory_order_relaxed) noexcept
    {
        if (Level == 0) return 0;
        const uint32_t slot = ThreadIndex();
        if (slot >= kThreadCapacity) return 0;
        auto& value = Data().slots[slot].values[static_cast<size_t>(metric)];
        const uint64_t old = value.load(std::memory_order_relaxed);
        const uint64_t next = amount > UINT64_MAX - old ? UINT64_MAX : old + amount;
        // A TLS slot has exactly one writer. Atomic stores let diagnostic
        // readers sample safely without contended read/modify/write traffic.
        value.store(next, order == std::memory_order_relaxed ?
            std::memory_order_relaxed : std::memory_order_release);
        return old;
    }

    static uint64_t Read(Metric metric,
        std::memory_order order = std::memory_order_relaxed) noexcept
    {
        if (Level == 0) return 0;
        State& state = Data();
        uint32_t count = state.claimed.load(std::memory_order_acquire);
        if (count > kThreadCapacity) count = static_cast<uint32_t>(kThreadCapacity);
        uint64_t total = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint64_t part = state.slots[i].values[static_cast<size_t>(metric)].load(
                order == std::memory_order_relaxed ? std::memory_order_relaxed : std::memory_order_acquire);
            total = part > UINT64_MAX - total ? UINT64_MAX : total + part;
        }
        return total;
    }

    static uint64_t DroppedThreads() noexcept
    {
        return Level == 0 ? 0 : Data().droppedThreads.load(std::memory_order_relaxed);
    }
    static size_t RetainedBytes() noexcept { return Level == 0 ? 0 : sizeof(State); }
    static const char* Coverage() noexcept
    {
        return Level == 0 ? "Disabled" : DroppedThreads() ? "Truncated" : "BoundedComplete";
    }
};

template<Metric Id>
struct ObservationCounter
{
    explicit ObservationCounter(uint64_t = 0) {}
    uint64_t fetch_add(uint64_t amount, std::memory_order order = std::memory_order_relaxed) noexcept
    {
        return ObservationCounters::Add(Id, amount, order);
    }
    uint64_t load(std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        return ObservationCounters::Read(Id, order);
    }
};

}}}
