#pragma once
// P1 INACTIVE: fail-closed live execution/R02 ZERO-CLASS subset.
// This is NOT complete execution diagnostics, a full class census,
// a coherent VM-wide epoch, or an authorized terminal/Player getter.
#include "AssemblyShadowFrozenExecutionRecord.h"
#include "AssemblyShadowMetadataLifetime.h"
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_execution_empty_live {
namespace r02=assembly_shadow_r02;
namespace frozen=assembly_shadow_frozen_execution_record;
enum class Error : uint8_t
{
    None, ReusedOutput, MetadataAdmissionUnavailable,
    InvalidClassObservation, ClassRowsNotSupported,
    CounterCaptureFailed, IncompleteCoverage, ObservedSourceChange,
    BeginFailed, FreezeFailed
};
struct Receipt
{
    Error error=Error::None;
    r02::SnapshotReceipt countersBefore{}, countersAfter{};
    size_t observedClassesBefore=0, observedClassesAfter=0;
    uint64_t activeGenerationBefore=0, activeGenerationAfter=0;
    bool zeroClassSubsetComplete=false;
    bool observedSourceFenceStable=false;
    bool classPointerOriginProven=false;
    bool coherentVmEpochProven=false;
    bool originalLiveGettersReplaced=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};
inline bool Fail(Receipt& receipt, Error error, frozen::Record& record) noexcept
{
    if(receipt.error==Error::None)receipt.error=error;
    receipt.zeroClassSubsetComplete=false;
    receipt.observedSourceFenceStable=false;
    record.RejectInvalidSource();
    return false;
}
// Compare ALL typed R02 values and metadata used by the frozen serializer.
// Equality cannot rule out ABA or unrelated VM writes.
inline bool Equivalent(const r02::Snapshot& a,const r02::Snapshot& b) noexcept
{
    if(!a.Complete() || !b.Complete() || a.DiagnosticsLevel()!=b.DiagnosticsLevel() ||
       a.ThreadCapacity()!=b.ThreadCapacity() ||
       a.MemoTlsBytesPerThread()!=b.MemoTlsBytesPerThread() ||
       a.RetainedBytes()!=b.RetainedBytes() ||
       a.DroppedThreads()!=b.DroppedThreads() || a.Saturated()!=b.Saturated())return false;
    for(size_t i=0;i<r02::Snapshot::kMetricCount;++i)
        if(a.Read(static_cast<r02::Metric>(i))!=b.Read(static_cast<r02::Metric>(i)))return false;
    return true;
}
inline bool CompleteCoverage(const r02::Snapshot& sample) noexcept
{
    return sample.Complete() &&
        (sample.DiagnosticsLevel()==0 ||
         (!sample.DroppedThreads() && !sample.Saturated() &&
          !sample.Read(r02::Metric::DroppedClasses)));
}
}}} // il2cpp::vm::assembly_shadow_bounded_execution_empty_live
