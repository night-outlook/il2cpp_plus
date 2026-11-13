#pragma once
// R03 B2 INACTIVE physical observed-class source: restricted AOT TypeDefs,
// bounded composite/Unicode/default-primitive keys with registered physical
// AOT owner proof; simple nongeneric/nonnested interpreter TypeDefs are
// separately first-owner-matched to ORIGINAL V1 token-valid physical images.
// Interpreter composites/VAR/MVAR remain unsupported.
// NOT complete execution V1 field15, terminal-safe V2, coherent VM epoch
// or a Player/recovery API.
// Caller owns/preallocates all 3 objects while healthy, one thread only.
// No registered Il2CppClass* escapes the synchronous physical enumerator.
#include "AssemblyShadowOwnedExecutionRows.h"
#include "AssemblyShadowPhysicalMembership.h"
#include "AssemblyShadowObservedPhysicalImages.h"
#include "AssemblyShadowOriginalV1OwnerJoin.h"
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
    AmbiguousMetadataOwner, OriginalV1ImageSourceUnavailable,
    OriginalV1OwnerMissing, OriginalV1FirstOwnerConflict,
    OriginalV1LookupBudget, ObservedClassFactsChanged,
    OriginalV1ImageFactsChanged, ObservedClassRootChanged,
    OriginalV1TypeDefContentChanged
};

// Bounded source limit: deliberately caps metadata-owner handle lookups per
// class and per capture while holding g_MetadataLock. Not a performance claim.
static constexpr size_t kMaximumAotImageTypes = 8192;
static constexpr size_t kMaximumRegisteredAotImages = 1024;
static constexpr size_t kMaximumOriginalV1Images = 1024;
static constexpr size_t kMaximumTotalMetadataLookups = 65536;
static constexpr size_t kMaximumNonRootTypeDefOwnerPairs = 128; // scoped subset
static constexpr size_t kMaximumNonRootTypeDefNameBytes = 64; // exact bytes per field

struct Receipt {
    Error error = Error::None;
    size_t registered=0, physicallyMatched=0, serializedRows=0;
    size_t ownedBytes=0, metadataLookups=0, rejectedUnsupported=0;
    size_t admittedPhysicalImages=0; // matched observed class images
    size_t registeredPhysicalAotImages=0; // separate raw Assembly table
    bool registeredImageSetStable=false; // two lock-scoped snapshots only
    size_t originalV1Images=0; // token-valid, interpreter INCLUDED
    size_t originalV1MetadataLookups=0, originalV1HandlesMatched=0;
    bool originalV1ImageSetStable=false; // independently revisited image order
    size_t originalV1ImageFactsMatched=0; // physical visits, not VM epoch
    bool originalV1ImageFactsTwoPassStable=false; // typeCount/tokens only
    bool originalV1FirstEmplaceOwnerMatched=false; // admitted handles ONLY
    size_t originalV1RootOwnerEndPassMatched=0; // root-only recheck
    size_t originalV1RootOwnerEndPassLookups=0; // separate bounded budget
    bool originalV1RootOwnerEndPassStable=false; // observational only
    size_t originalV1NonRootTypeDefOwnersRecorded=0; // exact handle + owner
    size_t originalV1NonRootTypeDefOwnersRechecked=0; // independent final read
    size_t originalV1NonRootTypeDefOwnerLookups=0; // separate bounded budget
    bool originalV1NonRootTypeDefOwnersEndPassStable=false; // NOT VM epoch
    size_t originalV1NonRootTypeDefContentMatched=0; // captured exact declaration
    bool originalV1NonRootTypeDefContentEndPassStable=false; // NOT content epoch
    size_t originalV1AdmittedTypeDefPairsRecorded=0; // roots + other TypeDefs
    size_t originalV1AdmittedTypeDefPairsRechecked=0; // exact distinct handles
    size_t originalV1AdmittedTypeDefOwnerLookups=0; // separate scoped budget
    size_t originalV1RootTypeDefContentRecorded=0; // distinct captured roots
    size_t originalV1RootTypeDefContentMatched=0; // exact content final read
    bool originalV1RootTypeDefContentEndPassStable=false; // NOT VM epoch
    bool originalV1CompleteField15Parity=false; // never set by this source
    size_t observedClassFactsMatched=0; // admitted first/second-pass matches
    bool observedClassFactsTwoPassStable=false; // no whole VM epoch
    size_t observedClassRootFactsMatched=0; // pointer/tag source observations
    bool observedClassRootFactsThreePassStable=false; // NOT a VM epoch
    size_t observedClassFactsEndPassMatched=0; // third, end-of-collection pass
    bool observedClassFactsEndPassStable=false; // observational, not atomic
    bool usedCrossImageArgument=false;
    bool usedUnobservedAotArgumentImage=false;
    bool usedUnicodeAssemblyFold=false;
    bool usedExistingPrimitiveDefault=false;
    bool usedSimpleInterpreterDefinition=false; // plain V1 TypeDef only
    size_t admittedPrimitiveArguments=0;
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
    receipt.usedExistingPrimitiveDefault=false;
    receipt.usedSimpleInterpreterDefinition=false;
    receipt.admittedPrimitiveArguments=0;
    receipt.usedNestedGenericArgument=false;
    receipt.usedPointerArgument=false;
    receipt.usedSzArrayArgument=false;
    receipt.usedMdArrayArgument=false;
    receipt.usedQualifiedArgument=false;
    receipt.compositeNodesVisited=0;
    receipt.maximumCompositeDepth=0;
    receipt.registeredImageSetStable=false;
    receipt.originalV1ImageSetStable=false;
    receipt.originalV1ImageFactsMatched=0;
    receipt.originalV1ImageFactsTwoPassStable=false;
    receipt.originalV1FirstEmplaceOwnerMatched=false;
    receipt.originalV1RootOwnerEndPassMatched=0;
    receipt.originalV1RootOwnerEndPassLookups=0;
    receipt.originalV1RootOwnerEndPassStable=false;
    receipt.originalV1NonRootTypeDefOwnersRecorded=0;
    receipt.originalV1NonRootTypeDefOwnersRechecked=0;
    receipt.originalV1NonRootTypeDefOwnerLookups=0;
    receipt.originalV1NonRootTypeDefOwnersEndPassStable=false;
    receipt.originalV1NonRootTypeDefContentMatched=0;
    receipt.originalV1NonRootTypeDefContentEndPassStable=false;
    receipt.originalV1AdmittedTypeDefPairsRecorded=0;
    receipt.originalV1AdmittedTypeDefPairsRechecked=0;
    receipt.originalV1AdmittedTypeDefOwnerLookups=0;
    receipt.originalV1RootTypeDefContentRecorded=0;
    receipt.originalV1RootTypeDefContentMatched=0;
    receipt.originalV1RootTypeDefContentEndPassStable=false;
    receipt.originalV1CompleteField15Parity=false;
    receipt.originalV1HandlesMatched=0;
    receipt.observedClassFactsMatched=0;
    receipt.observedClassFactsTwoPassStable=false;
    receipt.observedClassRootFactsMatched=0;
    receipt.observedClassRootFactsThreePassStable=false;
    receipt.observedClassFactsEndPassMatched=0;
    receipt.observedClassFactsEndPassStable=false;
    receipt.observedRegistryAndCounterFenceStable=false;
    receipt.completeArbitraryTypeKeys=false;
    receipt.completeExecutionDiagnostics=false;
    rows.Revoke();
    membership.Scrub();
    record.RejectInvalidSource();
    return false;
}

}}} // il2cpp::vm::assembly_shadow_bounded_definition_execution
