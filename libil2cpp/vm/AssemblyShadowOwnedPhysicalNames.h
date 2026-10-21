#pragma once
// R03 P1 INACTIVE: names copied synchronously from classes emitted by the
// physical enumerator while g_MetadataLock is held. This owns TEXT ONLY.
// No Il2CppClass*, Il2CppImage* or assembly pointer survives a callback.
// A physical name/registry-slot binding is NOT a canonical execution TypeKey, safe pointer lifetime,
// VM-wide coherent epoch or Player/runtime qualification.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "AssemblyShadowExecutionRegistrationWitness.h"
#include "AssemblyShadowObservedTypeShape.h"

namespace il2cpp { namespace vm { namespace assembly_shadow_owned_physical_names {

static constexpr size_t kMaximumRows=1024;
static constexpr size_t kMaximumTextBytes=256u*1024u;
static constexpr size_t kMaximumFieldBytes=4096;
static constexpr size_t kRegistrationSlots=assembly_shadow_execution_witness::kSlots;

enum class Error:uint8_t {
    None, ReusedCapture, BadPhase, MissingName, AliasedSource,
    FieldTooLong, TextCapacity, TooManyRows, CountMismatch,
    AdmissionUnavailable, RegistryInvalid, EnumeratorIncomplete,
    SourceChanged, NameCaptureFailed, InvalidRegistrationBinding,
    DuplicateRegistrationBinding
};
struct TextView { const char* data=nullptr; size_t bytes=0; };
struct RowView {
    TextView physicalAssemblyName{}, nameSpace{}, simpleName{};
    size_t registrySlot=kRegistrationSlots;
    assembly_shadow_execution_witness::Source registrationSource=
        assembly_shadow_execution_witness::Source::None;
    assembly_shadow_observed_type_shape::RawEvidence observedShape{};
};
class Capture final {
    struct Span {uint32_t offset=0,bytes=0;};
    struct Row {
        Span assembly{},nameSpace{},simpleName{};
        uint16_t registrySlot=static_cast<uint16_t>(kRegistrationSlots);
        assembly_shadow_execution_witness::Source registrationSource=
            assembly_shadow_execution_witness::Source::None;
        assembly_shadow_observed_type_shape::RawEvidence observedShape{};
    };
    enum class Stage:uint8_t {New, Collecting, Sealed, Failed};
    std::array<Row,kMaximumRows> rows_{};
    std::array<char,kMaximumTextBytes> text_{};
    std::array<uint8_t,kRegistrationSlots> usedRegistrationSlots_{};
    uint32_t used_=0,count_=0;
    Stage stage_=Stage::New;
    Error firstError_=Error::None;
    bool Fail(Error error) noexcept {
        if(firstError_==Error::None)firstError_=error;
        stage_=Stage::Failed;
        text_.fill(0);
        rows_.fill(Row{});
        usedRegistrationSlots_.fill(0);
        used_=count_=0;
        return false;
    }
    bool Append(const char* source,Span& result) noexcept {
        if(!source)return Fail(Error::MissingName);
        const uintptr_t self=reinterpret_cast<uintptr_t>(this);
        const uintptr_t ptr=reinterpret_cast<uintptr_t>(source);
        if(sizeof(*this)>UINTPTR_MAX-self)return Fail(Error::AliasedSource);
        const uintptr_t end=self+sizeof(*this);
        if(ptr>=self && ptr<end)return Fail(Error::AliasedSource);
        size_t length=0;
        const size_t remaining=kMaximumTextBytes-used_;
        // The callback owns a valid, zero-terminated VM string. This bounded
        // scan does not claim protection against an invalid source pointer.
        for(;length<=kMaximumFieldBytes && length<=remaining;++length)
        {
            if(length>=UINTPTR_MAX-ptr)return Fail(Error::MissingName);
            if(source[length]=='\0')break;
        }
        if(length>kMaximumFieldBytes)return Fail(Error::FieldTooLong);
        if(length>remaining)return Fail(Error::TextCapacity);
        if(length>0 && ptr<end && self<ptr+length)
            return Fail(Error::AliasedSource);
        result.offset=used_;
        result.bytes=static_cast<uint32_t>(length);
        if(length)std::memcpy(text_.data()+used_,source,length);
        used_+=static_cast<uint32_t>(length);
        return true;
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    bool Begin() noexcept {
        if(stage_!=Stage::New)return Fail(Error::ReusedCapture);
        stage_=Stage::Collecting;
        return true;
    }
    bool Add(size_t registrationSlot,
             assembly_shadow_execution_witness::Source registrationSource,
             const char* assembly,const char* nameSpace,const char* simpleName,
             assembly_shadow_observed_type_shape::RawEvidence observedShape={}) noexcept {
        if(stage_!=Stage::Collecting || firstError_!=Error::None)
            return Fail(Error::BadPhase);
        if(count_==kMaximumRows)return Fail(Error::TooManyRows);
        if(registrationSlot>=kRegistrationSlots ||
           (registrationSource!=assembly_shadow_execution_witness::Source::Candidate &&
            registrationSource!=assembly_shadow_execution_witness::Source::ActiveShadow))
            return Fail(Error::InvalidRegistrationBinding);
        if(usedRegistrationSlots_[registrationSlot])return Fail(Error::DuplicateRegistrationBinding);
        if(!assembly||!simpleName)return Fail(Error::MissingName);
        Row& row=rows_[count_];
        if(!Append(assembly,row.assembly) ||
           !Append(nameSpace?nameSpace:"",row.nameSpace) ||
           !Append(simpleName,row.simpleName))return false;
        row.registrySlot=static_cast<uint16_t>(registrationSlot);
        row.registrationSource=registrationSource;
        row.observedShape=observedShape;
        usedRegistrationSlots_[registrationSlot]=1;
        ++count_;
        return true;
    }
    bool Seal(size_t expected) noexcept {
        if(stage_!=Stage::Collecting || firstError_!=Error::None)
            return Fail(Error::BadPhase);
        if(expected!=count_)return Fail(Error::CountMismatch);
        stage_=Stage::Sealed;
        return true;
    }
    bool Complete() const noexcept {return stage_==Stage::Sealed && firstError_==Error::None;}
    bool CollectingHealthy() const noexcept {return stage_==Stage::Collecting && firstError_==Error::None;}
    Error FirstError() const noexcept{return firstError_;}
    size_t Count() const noexcept{return Complete()?count_:0;}
    size_t TextBytes() const noexcept{return Complete()?used_:0;}
    bool Get(size_t index,RowView& out) const noexcept {
        out=RowView{};
        if(!Complete()||index>=count_)return false;
        const Row& row=rows_[index];
        out.physicalAssemblyName={text_.data()+row.assembly.offset,row.assembly.bytes};
        out.nameSpace={text_.data()+row.nameSpace.offset,row.nameSpace.bytes};
        out.simpleName={text_.data()+row.simpleName.offset,row.simpleName.bytes};
        out.registrySlot=row.registrySlot;
        out.registrationSource=row.registrationSource;
        out.observedShape=row.observedShape;
        return true;
    }
    bool AllObservedShapesFromMatchedCallbacks() const noexcept {
        if(!Complete())return false;
        for(size_t i=0;i<count_;++i)
            if(!rows_[i].observedShape.observedFromMatchedPhysicalCallback)return false;
        return true;
    }
    bool RejectInvalidSource() noexcept{return Fail(Error::MissingName);}
    bool RejectInvalidBinding() noexcept{return Fail(Error::InvalidRegistrationBinding);}
    void Revoke() noexcept {
        if(stage_!=Stage::Sealed && firstError_==Error::None)
            firstError_=Error::NameCaptureFailed;
        stage_=Stage::Failed;
        // Do not expose partial names after a failed VM enumeration.
        text_.fill(0);
        rows_.fill(Row{});
        usedRegistrationSlots_.fill(0);
        used_=count_=0;
    }
};
struct Receipt {
    Error error=Error::None;
    size_t observedClasses=0,matchedClasses=0,ownedRows=0,ownedTextBytes=0;
    size_t boundRegistrationRows=0;
    uint64_t registryRevisionBefore=0,registryRevisionAfter=0;
    bool namesCopiedFromMatchedEnumeratorCallbacks=false;
    bool allRowsBoundToObservedRegistry=false;
    bool allObservedTypeShapeRowsFromMatchedCallbacks=false;
    bool classPointerOwnershipProven=false;
    bool metadataPointerLifetimeProven=false;
    bool completeExecutionTypeKeys=false;
    bool coherentVmEpochProven=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};
static_assert(sizeof(Capture)<=512u*1024u,"Owned physical names exceeded reviewed fixed budget");
}}} // namespace il2cpp::vm::assembly_shadow_owned_physical_names
