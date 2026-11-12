#pragma once
// R03 B2 INACTIVE: one-shot, pure byte comparison of an existing original V1
// execution JSON and an already frozen owned bounded execution JSON.
// Neither source is authenticated by this comparator. Even exact equality
// across two observations is NOT a coherent VM epoch, terminal-safe capture,
// complete canonical TypeKey coverage or native V2 field15 acceptance.
// The owner must hold valid immutable input lifetimes for the entire call.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_execution_v1_parity {
enum class Error : uint8_t {
    None, Reused, MissingInput, OutsideBudget, DifferentLength,
    DifferentByte, UnavailableLegacy, UnavailableBounded
};
struct View { const char* data=nullptr; size_t bytes=0; };
struct Receipt {
    Error error=Error::None;
    size_t legacyBytes=0, boundedBytes=0;
    size_t firstDifferentOffset=0; // valid only if error==DifferentByte/Length
    bool exactBytesObserved=false;
    bool twoIndependentObservationsOnly=false;
    bool coherentVmEpochProven=false;
    bool fullCanonicalTypeKeysProven=false;
    bool terminalSafeFinalExecutionDiagnostics=false;
    bool nativeV2TransportImplemented=false;
    bool runtimeAcceptance=false;
    bool R03Accepted=false;
    bool H2Passed=false;
};
class Probe final {
public:
    static constexpr size_t kMaximumBytes=3932160;
private:
    bool consumed_=false;
    Receipt receipt_{};
    bool Reject(Error error) noexcept {
        receipt_.error=error;
        receipt_.exactBytesObserved=false;
        receipt_.twoIndependentObservationsOnly=false;
        return false;
    }
public:
    Probe() noexcept=default;
    Probe(const Probe&)=delete;
    Probe& operator=(const Probe&)=delete;
    Probe(Probe&&)=delete;
    Probe& operator=(Probe&&)=delete;
    const Receipt& Result() const noexcept {return receipt_;}
    bool Compare(View legacy,View bounded) noexcept {
        if(consumed_)return Reject(Error::Reused);
        consumed_=true;
        receipt_=Receipt{};
        if(!legacy.data||!bounded.data||!legacy.bytes||!bounded.bytes)
            return Reject(Error::MissingInput);
        if(legacy.bytes>kMaximumBytes||bounded.bytes>kMaximumBytes)
            return Reject(Error::OutsideBudget);
        receipt_.legacyBytes=legacy.bytes;
        receipt_.boundedBytes=bounded.bytes;
        const size_t count=legacy.bytes<bounded.bytes?
            legacy.bytes:bounded.bytes;
        for(size_t i=0;i<count;++i) {
            if(legacy.data[i]!=bounded.data[i]) {
                receipt_.firstDifferentOffset=i;
                return Reject(Error::DifferentByte);
            }
        }
        if(legacy.bytes!=bounded.bytes) {
            receipt_.firstDifferentOffset=count;
            return Reject(Error::DifferentLength);
        }
        receipt_.exactBytesObserved=true;
        receipt_.twoIndependentObservationsOnly=true;
        return true;
    }
    bool RejectLegacy() noexcept {
        if(consumed_)return Reject(Error::Reused);
        consumed_=true;
        return Reject(Error::UnavailableLegacy);
    }
    bool RejectBounded() noexcept {
        if(consumed_)return Reject(Error::Reused);
        consumed_=true;
        return Reject(Error::UnavailableBounded);
    }
};
static_assert(sizeof(Probe)<=128,"R03 B2 pure parity state unexpectedly grew");
}}} // namespaces
