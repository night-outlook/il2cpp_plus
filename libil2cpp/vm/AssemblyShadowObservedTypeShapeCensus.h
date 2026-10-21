#pragma once
// R03 P1 INACTIVE, pure immutable source-to-primitive-shape census.
// An observed first-match class header supplies only primitive shape flags.
// The two "Plain*Candidate" groups are NOT canonical execution TypeKeys,
// proof of IL2CPP metadata allocation provenance, safe lifetime, VM epoch,
// generic compatibility, layout admission, or Player runtime acceptance.
#include "AssemblyShadowOwnedPhysicalNames.h"
#include "AssemblyShadowObservedTypeShape.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_observed_type_shape_census {
namespace owned=assembly_shadow_owned_physical_names;
namespace shape=assembly_shadow_observed_type_shape;
namespace witness=assembly_shadow_execution_witness;

enum class Error:uint8_t {
    None, IncompleteCapture, InvalidSourceReceipt, MissingShapeProof,
    DuplicateRegistrationSlot, InvalidRegistrationSource,
    InvalidShapeCensus, ReusedCensus
};
struct Result {
    Error error=Error::None;
    shape::Census census{};
    size_t candidateRegistrationRows=0,activeShadowRegistrationRows=0;
    uint64_t registryRevision=0;
    bool sourceOwnedAndMatched=false;
    bool firstMatchedClassHeaderBitsCopied=false;
    bool canonicalExecutionTypeKeys=false;
    bool physicalClassPointerOwnershipProven=false;
    bool coherentVmEpochProven=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};
inline bool Fail(Result& output,Error why) noexcept {
    if(output.error==Error::None)output.error=why;
    // No partial positive census, stale result from a previous successful
    // call, or successful marker remains after a failure.
    output.census=shape::Census{};
    output.candidateRegistrationRows=output.activeShadowRegistrationRows=0;
    output.registryRevision=0;
    output.sourceOwnedAndMatched=false;
    output.firstMatchedClassHeaderBitsCopied=false;
    return false;
}

// A caller supplies an already complete copied source and exact typed receipt.
// No borrowed VM pointer or field is accessed or retained. Each successful
// call requires a fresh destination; caller owns both objects exclusively.
inline bool Fresh(const Result& result) noexcept {
    return result.error==Error::None &&
           !result.sourceOwnedAndMatched &&
           !result.firstMatchedClassHeaderBitsCopied &&
           !result.census.completeObservedSubset &&
           result.census.total==0 &&
           result.candidateRegistrationRows==0 &&
           result.activeShadowRegistrationRows==0 &&
           result.registryRevision==0;
}
inline bool Build(const owned::Capture& capture,
                  const owned::Receipt& sourceReceipt,
                  Result& output) noexcept {
    if(!Fresh(output))
        return Fail(output,Error::ReusedCensus);
    if(!capture.Complete())return Fail(output,Error::IncompleteCapture);
    const size_t rows=capture.Count();
    if(sourceReceipt.error!=owned::Error::None ||
       !sourceReceipt.namesCopiedFromMatchedEnumeratorCallbacks ||
       !sourceReceipt.allRowsBoundToObservedRegistry ||
       !sourceReceipt.allObservedTypeShapeRowsFromMatchedCallbacks ||
       sourceReceipt.observedClasses!=rows ||
       sourceReceipt.matchedClasses!=rows ||
       sourceReceipt.ownedRows!=rows ||
       sourceReceipt.boundRegistrationRows!=rows ||
       sourceReceipt.ownedTextBytes!=capture.TextBytes() ||
       sourceReceipt.registryRevisionBefore!=sourceReceipt.registryRevisionAfter ||
       sourceReceipt.registryRevisionBefore==std::numeric_limits<uint64_t>::max() ||
       sourceReceipt.classPointerOwnershipProven ||
       sourceReceipt.metadataPointerLifetimeProven ||
       sourceReceipt.completeExecutionTypeKeys ||
       sourceReceipt.coherentVmEpochProven ||
       sourceReceipt.playerExecution || sourceReceipt.runtimeAcceptance)
        return Fail(output,Error::InvalidSourceReceipt);

    std::array<uint8_t,owned::kRegistrationSlots> seen{};
    shape::Census computed{};
    size_t candidate=0,active=0;
    for(size_t i=0;i<rows;++i) {
        owned::RowView row{};
        if(!capture.Get(i,row))return Fail(output,Error::IncompleteCapture);
        if(row.registrySlot>=seen.size() ||
           (row.registrationSource!=witness::Source::Candidate &&
            row.registrationSource!=witness::Source::ActiveShadow))
            return Fail(output,Error::InvalidRegistrationSource);
        if(seen[row.registrySlot])return Fail(output,Error::DuplicateRegistrationSlot);
        seen[row.registrySlot]=1;
        if(!row.observedShape.observedFromMatchedPhysicalCallback)
            return Fail(output,Error::MissingShapeProof);
        const shape::Category category=shape::Classify(row.observedShape);
        if(category==shape::Category::MissingMatchedCallbackProof ||
           shape::Index(category)>=shape::kCategoryCount)
            return Fail(output,Error::InvalidShapeCensus);
        ++computed.rowsByCategory[shape::Index(category)];
        ++computed.total;
        if(row.registrationSource==witness::Source::Candidate)++candidate;
        else ++active;
    }
    if(computed.total!=rows || candidate+active!=rows)
        return Fail(output,Error::InvalidShapeCensus);
    computed.completeObservedSubset=true;  // ONLY the matched registry subset.
    output.census=computed;
    output.candidateRegistrationRows=candidate;
    output.activeShadowRegistrationRows=active;
    output.registryRevision=sourceReceipt.registryRevisionBefore;
    output.sourceOwnedAndMatched=true;
    output.firstMatchedClassHeaderBitsCopied=true;
    return true;
}
}}} // il2cpp::vm::assembly_shadow_observed_type_shape_census
