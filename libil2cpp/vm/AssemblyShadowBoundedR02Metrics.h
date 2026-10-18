#pragma once
// R03 P1 INACTIVE — fixed, caller-owned capture of the legacy R02 JSON metrics.
// Reads existing actual ObservationCounters/ObservationMemo only; no new
// shared state, no live class identities, no metadata iterator, no Player wire.
// A clean two-sample equality check is a *witnessed-stability* condition,
// NEVER a coherent VM-wide epoch, GC/RSS bound, or race-free snapshot proof.
#include "AssemblyShadowR02Diagnostics.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_r02 {

using assembly_shadow_r02::Metric;
using assembly_shadow_r02::ObservationCounters;
using assembly_shadow_r02::ObservationMemo;

enum class Error : uint8_t
{
    None, WrongPhase, CoverageUnavailable, SourceChanged
};

struct Field final { const char* name; Metric metric; };

// This table duplicates the ORIGINAL append field order intentionally.
// Native source CI checks all names/Metric identities against the unchanged
// AssemblyShadowR02Diagnostics.h and executes full-byte parity comparisons.
static constexpr std::array<Field,22> kFields = {{
        {"definitionSearches", Metric::DefinitionSearches},
        {"definitionRowsScanned", Metric::DefinitionRows},
        {"admissionCacheHits", Metric::AdmissionHits},
        {"admissionCacheMisses", Metric::AdmissionMisses},
        {"admissionProofAttempts", Metric::AdmissionBuilds},
        {"admissionProofRejections", Metric::AdmissionRejects},
        {"admissionEntries", Metric::AdmissionEntries},
        {"admissionRetainedBytes", Metric::AdmissionRetainedBytes},
        {"admissionUnready", Metric::AdmissionUnready},
        {"baselineStateChecks", Metric::BaselineChecks},
        {"fieldWorkspaceBuilds", Metric::FieldWorkspaces},
        {"interfaceWorkspaceBuilds", Metric::InterfaceWorkspaces},
        {"layoutCheckCalls", Metric::LayoutChecks},
        {"counterpartCacheHits", Metric::CounterpartHits},
        {"counterpartCacheMisses", Metric::CounterpartMisses},
        {"counterpartEntries", Metric::CounterpartEntries},
        {"absentCounterpartEntries", Metric::CounterpartAbsent},
        {"cacheFixedBytes", Metric::CacheFixedBytes},
        {"counterpartRetainedBytes", Metric::CounterpartRetainedBytes},
        {"genericContextChecks", Metric::GenericContextChecks},
        {"observationLockContentions", Metric::ObservationLockContentions},
        {"observationMemoHits", Metric::ObservationMemoHits},
}};

struct Frame final
{
    std::array<uint64_t,kFields.size()> values{};
    uint64_t droppedClasses=0, droppedThreads=0;
    size_t memoTlsBytes=0, storageBytes=0, threadCapacity=0;
    int diagnosticsLevel=0;
    bool saturated=false;
};

inline Frame Sample() noexcept
{
    Frame f;
    f.diagnosticsLevel=ObservationCounters::Level;
    for(size_t i=0;i<kFields.size();++i)
        f.values[i]=ObservationCounters::Read(kFields[i].metric);
    f.memoTlsBytes=ObservationMemo::TlsBytesPerThread();
    f.storageBytes=ObservationCounters::RetainedBytes();
    f.threadCapacity=ObservationCounters::kThreadCapacity;
    f.droppedThreads=ObservationCounters::DroppedThreads();
    f.saturated=ObservationCounters::Saturated();
    f.droppedClasses=ObservationCounters::Read(Metric::DroppedClasses);
    // Read drop/saturation AGAIN after the final per-thread aggregation.
    // A late observation must not be certified by a previous clean check.
    f.droppedThreads=ObservationCounters::DroppedThreads();
    f.saturated=ObservationCounters::Saturated();
    return f;
}

