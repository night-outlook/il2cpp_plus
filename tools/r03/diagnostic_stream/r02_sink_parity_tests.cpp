// R03 P1: original live R02 counter access serialized through either output
// backend. This tests a formatting primitive, not coherent metadata capture.
#include "AssemblyShadowR02Diagnostics.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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
    if constexpr (r02::ObservationCounters::Level > 0) {
        r02::ObservationCounters::Add(r02::Metric::DefinitionSearches,1);
        r02::ObservationCounters::Add(r02::Metric::AdmissionEntries,123);
        r02::ObservationCounters::Add(r02::Metric::DroppedClasses,1);
        r02::ObservationCounters::Add(r02::Metric::CounterpartRetainedBytes,UINT64_MAX);
        Compare();
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
