#pragma once
// P1 staged component: an allocation-free, owned, frozen execution-diagnostic
// representation. This DOES NOT read VM metadata, own VM locks, certify a
// coherent census or authorize post-terminal Player use.
//
// Allocate this object while healthy, capture typed inputs under separately
// reviewed source lifetimes/locks, then freeze before releasing their storage.
// All variable text is deep-copied. No caller pointer survives the capture.
// Freeze and SerializeTo are single-owner; no mutable access after Freeze.
// A failure poisons the entire capture. Never truncate, retry or accept a
// prefix. Complete outer diagnostic wire/semantic authentication is separate.
#include "AssemblyShadowExecutionSerializer.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_frozen_execution {

using assembly_shadow_diagnostic::QuotedText;
using assembly_shadow_diagnostic::Sink;
using assembly_shadow_diagnostic::WriteResult;
using assembly_shadow_diagnostic::WriteError;
using assembly_shadow_execution_diagnostic::Header;
using assembly_shadow_execution_diagnostic::ClassRow;

enum class CaptureError : uint8_t
{
    None, InvalidInput, WrongState, RowLimit, TextLimit, AliasedInput, MissingR02
};

class Capture final
{
public:
    // Source-defined kMaximumExecutionClasses is 1024. This class must not
    // silently accept a different enumeration capacity without re-review.
    static constexpr size_t kMaxRows = 1024;
    static constexpr size_t kMaxTextBytes = 2u * 1024u * 1024u;
    static constexpr size_t kMaxCaptureObjectBytes = 3u * 1024u * 1024u;
private:
    struct Span { uint32_t offset = 0, size = 0; };
    struct OwnedRow
    {
        Span assembly, typeKey, pointer;
        uint16_t flags = 0;
    };
    enum : uint16_t
    {
        Shadow=1u<<0, PhysicalInterpreter=1u<<1, Active=1u<<2,
        CctorStarted=1u<<3, CctorFinished=1u<<4,
        InitializationException=1u<<5, PointerDetails=1u<<6,
        StaticStorage=1u<<7
    };
    Header header_{};
    Span state_{}, r02_{};
    OwnedRow rows_[kMaxRows]{};
    char text_[kMaxTextBytes]{};
    uint32_t used_=0, count_=0;
    CaptureError failure_=CaptureError::None;
    bool started_=false, r02Set_=false, frozen_=false;

    bool Fail(CaptureError error) noexcept
    {
        if (failure_==CaptureError::None) failure_=error;
        return false;
    }
    bool ValidNewInput(const char* data,size_t length) noexcept
    {
        if (!data) return Fail(CaptureError::InvalidInput);
        const uintptr_t p=reinterpret_cast<uintptr_t>(data);
        const uintptr_t a=reinterpret_cast<uintptr_t>(text_);
        if (length>UINTPTR_MAX-p || kMaxTextBytes>UINTPTR_MAX-a)
            return Fail(CaptureError::InvalidInput);
        if (length && p<a+kMaxTextBytes && a<p+length)
            return Fail(CaptureError::AliasedInput);
        if (length>kMaxTextBytes-used_) return Fail(CaptureError::TextLimit);
        return true;
    }
    bool Copy(const char* data,size_t length,Span& out) noexcept
    {
        if (!ValidNewInput(data,length)) return false;
        out.offset=used_;
        out.size=static_cast<uint32_t>(length);
        if (length) std::memcpy(text_+used_,data,length);
        used_+=static_cast<uint32_t>(length);
        return true;
    }
    QuotedText View(Span span) const noexcept
    {
        return {text_+span.offset,span.size};
    }
public:
    Capture() noexcept = default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;

    CaptureError FirstError() const noexcept { return failure_; }
    uint32_t CapturedClasses() const noexcept { return count_; }
    uint32_t CapturedTextBytes() const noexcept { return used_; }
    bool Frozen() const noexcept { return frozen_ && failure_==CaptureError::None; }

