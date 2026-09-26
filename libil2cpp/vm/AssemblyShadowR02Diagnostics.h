#pragma once
#include "AssemblyShadowObservationCounters.h"
#include "AssemblyShadowObservationMemo.h"
#include <ostream>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

// Additive diagnostics only; no type resolution, managed allocation or cache
// construction. Byte accounting excludes allocator overhead and older caches.
inline void AppendDiagnostics(std::ostream& output)
{
    output << ",\"r02\":{\"schemaVersion\":1,\"diagnosticsLevel\":" << ObservationCounters::Level;
    const struct { const char* name; Metric metric; } fields[] = {
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
        {"observationMemoHits", Metric::ObservationMemoHits}
    };
    for (const auto& field : fields)
        output << ",\"" << field.name << "\":" << ObservationCounters::Read(field.metric);
    output << ",\"observationMemoTlsBytesPerThread\":" << ObservationMemo::TlsBytesPerThread()
        << ",\"counterStorageBytes\":" << ObservationCounters::RetainedBytes()
        << ",\"counterThreadCapacity\":" << ObservationCounters::kThreadCapacity
        << ",\"droppedCounterThreads\":" << ObservationCounters::DroppedThreads()
        << ",\"counterSaturated\":" << (ObservationCounters::Saturated() ? "true" : "false")
        << ",\"counterCoverage\":\"" << ObservationCounters::Coverage() << "\""
        << ",\"classesCoverage\":\"" << (ObservationCounters::Level < 2 ? "Disabled" :
            ObservationCounters::DroppedThreads() || ObservationCounters::Read(Metric::DroppedClasses) ? "Truncated" :
            ObservationCounters::Saturated() ? "Saturated" : "BoundedComplete") << "\""
        << ",\"memoryAccountingAvailable\":" << (ObservationCounters::Level != 0 &&
            !ObservationCounters::DroppedThreads() && !ObservationCounters::Saturated() ? "true" : "false")
        << ",\"memoryAccountingScope\":\"R02StructuresExcludingAllocatorOverhead\"}";
}

}}}
