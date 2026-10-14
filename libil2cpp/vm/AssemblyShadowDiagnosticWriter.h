#pragma once
// Bounded formatting primitive. Input snapshots must be immutable, disjoint
// from the output and fully captured before entry. This is NOT a VM census,
// a file transaction, an allocation bound for capture, or semantic acceptance.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <type_traits>

namespace il2cpp { namespace vm { namespace assembly_shadow_diagnostic {

struct QuotedText { const char* data; size_t bytes; };
inline QuotedText Quoted(const std::string& value) noexcept
{ return {value.data(), value.size()}; }
inline QuotedText Quoted(const char* value) noexcept
{ return {value, value ? std::strlen(value) : 0}; }

// The same escaping operations are used for legacy ostream and fixed sinks.
// UTF-8 bytes are not normalized. Embedded NUL/control bytes are escaped.
template<class Output>
inline void WriteQuoted(Output& out, QuotedText text)
{
    static constexpr char hex[] = "0123456789abcdef";
    out << '"';
    size_t first = 0;
    for (size_t i = 0; i < text.bytes; ++i)
    {
        const unsigned char value = static_cast<unsigned char>(text.data[i]);
        if (value >= 32 && value != '"' && value != '\\') continue;
        if (i != first) out.write(text.data + first, static_cast<std::streamsize>(i - first));
        if (value == '"' || value == '\\') { out << '\\'; out << static_cast<char>(value); }
        else
        {
            const char escaped[] = {'\\', 'u', '0', '0', hex[value >> 4], hex[value & 15]};
            out.write(escaped, 6);
        }
        first = i + 1;
    }
    if (first != text.bytes)
        out.write(text.data + first, static_cast<std::streamsize>(text.bytes - first));
    out << '"';
}
inline std::ostream& operator<<(std::ostream& out, QuotedText text)
{ WriteQuoted(out, text); return out; }

enum class WriteError : uint8_t { None, InvalidInput, ByteLimit, SinkFailure, Reentered, Finished, UnsupportedLocale };
// A true return means the synchronous callback consumed every byte. A false
// return invalidates the entire emission; unknown partial sink writes cannot
// be counted as a complete field. Callback must not retain the scratch pointer.
using SinkWrite = bool (*)(void*, const char*, size_t) noexcept;
struct Sink { void* context; SinkWrite write; };
struct WriteResult
{
    WriteError error = WriteError::None;
    size_t bytesAccepted = 0; // Successful callback bytes, NOT exact input length on failure.
    bool complete = false;
};

// No owned storage or dynamic allocation. A formatter may continue visiting
// immutable input after a failure, but no further sink call is made. Re-entry
// is rejected; cross-thread access is unsupported (single-owner contract).
class Writer
{
    Sink sink_;
    size_t limit_;
    WriteResult result_{};
    bool inside_ = false;
    bool finished_ = false;
    void Fail(WriteError error) noexcept
    { if (result_.error == WriteError::None) result_.error = error; }
public:
    Writer(Sink sink, size_t maximumBytes) noexcept : sink_(sink), limit_(maximumBytes)
    { if (!sink.write) Fail(WriteError::InvalidInput); }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer& write(const char* data, std::streamsize signedBytes) noexcept
    {
        if (inside_) { Fail(WriteError::Reentered); return *this; }
        if (finished_) { Fail(WriteError::Finished); result_.complete = false; return *this; }
        if (result_.error != WriteError::None) return *this;
        if (signedBytes < 0 || (!data && signedBytes)) { Fail(WriteError::InvalidInput); return *this; }
        const size_t bytes = static_cast<size_t>(signedBytes);
        if (bytes > limit_ - result_.bytesAccepted) { Fail(WriteError::ByteLimit); return *this; }
        if (!bytes) return *this;
        inside_ = true;
        const bool ok = sink_.write(sink_.context, data, bytes);
        inside_ = false;
        // Re-entry may have invalidated us during the sink callback.
        if (!ok) Fail(WriteError::SinkFailure);
        if (result_.error == WriteError::None) result_.bytesAccepted += bytes;
        return *this;
    }
    Writer& operator<<(const char* value) noexcept
    {
        if (!value) { Fail(WriteError::InvalidInput); return *this; }
        return write(value, static_cast<std::streamsize>(std::strlen(value)));
    }
    Writer& operator<<(char value) noexcept { return write(&value, 1); }
    Writer& operator<<(bool value) noexcept { return *this << (value ? "true" : "false"); }
    Writer& operator<<(QuotedText text) noexcept
    {
        if (!text.data && text.bytes) { Fail(WriteError::InvalidInput); return *this; }
        WriteQuoted(*this, text); return *this;
    }
    Writer& operator<<(std::ios_base& (*manipulator)(std::ios_base&)) noexcept
    {
        if (manipulator != static_cast<std::ios_base& (*)(std::ios_base&)>(std::boolalpha))
            Fail(WriteError::InvalidInput);
        return *this;
    }
    template<class T, typename std::enable_if<std::is_integral<T>::value &&
        !std::is_same<T, bool>::value && !std::is_same<T, char>::value, int>::type = 0>
    Writer& operator<<(T value) noexcept
    {
        using U = typename std::make_unsigned<T>::type;
        // unsigned subtraction also handles the minimum signed value.
        const bool negative = std::is_signed<T>::value && value < 0;
        U magnitude = static_cast<U>(value);
        if (negative) magnitude = U(0) - magnitude;
        char buffer[3 * sizeof(T) + 2];
        char* end = buffer + sizeof(buffer); char* first = end;
        do { *--first = static_cast<char>('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
        if (negative) *--first = '-';
        return write(first, static_cast<std::streamsize>(end - first));
    }
    WriteResult Finish() noexcept
    {
        if (inside_) Fail(WriteError::Reentered);
        if (finished_) Fail(WriteError::Finished);
        finished_ = true;
        result_.complete = result_.error == WriteError::None;
        return result_;
    }
};

}}}
