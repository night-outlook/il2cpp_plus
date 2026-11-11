#pragma once
// R03 B2 INACTIVE genuine native observed-class source, restricted to ordinary
// physical AOT TypeDef definitions. NOT a full execution-V1 source, V2
// field15, a global VM epoch, or a terminal/release recovery API.
// Caller owns/preallocates all 3 objects while healthy, one thread only.
// No registered Il2CppClass* escapes the synchronous physical enumerator.
#include "AssemblyShadowOwnedExecutionRows.h"
#include "AssemblyShadowPhysicalMembership.h"
#include "AssemblyShadowFrozenExecutionRecord.h"
#include "AssemblyShadowMetadataLifetime.h"
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_definition_execution {

namespace owned = ::r03_b2_owned_execution_rows;
namespace physical = assembly_shadow_physical_membership;
namespace frozen = assembly_shadow_frozen_execution_record;

enum class Error : uint8_t {
    None, ReusedSource, NoMetadataAdmission, InvalidRegistry,
    DuplicateRegistration, BadPhysicalEnumeration, UnmatchedClass,
    SourceChanged, UnsupportedImage, UnsupportedTypeShape,
    UnsupportedMetadataAncestry, InvalidTypeIdentity, TypeKeyFailed,
    RowsFailed, CounterUnavailable, CounterChanged, FrozenRecordFailed,
    SourceOutsideBudget
};

// Bounded source limit: deliberately caps metadata-owner handle lookups per
// class and per capture while holding g_MetadataLock. Not a performance claim.
static constexpr size_t kMaximumAotImageTypes = 8192;
static constexpr size_t kMaximumTotalMetadataLookups = 65536;

struct Receipt {
    Error error = Error::None;
    size_t registered=0, physicallyMatched=0, serializedRows=0;
    size_t ownedBytes=0, metadataLookups=0, rejectedUnsupported=0;
    uint64_t registryRevisionBefore=0, registryRevisionAfter=0;
    uint64_t activeGenerationBefore=0, activeGenerationAfter=0;
    uint32_t admittedMetadataEpoch=0;
    // A TRUE value only binds primitive rows to one observed class source
    // and a physical enumeration in the initial metadata session.
    bool callbackPhysicalMembershipProven=false;
    bool restrictedTypeDefSubsetProven=false;
    bool sourceFenceStable=false;
    bool completeArbitraryTypeKeys=false;
    bool completeExecutionDiagnostics=false;
    bool coherentVmEpochProven=false;
    bool nativeV2TransportImplemented=false;
    bool runtimeAcceptance=false;
    bool R03Accepted=false;
    bool H2Passed=false;
};

inline bool Reject(Receipt& receipt,Error reason,
                   physical::Scratch& membership,owned::Rows& rows,
                   frozen::Record& record) noexcept {
    if(receipt.error==Error::None)receipt.error=reason;
    receipt.callbackPhysicalMembershipProven=false;
    receipt.restrictedTypeDefSubsetProven=false;
    receipt.sourceFenceStable=false;
    receipt.completeArbitraryTypeKeys=false;
    receipt.completeExecutionDiagnostics=false;
    rows.Revoke();
    membership.Scrub();
    record.RejectInvalidSource();
    return false;
}

}}} // il2cpp::vm::assembly_shadow_bounded_definition_execution
