#pragma once
// R03 P1: owned R02 counter values. Two finite read passes reject witnessed
// changes; they are NOT a coherent VM epoch, a class census or a teardown gate.
// No writer instrumentation, retries, heap buffers or managed calls are added.
#include "AssemblyShadowObservationCounters.h"
#include "AssemblyShadowObservationMemo.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

// Preserve the legacy live formatter's reads through this zero-state adapter.
struct LiveCounterSource
{
    int DiagnosticsLevel() const noexcept { return ObservationCounters::Level; }
    size_t ThreadCapacity() const noexcept { return ObservationCounters::kThreadCapacity; }
    size_t MemoTlsBytesPerThread() const noexcept { return ObservationMemo::TlsBytesPerThread(); }
    size_t RetainedBytes() const noexcept { return ObservationCounters::RetainedBytes(); }
    uint64_t Read(Metric metric, std::memory_order order=std::memory_order_relaxed) const noexcept
    { return ObservationCounters::Read(metric,order); }
    uint64_t DroppedThreads() const noexcept { return ObservationCounters::DroppedThreads(); }
    bool Saturated() const noexcept { return ObservationCounters::Saturated(); }
    const char* Coverage() const noexcept { return ObservationCounters::Coverage(); }
};

enum class SnapshotError : uint8_t { None, InvalidSample, ObservedCounterChange };
struct SnapshotReceipt
{
    SnapshotError error=SnapshotError::None;
    size_t samples=0, metricReads=0;
    bool observedReadFenceStable=false;
    bool coherentVmEpochProven=false;
    bool runtimeAcceptance=false;
};

class Snapshot final
{
public:
    static constexpr size_t kMetricCount=static_cast<size_t>(Metric::Count);
    static constexpr size_t kMaximumObjectBytes=512;
    static constexpr size_t kMaximumSlotValueReads=2*kMetricCount*ObservationCounters::kThreadCapacity;
private:
    std::array<uint64_t,kMetricCount> values_{};
    uint64_t dropped_=0;
    size_t storageBytes_=0, memoBytes_=0, threadCapacity_=0;
    int level_=-1;
    bool saturated_=false, complete_=false;

    template<class Source>
    static Snapshot Sample(const Source& source) noexcept
    {
        Snapshot result;
        result.level_=source.DiagnosticsLevel();
        result.threadCapacity_=source.ThreadCapacity();
        result.storageBytes_=source.RetainedBytes();
        result.memoBytes_=source.MemoTlsBytesPerThread();
        // Match the original execution getter's subset-before-total acquire
        // ordering. A total-first loop can manufacture subset > total while
        // an observer publishes its total and then its subset increment.
        for(size_t index=0;index<kMetricCount;++index)
        {
            const auto metric=static_cast<Metric>(index);
            if(metric==Metric::MethodChecks || metric==Metric::Transformations)continue;
            const bool subset=metric==Metric::ShadowMethodChecks ||
                metric==Metric::RejectedBaselineMethods || metric==Metric::ShadowTransformations;
            result.values_[index]=source.Read(metric,subset ? std::memory_order_acquire : std::memory_order_relaxed);
        }
        result.values_[static_cast<size_t>(Metric::MethodChecks)]=source.Read(Metric::MethodChecks);
        result.values_[static_cast<size_t>(Metric::Transformations)]=source.Read(Metric::Transformations);
        // Read() can set the existing aggregate-saturation flag. Sample it
        // AFTER all metric sums; never label a saturated sample complete-coverage.
        result.dropped_=source.DroppedThreads();
        result.saturated_=source.Saturated();
        return result;
    }
    bool ValidSample() const noexcept
    {
        if(level_<0 || level_>2 || threadCapacity_!=ObservationCounters::kThreadCapacity)return false;
        if(Read(Metric::ShadowMethodChecks)>Read(Metric::MethodChecks) ||
           Read(Metric::RejectedBaselineMethods)>Read(Metric::MethodChecks) ||
           Read(Metric::ShadowTransformations)>Read(Metric::Transformations))return false;
        if(level_<2 && memoBytes_)return false;
        if(level_==0)
        {
            if(dropped_ || saturated_ || storageBytes_)return false;
            for(uint64_t value:values_)if(value)return false;
        }
        return true;
    }
    bool SameValues(const Snapshot& other) const noexcept
    {
        return values_==other.values_ && dropped_==other.dropped_ &&
            storageBytes_==other.storageBytes_ && memoBytes_==other.memoBytes_ &&
            threadCapacity_==other.threadCapacity_ && level_==other.level_ && saturated_==other.saturated_;
    }
public:
    Snapshot() noexcept=default;
    bool Complete() const noexcept { return complete_; }
    int DiagnosticsLevel() const noexcept { return level_; }
    size_t ThreadCapacity() const noexcept { return threadCapacity_; }
    size_t MemoTlsBytesPerThread() const noexcept { return memoBytes_; }
    size_t RetainedBytes() const noexcept { return storageBytes_; }
    uint64_t DroppedThreads() const noexcept { return dropped_; }
    bool Saturated() const noexcept { return saturated_; }
    uint64_t Read(Metric metric) const noexcept
    {
        const size_t index=static_cast<size_t>(metric);
        return index<kMetricCount ? values_[index] : 0;
    }
    const char* Coverage() const noexcept
    { return level_==0 ? "Disabled" : dropped_ ? "Truncated" : saturated_ ? "Saturated" : "BoundedComplete"; }

    // Compile-time source seam for deterministic host controls. Production
    // calls TryCaptureCurrent, whose adapter reads the REAL R02 counters.
    // A failure clears destination; no stale earlier success can survive.
    template<class Source>
    static bool TryCaptureFrom(const Source& source,Snapshot& destination,SnapshotReceipt& receipt) noexcept
    {
        destination=Snapshot{};
        receipt=SnapshotReceipt{};
        const Snapshot before=Sample(source);
        const Snapshot after=Sample(source);
        receipt.samples=2;
        receipt.metricReads=2*kMetricCount;
        if(!before.ValidSample() || !after.ValidSample())
        { receipt.error=SnapshotError::InvalidSample;return false; }
        if(!before.SameValues(after))
        { receipt.error=SnapshotError::ObservedCounterChange;return false; }
        destination=before;
        destination.complete_=true;
        receipt.observedReadFenceStable=true;
        return true;
    }
    static bool TryCaptureCurrent(Snapshot& destination,SnapshotReceipt& receipt) noexcept
    { return TryCaptureFrom(LiveCounterSource{},destination,receipt); }
};
static_assert(sizeof(Snapshot)<=Snapshot::kMaximumObjectBytes,"R02 owned snapshot storage bound changed");
}}} // il2cpp::vm::assembly_shadow_r02
