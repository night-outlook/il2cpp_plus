#pragma once
// P1 INACTIVE: bounded, owned first-use snapshot of the REAL usage registry.
// Phase 1 copies raw records while UsageLock is held. Phase 2 copies immutable
// CandidateRegistry names and composes the exact legacy FirstUseSequence text
// WITHOUT the spin lock. No Il2CppClass is ever dereferenced here.
//
// A non-null class handle is captured as OPAQUE IDENTITY ONLY. Unless a
// separately source/lifetime-reviewed metadata resolver supplies the exact
// physical namespace/name, Seal MUST FAIL CLOSED. The live source adapter does
// not invoke such a resolver, so class-bearing records are NOT falsely
// presented as complete diagnostic rows. This does NOT prove an all-VM epoch,
// a safe terminal scheduler owner, or real Player/semantic acceptance.
//
// All mutable storage must be preallocated while healthy, and Capture must be
// owned by a single thread. No partial record/row can be accepted as complete.
#include "AssemblyShadowTypes.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_first_use {

using assembly_shadow_diagnostic::QuotedText;

enum class Error : uint8_t {
    None, WrongPhase, TooManyCandidates, InvalidDetail, InvalidInput,
    TextCapacity, AliasedInput, IndexMismatch, UnverifiedClassMetadata,
    ClassTokenMismatch, SourceChanged
};

struct Raw
{
    bool present=false;
    BaselineUseKind kind=BaselineUseKind::AssemblyReflection;
    char detail[160]{};
    // Never dereference a class pointer under UsageLock, or after it, in
    // this component. A future resolver needs separate metadata-lifetime proof.
    const void* opaqueClass=nullptr;
    uint64_t thread=0,timestamp=0,sequence=0;
};

struct Row
{
    QuotedText name{}, detail{}, type{};
    BaselineUseKind kind=BaselineUseKind::AssemblyReflection;
    uint64_t thread=0,timestamp=0,sequence=0;
};
class Capture final
{
public:
    static constexpr size_t kMaximumCandidates=4096;
    static constexpr size_t kTextCapacity=1024u*1024u;
    static constexpr size_t kDetailBytes=160;
    static constexpr size_t kMaximumObjectBytes=kTextCapacity+1000000u;
private:
    struct Span { uint32_t offset=0,bytes=0; };
    struct Slot
    {
        Raw raw{};
        Span name{},detail{},type{};
        bool sourceNameCopied=false, typeResolved=false;
    };
    enum class Phase : uint8_t { New, Raw, Names, Resolve, Complete, Failed };
    char text_[kTextCapacity]{};
    Slot slots_[kMaximumCandidates]{};
    uint32_t used_=0,expected_=0,rawCount_=0,nameCount_=0,presentCount_=0;
    uint64_t capturedGeneration_=0;
    Phase phase_=Phase::New;
    Error failure_=Error::None;

    bool Fail(Error error) noexcept
    {
        if(failure_==Error::None)failure_=error;
        phase_=Phase::Failed;
        return false;
    }
    bool Require(Phase phase) noexcept
    {
        if(phase_!=phase || failure_!=Error::None)return Fail(Error::WrongPhase);
        return true;
    }
    bool Valid(QuotedText source) noexcept
    {
        if(!source.data || source.bytes>UINTPTR_MAX-reinterpret_cast<uintptr_t>(source.data))
            return Fail(Error::InvalidInput);
        const auto p=reinterpret_cast<uintptr_t>(source.data);
        const auto start=reinterpret_cast<uintptr_t>(this);
        if(sizeof(*this)>UINTPTR_MAX-start)return Fail(Error::InvalidInput);
        if(source.bytes && p<start+sizeof(*this) && start<p+source.bytes)
            return Fail(Error::AliasedInput);
        return true;
    }
    bool Copy(QuotedText source,Span& output) noexcept
    {
        if(!Valid(source))return false;
        if(source.bytes>kTextCapacity-used_)return Fail(Error::TextCapacity);
        output.offset=used_;
        output.bytes=static_cast<uint32_t>(source.bytes);
        if(source.bytes)std::memcpy(text_+used_,source.data,source.bytes);
        used_+=static_cast<uint32_t>(source.bytes);
        return true;
    }
    bool AppendText(const char* source,size_t size) noexcept
    {
        if(!source || size>kTextCapacity-used_)return Fail(Error::TextCapacity);
        if(size)std::memcpy(text_+used_,source,size);
        used_+=static_cast<uint32_t>(size);
        return true;
    }
    bool BuildDetail(const Raw& source,Span& result) noexcept
    {
        // Original UseRecord::detail is a bounded zero-terminated char[160].
        // Reject missing terminators rather than read arbitrary memory.
        const void* end=std::memchr(source.detail,0,kDetailBytes);
        if(!end)return Fail(Error::InvalidDetail);
        const size_t length=static_cast<const char*>(end)-source.detail;
        result.offset=used_;
        if(!AppendText(source.detail,length))return false;
        static constexpr char kSequence[]=" FirstUseSequence=";
        if(!AppendText(kSequence,sizeof(kSequence)-1))return false;
        // Equivalent to std::to_string(uint64_t), no heap or locale.
        char numbers[20];
        size_t start=sizeof(numbers);
        uint64_t seq=source.sequence;
        do { numbers[--start]=static_cast<char>('0'+(seq%10));seq/=10; }
        while(seq);
        if(!AppendText(numbers+start,sizeof(numbers)-start))return false;
        result.bytes=used_-result.offset;
        return true;
    }
    QuotedText View(Span span) const noexcept
    {return {text_+span.offset,span.bytes};}
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    Error FirstError() const noexcept {return failure_;}
    bool RejectMalformedRegistry() noexcept {return Fail(Error::InvalidInput);}
    bool RejectChangedSource() noexcept {return Fail(Error::SourceChanged);}
    bool Complete() const noexcept {return phase_==Phase::Complete && failure_==Error::None;}
    size_t SourceCandidateCount() const noexcept{return expected_;}
    size_t PresentCount() const noexcept{return presentCount_;}
    uint64_t CapturedUsageGeneration() const noexcept{return capturedGeneration_;}
    size_t CapturedTextBytes() const noexcept{return used_;}

