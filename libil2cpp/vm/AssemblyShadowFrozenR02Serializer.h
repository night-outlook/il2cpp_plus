#pragma once
#include "AssemblyShadowR02Snapshot.h"
#include <ostream>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

// Pure formatter for a completed owned R02 value. The legacy live formatter
// remains untouched; reference tests compare EVERY field, value and byte order.
// A true return means the source was complete, not that an arbitrary sink
// succeeded. The owner must still check Writer::Finish(). No live reads here.
template<class Output>
inline bool AppendFrozenDiagnostics(Output& output,const Snapshot& snapshot)
{
    if(!snapshot.Complete())return false;
    output << ",\"r02\":{\"schemaVersion\":1,\"diagnosticsLevel\":" << snapshot.DiagnosticsLevel();
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
        output << ",\"" << field.name << "\":" << snapshot.Read(field.metric);
    output << ",\"observationMemoTlsBytesPerThread\":" << snapshot.MemoTlsBytesPerThread()
        << ",\"counterStorageBytes\":" << snapshot.RetainedBytes()
        << ",\"counterThreadCapacity\":" << snapshot.ThreadCapacity()
        << ",\"droppedCounterThreads\":" << snapshot.DroppedThreads()
        << ",\"counterSaturated\":" << (snapshot.Saturated() ? "true" : "false")
        << ",\"counterCoverage\":\"" << snapshot.Coverage() << "\""
        << ",\"classesCoverage\":\"" << (snapshot.DiagnosticsLevel() < 2 ? "Disabled" :
            snapshot.DroppedThreads() || snapshot.Read(Metric::DroppedClasses) ? "Truncated" :
            snapshot.Saturated() ? "Saturated" : "BoundedComplete") << "\""
        << ",\"memoryAccountingAvailable\":" << (snapshot.DiagnosticsLevel() != 0 &&
            !snapshot.DroppedThreads() && !snapshot.Saturated() ? "true" : "false")
        << ",\"memoryAccountingScope\":\"R02StructuresExcludingAllocatorOverhead\"}";
    return true;
}

}}}