class Capture final
{
    enum class Stage:uint8_t { New, Sampled, Sealed, Failed };
    Frame owned_{};
    Stage stage_=Stage::New;
    Error error_=Error::None;
    bool Fail(Error error) noexcept
    {
        if(error_==Error::None)error_=error;
        stage_=Stage::Failed;
        return false;
    }
    static bool Available(const Frame& f) noexcept
    {
        // A complete R03 capture must not silently become an R02 disabled,
        // truncated or saturated observation. Original V1 serializes those
        // explicit partial coverage states; this new API FAILS CLOSED instead.
        return f.diagnosticsLevel==2 && f.threadCapacity==128 &&
               f.droppedThreads==0 && f.droppedClasses==0 && !f.saturated;
    }
    static bool Equal(const Frame& a,const Frame& b) noexcept
    {
        if(a.diagnosticsLevel!=b.diagnosticsLevel ||
           a.droppedClasses!=b.droppedClasses ||
           a.droppedThreads!=b.droppedThreads ||
           a.memoTlsBytes!=b.memoTlsBytes ||
           a.storageBytes!=b.storageBytes ||
           a.threadCapacity!=b.threadCapacity ||
           a.saturated!=b.saturated)return false;
        for(size_t i=0;i<a.values.size();++i)
            if(a.values[i]!=b.values[i])return false;
        return true;
    }
public:
    static constexpr size_t kMaximumObjectBytes=512;
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    bool Complete() const noexcept
    {return stage_==Stage::Sealed && error_==Error::None;}
    Error FirstError() const noexcept{return error_;}
    bool Begin(const Frame& before) noexcept
    {
        if(stage_!=Stage::New || error_!=Error::None)
            return Fail(Error::WrongPhase);
        if(!Available(before))return Fail(Error::CoverageUnavailable);
        owned_=before;
        stage_=Stage::Sampled;
        return true;
    }
    bool Seal(const Frame& after) noexcept
    {
        if(stage_!=Stage::Sampled || error_!=Error::None)
            return Fail(Error::WrongPhase);
        if(!Available(after))return Fail(Error::CoverageUnavailable);
        if(!Equal(owned_,after))return Fail(Error::SourceChanged);
        stage_=Stage::Sealed;
        return true;
    }
    bool Get(Frame& out) const noexcept
    {
        if(!Complete())return false;
        out=owned_;
        return true;
    }

    // Append only complete owned values, without calling any counter source.
    // Output is the exact comma-prefixed legacy AppendDiagnostics R02 field.
    // The original encoder/decoder and its coverage predicates are unchanged.
    template<class Output> bool Append(Output& out) const
    {
        if(!Complete())return false;
        out << ",\"r02\":{\"schemaVersion\":1,\"diagnosticsLevel\":" << owned_.diagnosticsLevel;
        for(size_t i=0;i<kFields.size();++i)
            out << ",\"" << kFields[i].name << "\":" << owned_.values[i];
        out << ",\"observationMemoTlsBytesPerThread\":" << owned_.memoTlsBytes
            << ",\"counterStorageBytes\":" << owned_.storageBytes
            << ",\"counterThreadCapacity\":" << owned_.threadCapacity
            << ",\"droppedCounterThreads\":" << owned_.droppedThreads
            << ",\"counterSaturated\":false"
            << ",\"counterCoverage\":\"BoundedComplete\""
            << ",\"classesCoverage\":\"BoundedComplete\""
            << ",\"memoryAccountingAvailable\":true"
            << ",\"memoryAccountingScope\":\"R02StructuresExcludingAllocatorOverhead\"}";
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "P1 bounded R02 metric capture storage changed unexpectedly");

// No VM mutation, no owner acquisition and no source allocations after the
// counter singleton is prewarmed. Each Read sums at most 128 thread slots per
// metric; 2 independent reads do NOT establish an atomic multi-counter epoch.
inline bool TryCapture(Capture& output) noexcept
{
    const Frame before=Sample();
    if(!output.Begin(before))return false;
    const Frame after=Sample();
    return output.Seal(after);
}
}}} // il2cpp::vm::assembly_shadow_bounded_r02
