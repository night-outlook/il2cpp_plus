#pragma once
// R03 P1 INACTIVE. Pure, bounded and independently owned serialization of
// already captured physical display-name rows and SNAPSHOT-LOCAL registration
// witnesses. This is a separate evidence schema, NOT the execution V1 JSON.
// No VM class/metadata pointer, lock, VM call or mutable source view is kept.
// Completion does not prove canonical TypeKeys, GC/RSS, VM epoch or Player use.
#include "AssemblyShadowOwnedPhysicalNames.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_owned_physical_json {

namespace names=assembly_shadow_owned_physical_names;
namespace witness=assembly_shadow_execution_witness;
namespace diagnostic=assembly_shadow_diagnostic;

enum class Error : uint8_t {
    None, ReusedOutput, InvalidOutputLimit, IncompleteSource, InvalidReceipt,
    InvalidBinding, DuplicateBinding, InvalidName, InvalidUtf8,
    OutputCapacity, WriterFailure
};
struct Receipt {
    Error error=Error::None;
    size_t rows=0,candidateRows=0,activeShadowRows=0,serializedBytes=0;
    uint64_t snapshotRegistryRevision=0;
    bool allRowsBoundToObservedRegistry=false;
    bool ownedNonCanonicalJsonComplete=false;
    bool canonicalExecutionTypeKeys=false;
    bool physicalClassPointerOriginProven=false;
    bool coherentVmEpochProven=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};

// Prepared outside VM locks. One owner, no concurrent source/output mutation.
// The output is fully materialized before Data() can reveal any bytes. This
// field is NOT implicitly admitted to the shared legacy V2 frame budget.
class Record final {
public:
    static constexpr size_t kMaximumBytes=2u*1024u*1024u;
    static constexpr size_t kMaximumObjectBytes=kMaximumBytes+128;
private:
    enum class Stage:uint8_t {New,Writing,Complete,Failed};
    std::array<char,kMaximumBytes> bytes_{};
    size_t used_=0;
    Stage stage_=Stage::New;
    Error error_=Error::None;
    static bool Collect(void* ctx,const char* data,size_t size) noexcept {
        auto& self=*static_cast<Record*>(ctx);
        if((!data&&size) || size>kMaximumBytes-self.used_)return false;
        if(size)std::memcpy(self.bytes_.data()+self.used_,data,size);
        self.used_+=size;
        return true;
    }
public:
    Record() noexcept=default;
    Record(const Record&)=delete;
    Record& operator=(const Record&)=delete;
    Record(Record&&)=delete;
    Record& operator=(Record&&)=delete;
    Error FirstError() const noexcept{return error_;}
    // Only a fresh caller-allocated output may begin a live source pass.
    // Refuse completed, writing and failed objects before entering VM capture.
    bool Fresh() const noexcept{return stage_==Stage::New && error_==Error::None;}
    bool Complete() const noexcept{return stage_==Stage::Complete && error_==Error::None;}
    const char* Data() const noexcept{return Complete()?bytes_.data():nullptr;}
    size_t Bytes() const noexcept{return Complete()?used_:0;}
    void Reject(Error why) noexcept {
        if(error_==Error::None)error_=why;
        stage_=Stage::Failed;
        // Discard even a formerly completed record on an attempted reuse.
        bytes_.fill(0);used_=0;
    }
    bool Begin() noexcept {
        if(stage_!=Stage::New){Reject(Error::ReusedOutput);return false;}
        stage_=Stage::Writing;
        return true;
    }
    diagnostic::Sink Sink() noexcept{return {this,Collect};}
    bool Seal(diagnostic::WriteResult result) noexcept {
        if(stage_!=Stage::Writing || !result.complete ||
           result.bytesAccepted!=used_){
            Reject(result.error==diagnostic::WriteError::ByteLimit ?
                Error::OutputCapacity : Error::WriterFailure);
            return false;
        }
        stage_=Stage::Complete;
        return true;
    }
};
static_assert(sizeof(Record)<=Record::kMaximumObjectBytes,
              "Owned physical display-name JSON exceeds reviewed fixed storage");