    bool Begin(const Header& header) noexcept
    {
        if (failure_!=CaptureError::None) return false;
        if (started_ || frozen_) return Fail(CaptureError::WrongState);
        if (!header.state) return Fail(CaptureError::InvalidInput);
        const size_t n=std::strlen(header.state);
        if (n==0 || n>=128 || n==SIZE_MAX) return Fail(CaptureError::InvalidInput);
        // Preserve a terminator for Header::state; Quoted() uses strlen().
        if (!Copy(header.state,n+1,state_)) return false;
        header_=header;
        header_.state=nullptr; // A caller-owned pointer must not remain.
        started_=true;
        return true;
    }
    bool Add(const ClassRow& value) noexcept
    {
        if (failure_!=CaptureError::None) return false;
        if (!started_ || frozen_ || r02Set_) return Fail(CaptureError::WrongState);
        if (count_==kMaxRows) return Fail(CaptureError::RowLimit);
        if (!value.logicalAssembly.data || !value.typeKey.data ||
            !value.staticStoragePointer.data)
            return Fail(CaptureError::InvalidInput);
        auto& row=rows_[count_];
        if (!Copy(value.logicalAssembly.data,value.logicalAssembly.bytes,row.assembly) ||
            !Copy(value.typeKey.data,value.typeKey.bytes,row.typeKey) ||
            !Copy(value.staticStoragePointer.data,value.staticStoragePointer.bytes,row.pointer))
            return false;
        row.flags=
            (value.interpreterShadow?Shadow:0) |
            (value.physicalInterpreter?PhysicalInterpreter:0) |
            (value.isActive?Active:0) |
            (value.cctorStarted?CctorStarted:0) |
            (value.cctorFinished?CctorFinished:0) |
            (value.hasInitializationException?InitializationException:0) |
            (value.pointerDetailsAvailable?PointerDetails:0) |
            (value.staticStorageAvailable?StaticStorage:0);
        ++count_;
        return true;
    }
    // This exact suffix must be obtained from a source-reviewed R02 capture;
    // the prefix guard is NOT complete schema/semantic validation.
    bool SetR02Suffix(QuotedText suffix) noexcept
    {
        if (failure_!=CaptureError::None) return false;
        if (!started_ || frozen_ || r02Set_) return Fail(CaptureError::WrongState);
        if (!suffix.data) return Fail(CaptureError::InvalidInput);
        static constexpr char prefix[]=",\"r02\":{";
        if (suffix.bytes &&
            (suffix.bytes<sizeof(prefix)-1 ||
             std::memcmp(suffix.data,prefix,sizeof(prefix)-1)!=0))
            return Fail(CaptureError::InvalidInput);
        if (!Copy(suffix.data,suffix.bytes,r02_)) return false;
        r02Set_=true;
        return true;
    }
    // The ordinary production contract requires R02. The empty suffix is
    // allowed ONLY by an explicit host-fixture exception: the unchanged N
    // reference function's R02 mock emits no content.
    bool Freeze(bool historicalHostFixtureWithoutR02=false) noexcept
    {
        if (failure_!=CaptureError::None) return false;
        if (!started_ || frozen_ || !r02Set_)
            return Fail(CaptureError::WrongState);
        if (!r02_.size && !historicalHostFixtureWithoutR02)
            return Fail(CaptureError::MissingR02);
        frozen_=true;
        return true;
    }
    // Serialization touches only owned bytes and scalar values. No VM, locks,
    // class pointers, String, std::ostringstream or heap allocation. The
    // single-owner synchronous sink must not retain any supplied pointers.
    WriteResult SerializeTo(Sink sink,size_t maximumBytes) const noexcept
    {
        if (!Frozen())
        {
            WriteResult failure;
            failure.error=WriteError::InvalidInput;
            return failure;
        }
        assembly_shadow_diagnostic::Writer out(sink,maximumBytes);
        Header h=header_;
        h.state=text_+state_.offset;
        assembly_shadow_execution_diagnostic::Begin(out,h);
        for (uint32_t i=0;i<count_;++i)
        {
            const auto& item=rows_[i];
            const uint16_t flags=item.flags;
            ClassRow row;
            row.logicalAssembly=View(item.assembly);
            row.typeKey=View(item.typeKey);
            row.staticStoragePointer=View(item.pointer);
            row.interpreterShadow=(flags&Shadow)!=0;
            row.physicalInterpreter=(flags&PhysicalInterpreter)!=0;
            row.isActive=(flags&Active)!=0;
            row.cctorStarted=(flags&CctorStarted)!=0;
            row.cctorFinished=(flags&CctorFinished)!=0;
            row.hasInitializationException=(flags&InitializationException)!=0;
            row.pointerDetailsAvailable=(flags&PointerDetails)!=0;
            row.staticStorageAvailable=(flags&StaticStorage)!=0;
            assembly_shadow_execution_diagnostic::AppendClass(out,row,i!=0);
        }
        assembly_shadow_execution_diagnostic::EndClasses(out);
        if (r02_.size) out.write(text_+r02_.offset,static_cast<std::streamsize>(r02_.size));
        out<<'}';
        return out.Finish();
    }
};
static_assert(sizeof(Capture)<=Capture::kMaxCaptureObjectBytes,
              "P1 owned execution-capture memory budget exceeded");
static_assert(Capture::kMaxTextBytes<=UINT32_MAX,"offset representation overflow");

}}} // il2cpp::vm::assembly_shadow_frozen_execution
