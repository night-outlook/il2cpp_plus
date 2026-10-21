#pragma once
// R03 P1 INACTIVE. Fixed-size, pointer-free observations of physical class
// SHAPE only, copied during the already authorized first-match VM iterator.
// This is deliberately NOT a canonical execution TypeKey, layout admission,
// metadata allocation provenance, coherent VM epoch or Player certificate.
// Do not retain Il2CppClass*, type-definition handles or generic pointers.
#include <array>
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_observed_type_shape {

constexpr uint8_t kClassTypeCode=0x12;
constexpr uint8_t kValueTypeCode=0x11;
enum class Category:uint8_t {
    PlainReferenceDefinitionCandidate=0,
    PlainValueDefinitionCandidate=1,
    MissingMetadataDefinition=2,
    RankedOrArray=3,
    ConstructedGeneric=4,
    GenericDefinition=5,
    NestedDefinition=6,
    UnsupportedTypeCode=7,
    MissingMatchedCallbackProof=8,
    Count=9
};
constexpr size_t kCategoryCount=static_cast<size_t>(Category::Count);

// All fields are primitive COPIES of Il2CppClass metadata; no pointers, no
// borrowed text, no stable identity and no dereference after the callback.
// The low two TypeCode values are checked against actual IL2CPP ABI constants
// in the VM source method and separately by the official SDK syntax probe.
struct RawEvidence {
    uint8_t byvalTypeCode=0;
    uint8_t arrayRank=0;
    bool hasTypeMetadataHandle=false;
    bool hasDeclaringType=false;
    bool hasConstructedGenericClass=false;
    bool isGenericDefinition=false;
    bool observedFromMatchedPhysicalCallback=false;
};
static_assert(sizeof(RawEvidence)<=8,"Observed primitive shape grew unexpectedly");

inline Category Classify(const RawEvidence& row) noexcept {
    if(!row.observedFromMatchedPhysicalCallback)
        return Category::MissingMatchedCallbackProof;
    if(!row.hasTypeMetadataHandle)
        return Category::MissingMetadataDefinition;
    // Source-only conservative exclusion precedence. Multiple disqualifying
    // features remain non-candidates; no status authorizes construction.
    if(row.arrayRank!=0)return Category::RankedOrArray;
    if(row.hasConstructedGenericClass)return Category::ConstructedGeneric;
    if(row.isGenericDefinition)return Category::GenericDefinition;
    if(row.hasDeclaringType)return Category::NestedDefinition;
    if(row.byvalTypeCode==kClassTypeCode)
        return Category::PlainReferenceDefinitionCandidate;
    if(row.byvalTypeCode==kValueTypeCode)
        return Category::PlainValueDefinitionCandidate;
    return Category::UnsupportedTypeCode;
}
constexpr size_t Index(Category category) noexcept {
    return static_cast<size_t>(category);
}
struct Census {
    std::array<size_t,kCategoryCount> rowsByCategory{};
    size_t total=0;
    bool completeObservedSubset=false;
    // These must remain false even when every observed row is a "candidate".
    bool canonicalExecutionTypeKeys=false;
    bool physicalClassPointerOwnershipProven=false;
    bool coherentVmEpochProven=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};
}}} // il2cpp::vm::assembly_shadow_observed_type_shape
