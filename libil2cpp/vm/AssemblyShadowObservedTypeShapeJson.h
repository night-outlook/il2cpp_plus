#pragma once
// R03 P1 INACTIVE: self-contained bounded JSON of an already completed,
// pointer-FREE physical observed-registry type SHAPE census.
// DISTINCT from execution V1, the owned physical display-name JSON and V2.
// Candidate categories are source hints, NOT canonical generic-aware TypeKeys
// or proof of class allocation provenance, VM-wide coherence or Player use.
#include "AssemblyShadowObservedTypeShapeCensus.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_observed_type_shape_json {
namespace shapes=assembly_shadow_observed_type_shape_census;
namespace category=assembly_shadow_observed_type_shape;
namespace diagnostic=assembly_shadow_diagnostic;
namespace owned=assembly_shadow_owned_physical_names;

enum class Error:uint8_t {
    None, ReusedOutput, InvalidOutputLimit, IncompleteCensus,
    InvalidScope, InvalidRevision, InvalidCounts, InvalidCategory,
    OutputCapacity, WriterFailure
};
struct Receipt {
    Error error=Error::None;
    size_t observedRows=0,candidateRows=0,activeShadowRows=0,bytes=0;
    uint64_t snapshotRegistryRevision=0;
    bool ownedShapeJsonComplete=false;
    bool noncanonicalObservedSubsetOnly=false;
    bool canonicalExecutionTypeKeys=false;
    bool physicalClassPointerOriginProven=false;
    bool coherentVmEpochProven=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};
class Record final {
public:
    static constexpr size_t kMaximumBytes=4096;
    static constexpr size_t kMaximumObjectBytes=kMaximumBytes+128;
private:
    enum class Stage:uint8_t {New,Writing,Complete,Failed};
    std::array<char,kMaximumBytes> bytes_{};
    size_t used_=0;
    Stage stage_=Stage::New;
    Error firstError_=Error::None;
    static bool Collect(void* context,const char* data,size_t size) noexcept {
        auto& output=*static_cast<Record*>(context);
        if((!data && size) || size>kMaximumBytes-output.used_)return false;
        if(size)std::memcpy(output.bytes_.data()+output.used_,data,size);
        output.used_+=size;
        return true;
    }
public:
    Record() noexcept=default;
    Record(const Record&)=delete;
    Record& operator=(const Record&)=delete;
    Record(Record&&)=delete;
    Record& operator=(Record&&)=delete;
    bool Fresh() const noexcept{return stage_==Stage::New && firstError_==Error::None;}
    bool Complete() const noexcept{return stage_==Stage::Complete && firstError_==Error::None;}
    Error FirstError() const noexcept{return firstError_;}
    const char* Data() const noexcept{return Complete()?bytes_.data():nullptr;}
    size_t Bytes() const noexcept{return Complete()?used_:0;}
    void Reject(Error error) noexcept {
        if(firstError_==Error::None)firstError_=error;
        stage_=Stage::Failed;
        bytes_.fill(0);
        used_=0;
    }
    bool Begin() noexcept {
        if(!Fresh()){Reject(Error::ReusedOutput);return false;}
        stage_=Stage::Writing;
        return true;
    }
    diagnostic::Sink Sink() noexcept{return {this,Collect};}
    bool Seal(diagnostic::WriteResult value) noexcept {
        if(stage_!=Stage::Writing || !value.complete ||
           value.bytesAccepted!=used_){
            Reject(value.error==diagnostic::WriteError::ByteLimit ?
                   Error::OutputCapacity : Error::WriterFailure);
            return false;
        }
        stage_=Stage::Complete;
        return true;
    }
};
static_assert(sizeof(Record)<=Record::kMaximumObjectBytes,
              "Noncanonical shape JSON exceeded reviewed fixed buffer");

