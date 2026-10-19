#pragma once
// R03 P1 INACTIVE — bounded, IDENTITY-ONLY execution-class census.
// No metadata name/type pointer is dereferenced or retained here. The global
// original class observation table is NOT modified. The internal fingerprints
// are NOT exposed as a stable/cryptographic pointer identity or transport.
// Two equal samples can exclude an OBSERVED table change only; they do not
// establish class-origin provenance, full execution V1 rows, a global VM epoch,
// late native/shutdown safety, or runtime acceptance.
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_execution_census {

enum class Error : uint8_t
{
    None, WrongPhase, OwnerUnavailable, IncompleteCoverage,
    TooManyClasses, RegistryMismatch, ObservedChange
};

struct Sample
{
    size_t declaredCount=0, presentCount=0;
    size_t tableCapacity=0;
    uint64_t fingerprint=0; // INTERNAL equality witness; not exposed by Get.
    uint64_t droppedObservations=0, droppedCounterThreads=0;
    bool countersSaturated=false;
    int diagnosticsLevel=0;
};

// Provenance is deliberately narrow: only the number of stored table entries
// survives the capture. The class addresses and their hash never leave here.
struct Result
{
    size_t distinctClasses=0, scannedSlots=0;
    bool classIdentitySourceOnly=true;
    bool classPointerOriginProven=false;
    bool coherentVmEpochProven=false;
    bool classTypeKeyCaptured=false;
    bool runtimeAcceptance=false;
};

class Capture final
{
    enum class Phase:uint8_t { New, Sampled, Sealed, Failed };
    Sample captured_{};
    Phase phase_=Phase::New;
    Error first_=Error::None;
    bool Fail(Error error) noexcept
    {
        if(first_==Error::None)first_=error;
        phase_=Phase::Failed;
        return false;
    }
    static bool Valid(const Sample& s,size_t maximumClasses) noexcept
    {
        return s.diagnosticsLevel==2 && s.droppedObservations==0 &&
               s.droppedCounterThreads==0 && !s.countersSaturated &&
               s.tableCapacity==maximumClasses*2;
    }
public:
    static constexpr size_t kMaxClassIdentities=1024;
    static constexpr size_t kExpectedTableSlots=kMaxClassIdentities*2;
    static constexpr size_t kMaximumObjectBytes=128;
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    bool Complete() const noexcept
    {return phase_==Phase::Sealed && first_==Error::None;}
    Error FirstError() const noexcept{return first_;}
    bool RejectOwnerUnavailable() noexcept {return Fail(Error::OwnerUnavailable);}
    bool Begin(const Sample& before) noexcept
    {
        if(phase_!=Phase::New || first_!=Error::None)
            return Fail(Error::WrongPhase);
        if(before.declaredCount>kMaxClassIdentities ||
           before.presentCount>kMaxClassIdentities)
            return Fail(Error::TooManyClasses);
        if(!Valid(before,kMaxClassIdentities))
            return Fail(Error::IncompleteCoverage);
        if(before.presentCount!=before.declaredCount)
            return Fail(Error::RegistryMismatch);
        captured_=before;
        phase_=Phase::Sampled;
        return true;
    }
    bool Seal(const Sample& after) noexcept
    {
        if(phase_!=Phase::Sampled || first_!=Error::None)
            return Fail(Error::WrongPhase);
        if(after.declaredCount>kMaxClassIdentities ||
           after.presentCount>kMaxClassIdentities)
            return Fail(Error::TooManyClasses);
        if(!Valid(after,kMaxClassIdentities))
            return Fail(Error::IncompleteCoverage);
        if(after.presentCount!=after.declaredCount)
            return Fail(Error::RegistryMismatch);
        if(after.presentCount!=captured_.presentCount ||
           after.declaredCount!=captured_.declaredCount ||
           after.fingerprint!=captured_.fingerprint ||
           after.tableCapacity!=captured_.tableCapacity)
            return Fail(Error::ObservedChange);
        phase_=Phase::Sealed;
        return true;
    }
    bool Get(Result& output) const noexcept
    {
        if(!Complete())return false;
        output=Result{};
        output.distinctClasses=captured_.presentCount;
        output.scannedSlots=captured_.tableCapacity;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "R03 P1 identity-only census exceeded reviewed object cap");

}}} // il2cpp::vm::assembly_shadow_bounded_execution_census
