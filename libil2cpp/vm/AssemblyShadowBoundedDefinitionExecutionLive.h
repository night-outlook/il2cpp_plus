#pragma once
// R03 B2 INACTIVE genuine native observed-class source, restricted to
// physically matched AOT TypeDef definitions and bounded GENERICINST with
// independently owner-verified concrete AOT TypeDef args from matched class
// images. NOT a complete execution-V1 source, V2 field15, coherent VM epoch,
// or terminal/release recovery API.
// Caller owns/preallocates all 3 objects while healthy, one thread only.
// No registered Il2CppClass* escapes the synchronous physical enumerator.
#include "AssemblyShadowOwnedExecutionRows.h"
#include "AssemblyShadowPhysicalMembership.h"
#include "AssemblyShadowObservedPhysicalImages.h"
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
    SourceOutsideBudget, PhysicalImageLimit, UnadmittedMetadataOwner,
    AmbiguousMetadataOwner
};

// Bounded source limit: deliberately caps metadata-owner handle lookups per
// class and per capture while holding g_MetadataLock. Not a performance claim.
static constexpr size_t kMaximumAotImageTypes = 8192;
static constexpr size_t kMaximumRegisteredAotImages = 1024;
static constexpr size_t kMaximumTotalMetadataLookups = 65536;

struct Receipt {
    Error error = Error::None;
    size_t registered=0, physicallyMatched=0, serializedRows=0;
    size_t ownedBytes=0, metadataLookups=0, rejectedUnsupported=0;
    size_t admittedPhysicalImages=0; // matched observed class images
    size_t registeredPhysicalAotImages=0; // separate raw Assembly table
    bool registeredImageSetStable=false; // two lock-scoped snapshots only
    bool usedCrossImageArgument=false;
    bool usedUnobservedAotArgumentImage=false;
    bool usedUnicodeAssemblyFold=false;
    // Diagnostic-only subset coverage. All are false on rejected source.
    bool usedNestedGenericArgument=false;
    bool usedPointerArgument=false;
    bool usedSzArrayArgument=false;
    bool usedMdArrayArgument=false;
    bool usedQualifiedArgument=false;
    size_t compositeNodesVisited=0,maximumCompositeDepth=0;
    uint64_t registryRevisionBefore=0, registryRevisionAfter=0;
    uint64_t activeGenerationBefore=0, activeGenerationAfter=0;
    uint32_t admittedMetadataEpoch=0;
    // A TRUE value only binds primitive rows to one observed class source
    // and a physical enumeration in the initial metadata session.
    bool callbackPhysicalMembershipProven=false;
    bool restrictedTypeDefSubsetProven=false;
    // Only the registry identities/revision and R02 sample are checked twice.
    // Cctor, static storage and metadata mutation have no all-VM fence here.
    bool observedRegistryAndCounterFenceStable=false;
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
    receipt.usedCrossImageArgument=false;
    receipt.usedUnobservedAotArgumentImage=false;
    receipt.usedUnicodeAssemblyFold=false;
    receipt.usedNestedGenericArgument=false;
    receipt.usedPointerArgument=false;
    receipt.usedSzArrayArgument=false;
    receipt.usedMdArrayArgument=false;
    receipt.usedQualifiedArgument=false;
    receipt.compositeNodesVisited=0;
    receipt.maximumCompositeDepth=0;
    receipt.registeredImageSetStable=false;
    receipt.observedRegistryAndCounterFenceStable=false;
    receipt.completeArbitraryTypeKeys=false;
    receipt.completeExecutionDiagnostics=false;
    rows.Revoke();
    membership.Scrub();
    record.RejectInvalidSource();
    return false;
}

}}} // il2cpp::vm::assembly_shadow_bounded_definition_execution
