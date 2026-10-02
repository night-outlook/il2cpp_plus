#pragma once
// Test-only bounded attribution. Disabled at compile time in ordinary Players.
// No managed calls, name resolution, retry, cache mutation or acceptance policy.
// A session has one owner, six exact boundaries and a fail-closed cold-event log.
#include "AssemblyShadowAdmissionCache.h"
#include "AssemblyShadowObservationCounters.h"
#ifndef HYBRIDCLR_R03_RUNTIME_PROBE
#define HYBRIDCLR_R03_RUNTIME_PROBE 0
#endif
#if HYBRIDCLR_R03_RUNTIME_PROBE != 0 && HYBRIDCLR_R03_RUNTIME_PROBE != 1
#error Invalid HYBRIDCLR_R03_RUNTIME_PROBE
#endif
#include <cstring>
#include <mutex>
#if HYBRIDCLR_R03_RUNTIME_PROBE
#include "AssemblyShadowProducerFence.h"
#include "AssemblyShadowProbeTypeSnapshot.h"
#endif

namespace il2cpp { namespace vm { namespace assembly_shadow_r03 {
using assembly_shadow_r02::AdmissionKey;
using assembly_shadow_r02::Metric;
using assembly_shadow_r02::ObservationCounters;

struct LayoutRow
{
    const void* baseline = nullptr;
    const void* target = nullptr;
#if HYBRIDCLR_R03_RUNTIME_PROBE
    ProbeTypeSnapshot baselineIdentity, targetIdentity;
#endif
    bool fieldsChanged = false, baselineReady = false, targetReady = false;
    bool targetDefinitionReady = false, physicalProof = false;
    bool sourceSizeInited = false, targetSizeInited = false;
    bool sourcePending = false, targetPending = false;
    bool baselineInitialized = false, baselineVtable = false;
    uint32_t baselineCctor = 0, sourceSize = 0, targetSize = 0;
    int32_t sourceNativeSize = 0, targetNativeSize = 0;
    bool targetInitialized = false, targetVtable = false;
    uint32_t targetCctor = 0;
    uint32_t sourceFieldCount = 0, targetFieldCount = 0;
    uint32_t sourceOffsets[16] = {}, targetOffsets[16] = {};
    uint32_t sourceAttrs[16] = {}, targetAttrs[16] = {}, targetStorage[16] = {};
    bool truncated = false;
    int32_t error = 0;
};

#if HYBRIDCLR_R03_RUNTIME_PROBE
class RuntimeProbe
{
public:
    static constexpr size_t kEvents = 128, kSamples = 6, kLayouts = 32;
    struct Producer
    {
        const void* finalizerClass = nullptr;
        const void* callback = nullptr;
        uint64_t osThread = 0;
        bool recognizedArrayPool = false;
    };
    static Producer& CurrentProducer() { static thread_local Producer value; return value; }
    class ProducerScope
    {
        Producer previous_;
    public:
        ProducerScope(const void* klass, const void* callback, uint64_t osThread, bool recognized)
            : previous_(CurrentProducer())
        {
            auto& value = CurrentProducer(); value.finalizerClass = klass;
            value.callback = callback; value.osThread = osThread; value.recognizedArrayPool = recognized;
        }
        ~ProducerScope() { CurrentProducer() = previous_; }
        ProducerScope(const ProducerScope&) = delete;
        ProducerScope& operator=(const ProducerScope&) = delete;
    };
    struct Event
    {
        AdmissionKey key{};
        Producer producer;
        Metric metric = Metric::AdmissionMisses;
        uint64_t amount = 0, thread = 0;
        uint32_t phase = 0;
        char site[128] = {};
    };
    struct Sample
    {
        int label = -1;
        size_t eventEnd = 0;
        uint64_t thread = 0, values[static_cast<size_t>(Metric::Count)] = {};
        bool saturated = false;
        uint64_t droppedThreads = 0;
    };
    struct Report
    {
        bool used = false, sealed = false, invalid = false, overflow = false;
        const void* target = nullptr;
        uint64_t generation = 0, owner = 0;
        size_t events = 0, samples = 0, layouts = 0;
        Event event[kEvents]; Sample sample[kSamples]; LayoutRow layout[kLayouts];
    };
private:
    struct State
    {
        std::mutex mutex;
        std::atomic<bool> active{false};
        uint32_t phase = 0, observers = 0;
        Report report;
    };
    static State& Data() { static State state; return state; }
    static const char*& Site() { static thread_local const char* site = "<unspecified>"; return site; }
    static uint64_t Thread()
    {
        static std::atomic<uint64_t> next{1};
        static thread_local uint64_t id = next.fetch_add(1, std::memory_order_relaxed);
        return id;
    }
    static void Snapshot(State& s, int label)
    {
        auto& r = s.report;
        if (r.samples == kSamples) { r.overflow = true; return; }
        Sample& value = r.sample[r.samples++]; value.label = label;
        value.eventEnd = r.events; value.thread = Thread();
        for (size_t i = 0; i < static_cast<size_t>(Metric::Count); ++i)
            value.values[i] = ObservationCounters::Read(static_cast<Metric>(i));
        value.saturated = ObservationCounters::Saturated();
        value.droppedThreads = ObservationCounters::DroppedThreads();
    }
public:
    class SiteScope
    {
        const char* previous;
    public:
        explicit SiteScope(const char* site) : previous(Site()) { Site() = site; }
        ~SiteScope() { Site() = previous; }
    };
    static bool Begin(const void* target, uint64_t generation)
    {
        auto& s = Data(); std::lock_guard<std::mutex> lock(s.mutex);
        if (s.report.used || !target || !generation || ObservationCounters::Level == 0) return false;
        s.report.used = true; s.report.target = target; s.report.generation = generation;
        s.report.owner = Thread(); s.phase = 1;
        Snapshot(s, 0); s.active.store(true, std::memory_order_release); return true;
    }
    static bool Mark(int boundary)
    {
        auto& s = Data(); std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.active.load() || Thread() != s.report.owner || boundary != static_cast<int>(s.phase) ||
            (boundary == 1 && s.observers != 1) || (boundary == 3 && s.observers != 2))
        { s.report.invalid = true; return false; }
        Snapshot(s, boundary);
        if (boundary == 3) { s.active.store(false, std::memory_order_release); s.report.sealed = true; }
        else ++s.phase;
        return true;
    }
    // Called immediately before the existing observational R02 JSON snapshot.
    static void ObserverSnapshot()
    {
        auto& s = Data(); if (!s.active.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.active.load()) return;
        if (Thread() != s.report.owner || s.observers >= 2 ||
            (s.observers == 0 && s.phase != 1) || (s.observers == 1 && s.phase != 3))
        { s.report.invalid = true; return; }
        Snapshot(s, 10 + s.observers++);
    }
    // Cold counters and event insertion share the snapshot lock so a cross-
    // thread miss cannot be counted on one side and logged on the other side.
    static void Count(Metric metric, uint64_t amount, const AdmissionKey& key)
    {
        auto& s = Data();
        if (!s.active.load(std::memory_order_acquire)) { ObservationCounters::Add(metric, amount); return; }
        std::lock_guard<std::mutex> lock(s.mutex);
        ObservationCounters::Add(metric, amount);
        if (!s.active.load()) return;
        auto& r = s.report;
        if (r.events == kEvents) { r.overflow = true; return; }
        Event& e = r.event[r.events++]; e.key = key; e.metric = metric;
        e.amount = amount; e.thread = Thread(); e.phase = s.phase; e.producer = CurrentProducer();
        const char* site = Site(); const size_t length = std::strlen(site);
        if (length >= sizeof(e.site)) r.overflow = true;
        std::memcpy(e.site, site, length < sizeof(e.site) ? length : sizeof(e.site) - 1);
    }
    // Defined in the VM adapter. It snapshots definition identity before the
    // staged caller's ownership scope ends, and only then copies the row.
    static void RecordLayout(const LayoutRow& row);
    static Report Read()
    {
        auto& s = Data(); std::lock_guard<std::mutex> lock(s.mutex);
        if (s.active.load()) s.report.invalid = true;
        return s.report;
    }
};
#else
class RuntimeProbe
{
public:
    class SiteScope { public: explicit SiteScope(const char*) {} };
    static void ObserverSnapshot() {}
    static void Count(Metric metric, uint64_t amount, const AdmissionKey&) { ObservationCounters::Add(metric, amount); }
    static void RecordLayout(const LayoutRow&) {}
};
#endif
}}}
