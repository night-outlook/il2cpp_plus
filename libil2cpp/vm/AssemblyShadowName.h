#pragma once

#include "il2cpp-config.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include <cstring>
#include <string>
#include <unordered_map>
#include "vm-utils/VmStringUtils.h"

namespace il2cpp { namespace vm { namespace assembly_shadow_detail {

// A non-owning key keeps resolver queries allocation-free. Folding is the same
// UTF-16-code-unit folding as VmStringUtils::CaseInsensitiveComparer, not an
// ASCII approximation or the process locale.
struct NameView
{
    const char* begin;
    const char* end;
    bool Empty() const { return begin == end; }
};

inline char LowerAscii(char value)
{
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

inline NameView ViewName(const char* name)
{
    if (!name) return { nullptr, nullptr };
    const char* begin = name;
    const char* end = name;
    for (; *end && *end != ','; ++end)
        if (*end == '/' || *end == '\\') begin = end + 1;
    // Display names may have a space before the version separator. Internal
    // spaces in a legal simple assembly name are left untouched.
    if (*end == ',') while (end > begin && end[-1] == ' ') --end;
    if (end - begin >= 4 && end[-4] == '.' &&
        ((LowerAscii(end[-3]) == 'd' && LowerAscii(end[-2]) == 'l' && LowerAscii(end[-1]) == 'l') ||
         (LowerAscii(end[-3]) == 'e' && LowerAscii(end[-2]) == 'x' && LowerAscii(end[-1]) == 'e')))
        end -= 4;
    return { begin, end };
}

inline bool NextFolded(const char*& current, const char* end, uint16_t& first, uint16_t& second)
{
    if (current == end) return false;
    uint32_t value = static_cast<unsigned char>(*current++);
    unsigned remaining = 0;
    uint32_t minimum = 0;
    if (value >= 0xc2 && value <= 0xdf) { value &= 0x1f; remaining = 1; minimum = 0x80; }
    else if (value >= 0xe0 && value <= 0xef) { value &= 0x0f; remaining = 2; minimum = 0x800; }
    else if (value >= 0xf0 && value <= 0xf4) { value &= 7; remaining = 3; minimum = 0x10000; }
    else if (value >= 0x80 || value < 0x20 || value == 0x7f) return false;
    for (unsigned index = 0; index < remaining; ++index)
    {
        if (current == end) return false;
        unsigned char next = static_cast<unsigned char>(*current++);
        if ((next & 0xc0) != 0x80) return false;
        value = (value << 6) | (next & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    first = value > 0xffff ? static_cast<uint16_t>((value >> 10) + 55232) : static_cast<uint16_t>(value);
    second = value > 0xffff ? static_cast<uint16_t>((value & 0x3ff) + 56320) : 0;
    first = utils::VmStringUtils::Utf16ToLower(first);
    second = utils::VmStringUtils::Utf16ToLower(second);
    return true;
}

inline bool NameHash(NameView name, size_t& result)
{
    if (name.Empty()) return false;
    uint64_t hash = UINT64_C(14695981039346656037);
    const char* current = name.begin;
    while (current != name.end)
    {
        uint16_t first, second;
        if (!NextFolded(current, name.end, first, second)) return false;
        hash = (hash ^ first) * UINT64_C(1099511628211);
        hash = (hash ^ second) * UINT64_C(1099511628211);
    }
    result = static_cast<size_t>(hash);
    return true;
}

inline bool NameEquals(NameView left, NameView right)
{
    const char* l = left.begin;
    const char* r = right.begin;
    while (l != left.end && r != right.end)
    {
        uint16_t lf, ls, rf, rs;
        if (!NextFolded(l, left.end, lf, ls) || !NextFolded(r, right.end, rf, rs) || lf != rf || ls != rs)
            return false;
    }
    return l == left.end && r == right.end;
}

inline bool CanonicalName(const char* input, std::string& output)
{
    NameView name = ViewName(input);
    size_t ignored;
    if (!NameHash(name, ignored)) return false;
    output.assign(name.begin, name.end);
    return true;
}

template<class TValue> class NameIndex
{
    struct Entry { std::string name; TValue value; };
    std::unordered_multimap<size_t, Entry> entries;
public:
    void Reserve(size_t size) { entries.reserve(size); }
    void Add(const std::string& name, TValue value)
    {
        size_t hash = 0;
        NameHash(ViewName(name.c_str()), hash);
        entries.emplace(hash, Entry{ name, value });
    }
    TValue Find(const char* input) const
    {
        NameView name = ViewName(input);
        size_t hash;
        if (!NameHash(name, hash)) return TValue();
        auto range = entries.equal_range(hash);
        for (auto it = range.first; it != range.second; ++it)
            if (NameEquals(name, ViewName(it->second.name.c_str()))) return it->second.value;
        return TValue();
    }
};

}}}
#endif
