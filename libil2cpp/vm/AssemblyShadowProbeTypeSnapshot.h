#pragma once
// Diagnostic-only owned identity bytes. The opaque physical pointer is never
// dereferenced here. Capture occurs with valid staging ownership; serialization
// cannot decode a metadata handle, initialize a class, or raise a managed error.
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace il2cpp { namespace vm { namespace assembly_shadow_r03 {
class ProbeTypeSnapshot
{
public:
    enum class Status { NotCaptured, Captured, MissingInput, TooLong, CaptureFailed };
    static constexpr size_t kTextCapacity = 512, kKeyCapacity = 4096;
private:
    const void* physical_ = nullptr;
    Status status_ = Status::NotCaptured;
    char assembly_[kTextCapacity] = {}, namespace_[kTextCapacity] = {};
    char name_[kTextCapacity] = {}, key_[kKeyCapacity] = {};

    template<size_t N> static bool Copy(char (&output)[N], const char* input) noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (!input[i]) { output[i] = 0; return true; }
            if (i + 1 == N) return false;
            output[i] = input[i];
        }
        return false;
    }
    static void Quote(std::ostream& out, const char* input)
    {
        static const char hex[] = "0123456789abcdef";
        out.put('"');
        for (const unsigned char* at = reinterpret_cast<const unsigned char*>(input); *at; ++at)
        {
            if (*at == '"' || *at == '\\') { out.put('\\'); out.put(static_cast<char>(*at)); }
            else if (*at < 0x20)
            { out << "\\u00"; out.put(hex[*at >> 4]); out.put(hex[*at & 15]); }
            else out.put(static_cast<char>(*at));
        }
        out.put('"');
    }
public:
    // No truncation is accepted as an identity. Failure is explicit, nonthrowing
    // and confined to this diagnostic object; it cannot change runtime state.
    bool Capture(const void* physical, const char* assembly, const char* namespaze,
        const char* name, const char* key) noexcept
    {
        physical_ = physical;
        status_ = Status::MissingInput;
        assembly_[0] = namespace_[0] = name_[0] = key_[0] = 0;
        if (!physical || !assembly || !*assembly || !namespaze || !name || !*name || !key || !*key)
            return false;
        if (!Copy(assembly_, assembly) || !Copy(namespace_, namespaze) || !Copy(name_, name) || !Copy(key_, key))
        {
            assembly_[0] = namespace_[0] = name_[0] = key_[0] = 0;
            status_ = Status::TooLong;
            return false;
        }
        status_ = Status::Captured;
        return true;
    }
    void CaptureFailed(const void* physical) noexcept
    {
        Capture(physical, nullptr, nullptr, nullptr, nullptr);
        status_ = Status::CaptureFailed;
    }
    bool Complete() const noexcept { return status_ == Status::Captured; }
    const char* StatusName() const noexcept
    {
        switch (status_)
        {
        case Status::Captured: return "Captured";
        case Status::MissingInput: return "MissingInput";
        case Status::TooLong: return "TooLong";
        case Status::CaptureFailed: return "CaptureFailed";
        default: return "NotCaptured";
        }
    }
    static const char* Policy() noexcept { return "R03OwnedLayoutIdentityV1"; }
    void WriteJson(std::ostream& out) const
    {
        // Do not let pointer formatting alter the caller's decimal counter mode.
        const auto flags = out.flags();
        out << "{\"physical\":\"0x" << std::hex << reinterpret_cast<uintptr_t>(physical_) << "\"";
        out.flags(flags);
        out << ",\"assembly\":"; Quote(out, assembly_);
        out << ",\"namespace\":"; Quote(out, namespace_);
        out << ",\"name\":"; Quote(out, name_);
        out << ",\"typeKey\":"; Quote(out, key_);
        out << "}";
    }
};
}}}