inline bool Fail(Record& output,Receipt& receipt,Error reason) noexcept {
    if(receipt.error==Error::None)receipt.error=reason;
    receipt.observedRows=receipt.candidateRows=receipt.activeShadowRows=receipt.bytes=0;
    receipt.snapshotRegistryRevision=0;
    receipt.ownedShapeJsonComplete=false;
    receipt.noncanonicalObservedSubsetOnly=false;
    output.Reject(reason); // prior successful bytes become unreadable and zeroed
    return false;
}
inline bool Compose(const shapes::Result& source,
                    Record& output,Receipt& receipt,
                    size_t maximumBytes=Record::kMaximumBytes) noexcept {
    receipt=Receipt{};
    if(!output.Begin())return Fail(output,receipt,Error::ReusedOutput);
    if(maximumBytes>Record::kMaximumBytes)
        return Fail(output,receipt,Error::InvalidOutputLimit);
    if(source.error!=shapes::Error::None ||
       !source.census.completeObservedSubset ||
       !source.sourceOwnedAndMatched ||
       !source.firstMatchedClassHeaderBitsCopied)
        return Fail(output,receipt,Error::IncompleteCensus);
    if(source.census.canonicalExecutionTypeKeys ||
       source.census.physicalClassPointerOwnershipProven ||
       source.census.coherentVmEpochProven ||
       source.census.playerExecution || source.census.runtimeAcceptance ||
       source.canonicalExecutionTypeKeys ||
       source.physicalClassPointerOwnershipProven ||
       source.coherentVmEpochProven ||
       source.playerExecution || source.runtimeAcceptance)
        return Fail(output,receipt,Error::InvalidScope);
    if(source.registryRevision==std::numeric_limits<uint64_t>::max())
        return Fail(output,receipt,Error::InvalidRevision);
    const size_t count=source.census.total;
    if(count>owned::kMaximumRows || source.candidateRegistrationRows>count ||
       source.activeShadowRegistrationRows!=count-source.candidateRegistrationRows)
        return Fail(output,receipt,Error::InvalidCounts);
    size_t categories=0;
    for(size_t i=0;i<category::kCategoryCount;++i){
        const size_t n=source.census.rowsByCategory[i];
        if(n>count-categories)return Fail(output,receipt,Error::InvalidCounts);
        categories+=n;
    }
    if(categories!=count ||
       source.census.rowsByCategory[category::Index(category::Category::MissingMatchedCallbackProof)]!=0)
        return Fail(output,receipt,Error::InvalidCategory);

    static constexpr const char* kNames[category::kCategoryCount]={
        "plainReferenceDefinitionCandidates",
        "plainValueDefinitionCandidates",
        "missingMetadataDefinition",
        "rankedOrArray",
        "constructedGeneric",
        "genericDefinition",
        "nestedDefinition",
        "unsupportedTypeCode",
        "missingMatchedCallbackProof"
    };
    diagnostic::Writer writer(output.Sink(),maximumBytes);
    writer<<"{\"schemaVersion\":1,"
          "\"kind\":\"R03P1ObservedTypeShapeCounts\","
          "\"scope\":\"ObservedRegistrySubsetShapeCountsOnly\","
          "\"registryRevision\":"<<source.registryRevision
          <<",\"observedRows\":"<<count
          <<",\"candidateRows\":"<<source.candidateRegistrationRows
          <<",\"activeShadowRows\":"<<source.activeShadowRegistrationRows
          <<",\"canonicalExecutionTypeKeys\":false,"
          "\"physicalClassPointerOriginProven\":false,"
          "\"coherentVmEpochProven\":false,"
          "\"playerExecution\":false,"
          "\"runtimeAcceptance\":false,"
          "\"categories\":{";
    for(size_t i=0;i<category::kCategoryCount;++i){
        if(i)writer<<',';
        writer<<diagnostic::Quoted(kNames[i])<<':'
              <<source.census.rowsByCategory[i];
    }
    writer<<"}}";
    const auto done=writer.Finish();
    if(!output.Seal(done))
        return Fail(output,receipt,
                    done.error==diagnostic::WriteError::ByteLimit ?
                    Error::OutputCapacity : Error::WriterFailure);
    receipt.observedRows=count;
    receipt.candidateRows=source.candidateRegistrationRows;
    receipt.activeShadowRows=source.activeShadowRegistrationRows;
    receipt.snapshotRegistryRevision=source.registryRevision;
    receipt.bytes=output.Bytes();
    receipt.ownedShapeJsonComplete=true;
    receipt.noncanonicalObservedSubsetOnly=true;
    return true;
}
}}} // namespace il2cpp::vm::assembly_shadow_observed_type_shape_json
