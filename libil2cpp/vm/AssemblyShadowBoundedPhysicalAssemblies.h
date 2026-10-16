#pragma once
// P1 INACTIVE: preallocated, completely owned PHYSICAL assembly-name inventory.
// Producer must invoke Begin/Add/Seal under s_assemblyLock on a reviewed owner.
// Preserves CopyValidAssemblies semantics: source order, token!=0, duplicates,
// physical interpreter bit, and independently sampled active generation.
//
// No borrowed name or Il2CppAssembly* survives in a complete result. Fail
// closed on any missing/corrupt/over-budget record rather than dropping it.
// This does NOT capture class metadata, prove a global VM epoch, coordinate
// Runtime::Shutdown/MetadataAllocCleanup, or authorize terminal Player use.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_physical {

enum class Error : uint8_t
{
    None, WrongPhase, SourceInvalid, TooMany, TextLimit,
    AliasedSource, SourceChanged
};
struct Row
{
    const char* name=nullptr;
    size_t bytes=0;
    bool interpreter=false;
};

class Capture final
{
public:
    static constexpr size_t kMaxEntries=4096;
    static constexpr size_t kMaxText=1024u*1024u;
    static constexpr size_t kMaxObjectBytes=kMaxText+65536;
private:
    struct Entry {uint32_t offset=0,bytes=0;bool interpreter=false;};
    enum class Phase:uint8_t { New, Adding, Complete, Failed };
    char text_[kMaxText]{};
    Entry entries_[kMaxEntries]{};
    uint32_t used_=0,count_=0;
    uint64_t generation_=0;
    Phase phase_=Phase::New;
    Error error_=Error::None;

    bool Fail(Error error) noexcept
    {
        if(error_==Error::None) error_=error;
        phase_=Phase::Failed;
        return false;
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;

    Error FirstError() const noexcept {return error_;}
    bool Complete() const noexcept
    {return phase_==Phase::Complete && error_==Error::None;}
    size_t Count() const noexcept {return Complete()?count_:0;}
    size_t OwnedTextBytes() const noexcept {return Complete()?used_:0;}
    uint64_t EnumerationGeneration() const noexcept
    {return Complete()?generation_:0;}
    bool RejectSource() noexcept {return Fail(Error::SourceInvalid);}

    bool Begin() noexcept
    {
        if(phase_!=Phase::New || error_!=Error::None)
            return Fail(Error::WrongPhase);
        phase_=Phase::Adding;
        return true;
    }
    // Source C-string must be lifetime-valid for the duration of this single
    // synchronous call, under the assembly registry lock. Never retain name.
    bool Add(const char* name,bool interpreter) noexcept
    {
        if(phase_!=Phase::Adding || error_!=Error::None)
            return Fail(Error::WrongPhase);
        if(!name)return Fail(Error::SourceInvalid);
        if(count_>=kMaxEntries)return Fail(Error::TooMany);
        const uintptr_t start=reinterpret_cast<uintptr_t>(name);
        const uintptr_t own=reinterpret_cast<uintptr_t>(this);
        if(sizeof(*this)>UINTPTR_MAX-own)return Fail(Error::SourceInvalid);
        if(start>=own && start<own+sizeof(*this))
            return Fail(Error::AliasedSource);
        // Bounded strlen: a source name requiring more than the ENTIRE
        // remaining budget fails, without emitting a truncated name.
        const size_t remaining=kMaxText-used_;
        size_t length=0;
        while(length<=remaining && name[length])
        {
            if(start>=UINTPTR_MAX-length)return Fail(Error::SourceInvalid);
            ++length;
        }
        if(length>remaining)return Fail(Error::TextLimit);
        if(length && start<own+sizeof(*this) &&
           own<start+length)return Fail(Error::AliasedSource);
        if(length)std::memcpy(text_+used_,name,length);
        Entry& entry=entries_[count_++];
        entry.offset=used_;
        entry.bytes=static_cast<uint32_t>(length);
        entry.interpreter=interpreter;
        used_+=static_cast<uint32_t>(length);
        return true;
    }
    bool Seal(uint64_t generation,bool sourceStable) noexcept
    {
        if(phase_!=Phase::Adding || error_!=Error::None)
            return Fail(Error::WrongPhase);
        if(!sourceStable)return Fail(Error::SourceChanged);
        generation_=generation;
        phase_=Phase::Complete;
        return true;
    }
    bool Get(size_t index,Row& result) const noexcept
    {
        if(!Complete() || index>=count_)return false;
        const Entry& entry=entries_[index];
        result.name=text_+entry.offset;
        result.bytes=entry.bytes;
        result.interpreter=entry.interpreter;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaxObjectBytes,
              "P1 owned physical assembly inventory grew beyond fixed review budget");
static_assert(Capture::kMaxText<=UINT32_MAX,"text offset representation too small");

}}} // il2cpp::vm::assembly_shadow_bounded_physical
