// R03 P1: original live R02 counter access serialized through either output
// backend. This tests a formatting primitive, not coherent metadata capture.
#include "AssemblyShadowR02Diagnostics.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include "AssemblyShadowFrozenR02Capture.h"
#include "AssemblyShadowFrozenExecutionCapture.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
using namespace il2cpp::vm;
namespace r02 = assembly_shadow_r02;
namespace fmt = assembly_shadow_diagnostic;

struct BoundedBuffer {
    std::string bytes;
    size_t writeCalls = 0;
    size_t failAt = 0;
    static bool Append(void* context,const char* ptr,size_t n) noexcept {
        auto& dest = *static_cast<BoundedBuffer*>(context);
        ++dest.writeCalls;
        if (dest.writeCalls == dest.failAt) return false;
        try { dest.bytes.append(ptr,n); return true; } catch (...) { return false; }
    }
    fmt::Sink Sink() noexcept { return {this,Append}; }
};
static std::string Legacy() {
    std::ostringstream stream;
    r02::AppendDiagnostics(stream);
    return stream.str();
}
static fmt::WriteResult Bounded(BoundedBuffer& buffer, size_t bytes) {
    fmt::Writer writer(buffer.Sink(),bytes);
    r02::AppendDiagnostics(writer);
    return writer.Finish();
}
static void FreezeAndCompare() {
    const std::string expected=Legacy();
    assembly_shadow_frozen_r02::Capture frozen;
    assert(frozen.Freeze());
    assert(frozen.Complete() && frozen.Bytes()==expected.size());
    assert(std::string(frozen.Data(),frozen.Bytes())==expected);
    if constexpr (r02::ObservationCounters::Level > 0)
        r02::ObservationCounters::Add(r02::Metric::DefinitionRows,11);
    // Captured bytes remain owned even after counters change.
    assert(std::string(frozen.Data(),frozen.Bytes())==expected);
    assert(!frozen.Freeze()); // No reuse/synthetic duplicate capture
    assert(!frozen.Complete() && frozen.Data()==nullptr && frozen.Bytes()==0);
}
static void ComposeFrozenExecution() {
    // Prepared host-only composition. Full live VM capture and lock epochs are
    // NOT represented; the R02 suffix is frozen before execution serialization.
    assembly_shadow_frozen_r02::Capture suffix;
    assert(suffix.Freeze());
    assembly_shadow_execution_diagnostic::Header header;
    header.stateCode=9;
    header.state="FailedAfterCommit";
    std::ostringstream legacy;
    assembly_shadow_execution_diagnostic::Begin(legacy,header);
    assembly_shadow_execution_diagnostic::EndClasses(legacy);
    legacy.write(suffix.Data(),static_cast<std::streamsize>(suffix.Bytes()));
    legacy<<'}';
    auto execution=std::make_unique<assembly_shadow_frozen_execution::Capture>();
    assert(execution->Begin(header));
    assert(execution->SetR02Suffix({suffix.Data(),suffix.Bytes()}));
    assert(execution->Freeze()); // No synthetic missing-R02 exemption.
    BoundedBuffer final;
    const auto result=execution->SerializeTo(final.Sink(),legacy.str().size());
    assert(result.complete && result.bytesAccepted==legacy.str().size());
    assert(final.bytes==legacy.str());
    const auto original=final.bytes;
    if constexpr (r02::ObservationCounters::Level>0)
        r02::ObservationCounters::Add(r02::Metric::DefinitionSearches,17);
    BoundedBuffer repeated;
    assert(execution->SerializeTo(repeated.Sink(),original.size()).complete);
    assert(repeated.bytes==original);
}
static void Compare() {
    auto legacy = Legacy();
    BoundedBuffer exactly;
    auto receipt = Bounded(exactly,legacy.size());
    assert(receipt.complete && receipt.bytesAccepted==legacy.size());
    assert(receipt.error==fmt::WriteError::None && legacy==exactly.bytes);
    assert(legacy.find(",\"r02\":{\"schemaVersion\":1") == 0);
    assert(legacy.find("\"memoryAccountingScope\":\"R02StructuresExcludingAllocatorOverhead\"}")!=std::string::npos);
    BoundedBuffer shortSink;
    auto shortReceipt=Bounded(shortSink,legacy.size()-1);
    assert(!shortReceipt.complete && shortReceipt.error==fmt::WriteError::ByteLimit);
    assert(shortReceipt.bytesAccepted<legacy.size());
    BoundedBuffer failure;
    failure.failAt=3;
    auto failReceipt=Bounded(failure,legacy.size());
    assert(!failReceipt.complete && failReceipt.error==fmt::WriteError::SinkFailure);
    assert(failReceipt.bytesAccepted<legacy.size());
}
int main() {
    Compare();
    FreezeAndCompare();
    ComposeFrozenExecution();
    if constexpr (r02::ObservationCounters::Level > 0) {
        r02::ObservationCounters::Add(r02::Metric::DefinitionSearches,1);
        r02::ObservationCounters::Add(r02::Metric::AdmissionEntries,123);
        r02::ObservationCounters::Add(r02::Metric::DroppedClasses,1);
        r02::ObservationCounters::Add(r02::Metric::CounterpartRetainedBytes,UINT64_MAX);
        Compare();
        FreezeAndCompare();
        const auto json=Legacy();
        assert(json.find("\"definitionSearches\":1") != std::string::npos);
        assert(json.find("\"admissionEntries\":123") != std::string::npos);
        assert(json.find("\"counterpartRetainedBytes\":18446744073709551615") != std::string::npos);
        assert(json.find(r02::ObservationCounters::Level < 2 ? "\"classesCoverage\":\"Disabled\"" : "\"classesCoverage\":\"Truncated\"") != std::string::npos);
    } else {
        const auto json=Legacy();
        assert(json.find("\"diagnosticsLevel\":0")!=std::string::npos);
        assert(json.find("\"classesCoverage\":\"Disabled\"")!=std::string::npos);
    }
    std::cout << "R02 legacy/fixed-sink exact byte parity, negative sinks, observation level "
              << r02::ObservationCounters::Level << " passed\n";
    return 0;
}