inline Error CheckUtf8(names::TextView value,bool requireNonempty) noexcept {
    if(!value.data || value.bytes>names::kMaximumFieldBytes ||
       (requireNonempty && !value.bytes))return Error::InvalidName;
    const auto* p=reinterpret_cast<const unsigned char*>(value.data);
    for(size_t i=0;i<value.bytes;){
        uint32_t cp=p[i++];unsigned extra=0;uint32_t minimum=0;
        if(!cp)return Error::InvalidName;
        if(cp<128)continue;
        if(cp>=0xc2 && cp<=0xdf){extra=1;cp&=31;minimum=128;}
        else if(cp>=0xe0 && cp<=0xef){extra=2;cp&=15;minimum=2048;}
        else if(cp>=0xf0 && cp<=0xf4){extra=3;cp&=7;minimum=65536;}
        else return Error::InvalidUtf8;
        if(extra>value.bytes-i)return Error::InvalidUtf8;
        while(extra--){
            const uint32_t c=p[i++];
            if((c&0xc0)!=0x80)return Error::InvalidUtf8;
            cp=(cp<<6)|(c&63);
        }
        if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))
            return Error::InvalidUtf8;
    }
    return Error::None;
}
inline bool Fail(Record& output,Receipt& receipt,Error reason) noexcept {
    if(receipt.error==Error::None)receipt.error=reason;
    receipt.rows=receipt.candidateRows=receipt.activeShadowRows=receipt.serializedBytes=0;
    receipt.allRowsBoundToObservedRegistry=false;
    receipt.ownedNonCanonicalJsonComplete=false;
    output.Reject(reason);
    return false;
}

