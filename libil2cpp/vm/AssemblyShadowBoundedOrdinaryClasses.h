#pragma once
// P1 INACTIVE: lossless owned ordinary-class diagnostic rows.
// Intended caller holds g_MetadataLock and invokes il2cpp_class_for_each;
// source names and metadata must remain alive during each synchronous Add.
// NO Il2CppClass/Image/Type pointer survives here.
//
// A successful row contains the exact existing general V1 fields:
// assemblyName, typeName (nonempty namespace + '.' + name), isInterpreter,
// isConstructedGeneric, usesStagedMetadata. No partial output is accepted.
// These buffers do NOT establish metadata shutdown/read quiescence,
// class iteration allocation bounds, or one global VM snapshot epoch.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_classes {

enum class Error : uint8_t
{
    None, WrongPhase, NullName, AliasedInput,
    TooManyClasses, TextCapacity, SourceChanged
};
struct ClassView
{
    const char* assemblyName=nullptr;
    size_t assemblyNameBytes=0;
    const char* typeName=nullptr;
    size_t typeNameBytes=0;
    bool isInterpreter=false;
    bool isConstructedGeneric=false;
    bool usesStagedMetadata=false;
};
class Capture final
{
public:
    static constexpr size_t kMaximumClasses=8192;
    static constexpr size_t kTextCapacity=2u*1024u*1024u;
    static constexpr size_t kMaximumObjectBytes=kTextCapacity+256u*1024u;
private:
    struct Span {uint32_t offset=0,bytes=0;};
    struct Row
    {
        Span assembly{},type{};
        uint8_t flags=0;
    };
    enum class Phase:uint8_t { New, Enumerating, Sealed, Failed };
    enum : uint8_t { Interpreter=1, ConstructedGeneric=2, StagedMetadata=4 };
    char text_[kTextCapacity]{};
    Row classes_[kMaximumClasses]{};
    uint32_t used_=0,count_=0;
    uint64_t generation_=0;
    Phase phase_=Phase::New;
    Error error_=Error::None;
    bool Fail(Error e) noexcept
    {
        if(error_==Error::None) error_=e;
        phase_=Phase::Failed;
        return false;
    }
    bool AddChar(char c) noexcept
    {
        if(used_>=kTextCapacity)return Fail(Error::TextCapacity);
        text_[used_++]=c;
        return true;
    }
    // Caller guarantees memory is valid for the synchronous bounded scan.
    // A missing terminator before the remaining budget fails closed.
    // Reject pointers into ANY owned part of this object before dereference.
    bool AppendName(const char* src,Span& output) noexcept
    {
        if(!src)return Fail(Error::NullName);
        const uintptr_t own=reinterpret_cast<uintptr_t>(this);
        const uintptr_t ptr=reinterpret_cast<uintptr_t>(src);
        if(sizeof(*this)>UINTPTR_MAX-own)return Fail(Error::NullName);
        if(ptr>=own && ptr<own+sizeof(*this))return Fail(Error::AliasedInput);
        const size_t available=kTextCapacity-used_;
        size_t length=0;
        while(length<=available)
        {
            if(length>=UINTPTR_MAX-ptr)return Fail(Error::NullName);
            if(src[length]=='\0') break;
            ++length;
        }
        if(length>available)return Fail(Error::TextCapacity);
        if(length && ptr<own+sizeof(*this) &&
           own<ptr+length)return Fail(Error::AliasedInput);
        output.offset=used_;
        output.bytes=static_cast<uint32_t>(length);
        if(length)std::memcpy(text_+used_,src,length);
        used_+=static_cast<uint32_t>(length);
        return true;
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    bool Complete() const noexcept
    {return phase_==Phase::Sealed && error_==Error::None;}
    Error FirstError() const noexcept {return error_;}
    size_t Count() const noexcept {return Complete()?count_:0;}
    size_t OwnedTextBytes() const noexcept {return Complete()?used_:0;}
    uint64_t ClassEnumerationGeneration() const noexcept
    {return Complete()?generation_:0;}
    bool RejectInvalidSource() noexcept {return Fail(Error::NullName);}

    bool Begin() noexcept
    {
        if(phase_!=Phase::New||error_!=Error::None)return Fail(Error::WrongPhase);
        phase_=Phase::Enumerating;
        return true;
    }
    bool Add(const char* assemblyName,const char* nameSpace,const char* name,
             bool interpreter,bool constructedGeneric,bool staged) noexcept
    {
        if(phase_!=Phase::Enumerating||error_!=Error::None)return Fail(Error::WrongPhase);
        if(count_==kMaximumClasses)return Fail(Error::TooManyClasses);
        if(!assemblyName||!name)return Fail(Error::NullName);
        Row& row=classes_[count_];
        if(!AppendName(assemblyName,row.assembly))return false;
        // Legacy GetDiagnosticsJson writes namespace + '.' only when namespace
        // is not null AND not empty; a missing namespace is equivalent to "".
        row.type.offset=used_;
        if(nameSpace&&nameSpace[0])
        {
            Span ns;
            if(!AppendName(nameSpace,ns)||!AddChar('.'))return false;
        }
        Span simple;
        if(!AppendName(name,simple))return false;
        row.type.bytes=used_-row.type.offset;
        row.flags=(interpreter?Interpreter:0)|
                  (constructedGeneric?ConstructedGeneric:0)|
                  (staged?StagedMetadata:0);
        ++count_;
        return true;
    }
    bool Seal(uint64_t generation,bool stable) noexcept
    {
        if(phase_!=Phase::Enumerating||error_!=Error::None)return Fail(Error::WrongPhase);
        if(!stable)return Fail(Error::SourceChanged);
        generation_=generation;
        phase_=Phase::Sealed;
        return true;
    }
    bool Get(size_t index,ClassView& out) const noexcept
    {
        if(!Complete()||index>=count_)return false;
        const auto& row=classes_[index];
        out.assemblyName=text_+row.assembly.offset;
        out.assemblyNameBytes=row.assembly.bytes;
        out.typeName=text_+row.type.offset;
        out.typeNameBytes=row.type.bytes;
        out.isInterpreter=(row.flags&Interpreter)!=0;
        out.isConstructedGeneric=(row.flags&ConstructedGeneric)!=0;
        out.usesStagedMetadata=(row.flags&StagedMetadata)!=0;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "P1 owned class table grew beyond fixed review budget");
static_assert(Capture::kTextCapacity<=UINT32_MAX,
              "P1 class text offsets exceed representation");
}}} // il2cpp::vm::assembly_shadow_bounded_classes