    bool Begin(size_t candidates) noexcept
    {
        if(!Require(Phase::New))return false;
        if(candidates>kMaximumCandidates)return Fail(Error::TooManyCandidates);
        expected_=static_cast<uint32_t>(candidates);
        phase_=Phase::Raw;
        return true;
    }
    // Exactly one ordered Raw per candidate; call inside a single UsageLock.
    // It copies only raw fields, including the opaque class identity bit/token.
    // No Candidate.name, Il2CppClass, metadata, or type-key is dereferenced.
    bool AddRaw(const Raw& value) noexcept
    {
        if(!Require(Phase::Raw))return false;
        if(rawCount_==expected_)return Fail(Error::IndexMismatch);
        if(value.present && !std::memchr(value.detail,0,kDetailBytes))
            return Fail(Error::InvalidDetail);
        slots_[rawCount_++].raw=value;
        if(value.present)++presentCount_;
        return true;
    }
    bool EndRaw(uint64_t sourceGeneration) noexcept
    {
        if(!Require(Phase::Raw) || rawCount_!=expected_)
            return Fail(Error::IndexMismatch);
        capturedGeneration_=sourceGeneration;
        phase_=Phase::Names;
        return true;
    }
    // Candidate names are immutable after registry publication; copy them
    // after releasing UsageLock to keep its hold time bounded. No class access.
    bool AddCandidateName(size_t index,QuotedText name) noexcept
    {
        if(!Require(Phase::Names) || index!=nameCount_ || nameCount_==expected_)
            return Fail(Error::IndexMismatch);
        Slot& slot=slots_[nameCount_];
        if(slot.raw.present)
        {
            if(!Copy(name,slot.name) || !BuildDetail(slot.raw,slot.detail))return false;
            slot.typeResolved=slot.raw.opaqueClass==nullptr;
        }
        slot.sourceNameCopied=true;
        ++nameCount_;
        return true;
    }
    bool EndNames() noexcept
    {
        if(!Require(Phase::Names) || nameCount_!=expected_)
            return Fail(Error::IndexMismatch);
        phase_=Phase::Resolve;
        return true;
    }
    // Host/tests can model a separately REVIEWED metadata lifetime ticket.
    // This API does not authenticate the supplied name or confer ownership.
    // The live source method NEVER calls it without metadata-lifetime proof.
    bool ResolvePhysicalClass(size_t index,const void* exactOpaqueIdentity,
        QuotedText nameSpace,QuotedText name) noexcept
    {
        if(!Require(Phase::Resolve) || index>=expected_)
            return Fail(Error::IndexMismatch);
        Slot& slot=slots_[index];
        if(!slot.raw.present || !slot.raw.opaqueClass || slot.typeResolved ||
            slot.raw.opaqueClass!=exactOpaqueIdentity)
            return Fail(Error::ClassTokenMismatch);
        if(!Valid(nameSpace)||!Valid(name))return false;
        Span view;view.offset=used_;
        if(!AppendText(nameSpace.data,nameSpace.bytes) ||
           !AppendText(".",1) ||
           !AppendText(name.data,name.bytes))return false;
        view.bytes=used_-view.offset;
        slot.type=view;
        slot.typeResolved=true;
        return true;
    }
    // The production source re-enters UsageLock ONLY to sample the ordinary
    // protected generation again. If different, no record can be released.
    bool Seal(uint64_t verifiedGeneration,bool sameRegistry) noexcept
    {
        if(!Require(Phase::Resolve))return false;
        if(!sameRegistry || verifiedGeneration!=capturedGeneration_)
            return Fail(Error::SourceChanged);
        for(size_t i=0;i<expected_;++i)
            if(slots_[i].raw.present && !slots_[i].typeResolved)
                return Fail(Error::UnverifiedClassMetadata);
        phase_=Phase::Complete;
        return true;
    }
    // Candidate-index order is preserved, including absent candidate slots.
    // A missing first use is not exposed as an invented JSON row.
    bool Get(size_t index,Row& output) const noexcept
    {
        if(!Complete() || index>=expected_ || !slots_[index].raw.present)
            return false;
        const auto& slot=slots_[index];
        output.name=View(slot.name);
        output.detail=View(slot.detail);
        output.type=View(slot.type);
        output.kind=slot.raw.kind;
        output.thread=slot.raw.thread;
        output.timestamp=slot.raw.timestamp;
        output.sequence=slot.raw.sequence;
        return true;
    }
    // Opaque identity for FUTURE separately reviewed metadata resolver only.
    // Never dereference the return value without a VM lifetime ticket.
    bool PendingOpaqueClass(size_t index,const void*& token) const noexcept
    {
        if(phase_!=Phase::Resolve || index>=expected_ ||
           !slots_[index].raw.present || !slots_[index].raw.opaqueClass ||
           slots_[index].typeResolved)return false;
        token=slots_[index].raw.opaqueClass;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "P1 first-use snapshot capacity/object budget exceeded");
}}} // il2cpp::vm::assembly_shadow_bounded_first_use