// Does not touch metadata, class pointers, the physical iterator or any VM
// lock. The original names Capture and its receipt must already be complete
// and exclusively owned for this entire synchronous call. No pointer from a
// row is kept by output; the returned record survives input Revoke().
inline bool Compose(const names::Capture& source,const names::Receipt& sourceReceipt,
                    Record& output,Receipt& receipt,
                    size_t maximumBytes=Record::kMaximumBytes) noexcept {
    receipt=Receipt{};
    if(!output.Begin())return Fail(output,receipt,Error::ReusedOutput);
    if(maximumBytes>Record::kMaximumBytes)
        return Fail(output,receipt,Error::InvalidOutputLimit);
    if(!source.Complete())
        return Fail(output,receipt,Error::IncompleteSource);
    const size_t count=source.Count();
    if(sourceReceipt.error!=names::Error::None ||
       !sourceReceipt.namesCopiedFromMatchedEnumeratorCallbacks ||
       !sourceReceipt.allRowsBoundToObservedRegistry ||
       sourceReceipt.observedClasses!=count ||
       sourceReceipt.matchedClasses!=count ||
       sourceReceipt.ownedRows!=count ||
       sourceReceipt.boundRegistrationRows!=count ||
       sourceReceipt.ownedTextBytes!=source.TextBytes() ||
       sourceReceipt.registryRevisionBefore!=sourceReceipt.registryRevisionAfter ||
       sourceReceipt.registryRevisionBefore==std::numeric_limits<uint64_t>::max() ||
       sourceReceipt.classPointerOwnershipProven ||
       sourceReceipt.metadataPointerLifetimeProven ||
       sourceReceipt.completeExecutionTypeKeys ||
       sourceReceipt.coherentVmEpochProven ||
       sourceReceipt.playerExecution || sourceReceipt.runtimeAcceptance)
        return Fail(output,receipt,Error::InvalidReceipt);

    // Prevalidate every input row and the independent slot/role census before
    // writing anything. The capture internally bounds its own text length.
    std::array<uint8_t,names::kRegistrationSlots> seen{};
    size_t candidate=0,active=0,textBytes=0;
    for(size_t i=0;i<count;++i){
        names::RowView row{};
        if(!source.Get(i,row))return Fail(output,receipt,Error::IncompleteSource);
        if(row.registrySlot>=seen.size() ||
           (row.registrationSource!=witness::Source::Candidate &&
            row.registrationSource!=witness::Source::ActiveShadow))
            return Fail(output,receipt,Error::InvalidBinding);
        if(seen[row.registrySlot])return Fail(output,receipt,Error::DuplicateBinding);
        seen[row.registrySlot]=1;
        const Error assembly=CheckUtf8(row.physicalAssemblyName,true);
        const Error ns=CheckUtf8(row.nameSpace,false);
        const Error type=CheckUtf8(row.simpleName,true);
        if(assembly!=Error::None)return Fail(output,receipt,assembly);
        if(ns!=Error::None)return Fail(output,receipt,ns);
        if(type!=Error::None)return Fail(output,receipt,type);
        const size_t bytes=row.physicalAssemblyName.bytes+
                           row.nameSpace.bytes+row.simpleName.bytes;
        if(bytes>names::kMaximumTextBytes-textBytes)
            return Fail(output,receipt,Error::InvalidReceipt);
        textBytes+=bytes;
        if(row.registrationSource==witness::Source::Candidate)++candidate;
        else ++active;
    }
    if(candidate+active!=count || textBytes!=source.TextBytes())
        return Fail(output,receipt,Error::InvalidReceipt);

    diagnostic::Writer writer(output.Sink(),maximumBytes);
    writer<<"{\"schemaVersion\":1,"
          "\"kind\":\"R03P1ObservedPhysicalDisplayNames\","
          "\"scope\":\"OwnedSnapshotLocalDisplayNamesOnly\","
          "\"registryRevision\":"<<sourceReceipt.registryRevisionBefore
          <<",\"observedRows\":"<<count
          <<",\"candidateRows\":"<<candidate
          <<",\"activeShadowRows\":"<<active
          <<",\"canonicalExecutionTypeKeys\":false,"
          "\"physicalClassPointerOriginProven\":false,"
          "\"coherentVmEpochProven\":false,"
          "\"runtimeAcceptance\":false,\"rows\":[";
    for(size_t i=0;i<count;++i){
        names::RowView row{};
        if(!source.Get(i,row))return Fail(output,receipt,Error::IncompleteSource);
        if(i)writer<<',';
        writer<<"{\"registrySlot\":"<<row.registrySlot
              <<",\"registrationSource\":";
        writer<<diagnostic::Quoted(row.registrationSource==witness::Source::Candidate ?
                                   "Candidate":"ActiveShadow");
        writer<<",\"physicalAssemblyName\":"
              <<diagnostic::QuotedText{row.physicalAssemblyName.data,row.physicalAssemblyName.bytes}
              <<",\"namespace\":"
              <<diagnostic::QuotedText{row.nameSpace.data,row.nameSpace.bytes}
              <<",\"simpleName\":"
              <<diagnostic::QuotedText{row.simpleName.data,row.simpleName.bytes}
              <<'}';
    }
    writer<<"]}";
    const auto finished=writer.Finish();
    if(!output.Seal(finished))
        return Fail(output,receipt,finished.error==diagnostic::WriteError::ByteLimit ?
                    Error::OutputCapacity : Error::WriterFailure);
    receipt.rows=count;
    receipt.candidateRows=candidate;
    receipt.activeShadowRows=active;
    receipt.serializedBytes=output.Bytes();
    receipt.snapshotRegistryRevision=sourceReceipt.registryRevisionBefore;
    receipt.allRowsBoundToObservedRegistry=true;
    receipt.ownedNonCanonicalJsonComplete=true;
    return true;
}
}}} // namespaces
