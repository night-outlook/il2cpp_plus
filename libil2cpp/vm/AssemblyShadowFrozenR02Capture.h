#pragma once
// P1 inactive component: freeze a complete R02 diagnostic suffix into owned
// fixed storage. Reads actual R02 counters at caller-selected capture instant,
// but does NOT establish cross-counter/global VM snapshot coherence.
// All capture runs on one owner while healthy. No new/dynamic allocation,
// locks, VM metadata, managed callbacks or I/O are performed here.
#include "AssemblyShadowR02Diagnostics.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace il2cpp { namespace vm { namespace assembly_shadow_frozen_r02 {

class Capture final
{
public:
    static constexpr size_t kCapacity = 8192;
    enum class Error : uint8_t { None, AlreadyUsed, Capacity, SourceOrSink };
private:
    char bytes_[kCapacity]{};
    size_t used_ = 0;
    Error error_ = Error::None;
    bool attempted_ = false, complete_ = false;
    static bool Consume(void* context,const char* bytes,size_t size) noexcept
    {
        Capture& self=*static_cast<Capture*>(context);
        if (!bytes && size) return false;
        if (size > kCapacity-self.used_) return false;
        if (size) std::memcpy(self.bytes_+self.used_,bytes,size);
        self.used_+=size;
        return true;
    }
public:
    Capture() noexcept = default;
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    Error FirstError() const noexcept { return error_; }
    bool Complete() const noexcept { return attempted_ && complete_ && error_==Error::None; }
    const char* Data() const noexcept { return Complete() ? bytes_ : nullptr; }
    size_t Bytes() const noexcept { return Complete() ? used_ : 0; }
    // Must be called at a separately reviewed VM ownership boundary. The
    // counter reads in AppendDiagnostics are still independent atomics.
    bool Freeze() noexcept
    {
        if (attempted_)
        {
            if (error_==Error::None) error_=Error::AlreadyUsed;
            complete_=false;
            return false;
        }
        attempted_=true;
        assembly_shadow_diagnostic::Writer writer({this,Consume},kCapacity);
        assembly_shadow_r02::AppendDiagnostics(writer);
        const auto result=writer.Finish();
        if (!result.complete || result.bytesAccepted!=used_)
        {
            error_=result.error==assembly_shadow_diagnostic::WriteError::ByteLimit
                ? Error::Capacity : Error::SourceOrSink;
            return false;
        }
        // This is a format prefix check, NOT a semantic JSON authenticator.
        static constexpr char kPrefix[]=",\"r02\":{";
        if (used_<sizeof(kPrefix)-1 || std::memcmp(bytes_,kPrefix,sizeof(kPrefix)-1)!=0)
        {
            error_=Error::SourceOrSink;
            return false;
        }
        complete_=true;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kCapacity+64,
              "unexpected frozen R02 component state growth");

}}} // il2cpp::vm::assembly_shadow_frozen_r02
