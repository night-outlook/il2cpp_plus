#pragma once
// R03 P1 INACTIVE: healthy-time source bundle for one bounded general capture.
// Every capture is preallocated and single-owner. This bundle acquires NO VM
// locks and does NOT establish a global VM epoch or Player admission.
#include "AssemblyShadowBoundedGeneralComposer.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_general_live {
namespace composer=assembly_shadow_bounded_general;
namespace diagnostic=assembly_shadow_diagnostic;
namespace frozen=assembly_shadow_frozen_general;
namespace inventory=assembly_shadow_bounded_inventory;
namespace first=assembly_shadow_bounded_first_use;
namespace physical=assembly_shadow_bounded_physical;
namespace classes=assembly_shadow_bounded_classes;

enum class Error : uint8_t
{
    None, MissingSource, MetadataAdmissionUnavailable,
    MalformedStartup, StartupLimit, TransactionFailed, FirstUseFailed,
    PhysicalFailed, ClassesFailed, ObservedSourceChange, ComposeFailed
};

struct Sources
{
    inventory::Capture* transaction=nullptr;
    first::Capture* firstUses=nullptr;
    physical::Capture* ordinaryAssemblies=nullptr;
    classes::Capture* ordinaryClasses=nullptr;
};

class Scratch final
{
public:
    static constexpr size_t kMaximumStartupNames=composer::kMaxStartupNames;
    static constexpr size_t kMaximumStartupTextBytes=frozen::Capture::kMaxBytes;
    // Borrowed startup names are stable generated process-lifetime literals.
    // Each span must be consumed before the outer metadata ticket is released.
    std::array<diagnostic::QuotedText,kMaximumStartupNames> names{};
    size_t count=0;
    size_t scannedBytes=0;
    bool sealed=false;
    Scratch() noexcept=default;
    Scratch(const Scratch&)=delete;
    Scratch& operator=(const Scratch&)=delete;
    bool AddStartup(const char* name) noexcept
    {
        if(sealed || !name || count>=kMaximumStartupNames)return false;
        // Bound each scan by remaining budget, not unbounded strlen().
        const size_t limit=kMaximumStartupTextBytes-scannedBytes;
        size_t n=0;
        while(n<limit && name[n])++n;
        if(n==limit)return false;
        names[count++]={name,n};
        scannedBytes+=n;
        return true;
    }
    bool Seal() noexcept
    {
        if(sealed)return false;
        sealed=true;
        return true;
    }
};

struct Receipt
{
    Error error=Error::None;
    composer::Receipt composed{};
    uint64_t usageGenerationBefore=0,usageGenerationAfter=0;
    uint64_t activeGenerationBefore=0,activeGenerationAfter=0;
    size_t startupCandidates=0,startupTextBytes=0;
    bool componentSourcesComplete=false;
    bool observableSourceFenceStable=false;
    // A read-fence equality does not rule out ABA, concurrent R02 updates,
    // class metadata mutations or cross-domain atomicity violations.
    bool coherentVmEpochProven=false;
    bool runtimeAcceptance=false;
    bool playerExecution=false;
};

inline bool Fail(Receipt& receipt,Error error,frozen::Capture& output) noexcept
{
    if(receipt.error==Error::None)receipt.error=error;
    receipt.componentSourcesComplete=false;
    receipt.observableSourceFenceStable=false;
    receipt.composed.serializedBytes=0;
    output.RejectInvalidSource();
    return false;
}
static_assert(sizeof(Scratch)<=70000u,
              "P1 startup candidate scratch storage unexpectedly increased");
}}} // il2cpp::vm::assembly_shadow_bounded_general_live
