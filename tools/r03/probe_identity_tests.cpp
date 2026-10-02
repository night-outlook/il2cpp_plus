#include "vm/AssemblyShadowProbeTypeSnapshot.h"
#include "vm/AssemblyShadowObservationCounters.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
using il2cpp::vm::assembly_shadow_r03::ProbeTypeSnapshot;
using il2cpp::vm::assembly_shadow_r02::ObservationCounters;
using il2cpp::vm::assembly_shadow_r02::Metric;
static std::string Render(const ProbeTypeSnapshot& value)
{ std::ostringstream text; value.WriteJson(text); return text.str(); }
static ProbeTypeSnapshot Capture(const std::string& key)
{
    ProbeTypeSnapshot value;
    // Deliberately non-dereferenceable identity. The serializer must only print it.
    assert(value.Capture(reinterpret_cast<const void*>(1), "assembly", "Namespace", "Node", key.c_str()));
    return value;
}
int main(int argc, char** argv)
{
    assert(argc == 2); const std::string mode = argv[1];
    static_assert(std::is_trivially_copyable<ProbeTypeSnapshot>::value, "Owned bytes are fixed-copy values");
    const std::string key = "type(8:assembly/9:Namespace/5:Outer@1/0:/4:Node@0)";
    if (mode == "owned")
    {
        char assembly[] = "assembly", space[] = "Namespace", name[] = "Node";
        std::string source = key; ProbeTypeSnapshot value;
        assert(value.Capture(reinterpret_cast<const void*>(1), assembly, space, name, source.c_str()));
        const auto before = Render(value);
        std::memset(assembly, '?', sizeof(assembly)); std::memset(space, '?', sizeof(space));
        std::memset(name, '?', sizeof(name)); source.clear(); source.shrink_to_fit();
        assert(Render(value) == before && before.find(key) != std::string::npos);
    }
    else if (mode == "copy")
    {
        ProbeTypeSnapshot copy;
        { const auto source = Capture(key); copy = source; }
        assert(copy.Complete() && Render(copy).find(key) != std::string::npos);
    }
    else if (mode == "generic")
    {
        const std::string composite = "generic(type(8:mscorlib/31:System.Runtime.CompilerServices/22:ConditionalWeakTable`2@2/0:/10:Enumerator@0),szarray(szarray(type(8:mscorlib/6:System/4:Byte@0))))";
        const auto value = Capture(composite);
        assert(Render(value).find(composite) != std::string::npos);
    }
    else if (mode == "missing")
    {
        ProbeTypeSnapshot value; assert(!value.Complete());
        assert(std::strcmp(value.StatusName(), "NotCaptured") == 0);
        assert(!value.Capture(nullptr, "a", "", "n", "key"));
        assert(std::strcmp(value.StatusName(), "MissingInput") == 0);
        assert(Render(value).find("\"typeKey\":\"\"") != std::string::npos);
    }
    else if (mode == "limits")
    {
        auto value = Capture(std::string(ProbeTypeSnapshot::kKeyCapacity - 1, 'k'));
        assert(value.Complete());
        assert(!value.Capture(reinterpret_cast<const void*>(1), "a", "", "n", std::string(ProbeTypeSnapshot::kKeyCapacity, 'k').c_str()));
        assert(std::strcmp(value.StatusName(), "TooLong") == 0);
        assert(Render(value).find("\"typeKey\":\"\"") != std::string::npos);
    }
    else if (mode == "text-bounds")
    {
        ProbeTypeSnapshot value;
        const std::string limit(ProbeTypeSnapshot::kTextCapacity, 'x');
        assert(!value.Capture(reinterpret_cast<const void*>(1), limit.c_str(), "", "n", "key"));
        assert(!value.Capture(reinterpret_cast<const void*>(1), "a", limit.c_str(), "n", "key"));
        assert(!value.Capture(reinterpret_cast<const void*>(1), "a", "", limit.c_str(), "key"));
        assert(!value.Complete());
    }
    else if (mode == "escape")
    {
        auto value = Capture("nested\"\\\n\t\r\x01/Utf8-\xc3\xa9");
        assert(Render(value).find("nested\\\"\\\\\\u000a\\u0009\\u000d\\u0001/Utf8-\xc3\xa9") != std::string::npos);
    }
    else if (mode == "format-state")
    {
        auto value = Capture(key); std::ostringstream text;
        value.WriteJson(text); text << ":" << 16;
        assert(text.str().substr(text.str().size()-3) == ":16");
    }
    else if (mode == "repeat")
    {
        const auto value = Capture(key); const std::string first = Render(value);
        ObservationCounters::Add(Metric::AdmissionMisses, 16);
        const auto before = ObservationCounters::Read(Metric::AdmissionMisses);
        for (int i = 0; i < 100; ++i) assert(Render(value) == first);
        assert(ObservationCounters::Read(Metric::AdmissionMisses) == before);
    }
    else if (mode == "recapture-failure")
    {
        auto value = Capture(key); assert(!value.Capture(nullptr, nullptr, nullptr, nullptr, nullptr));
        assert(Render(value).find(key) == std::string::npos && !value.Complete());
    }
    else if (mode == "stream-failure")
    {
        const auto value = Capture(key); std::ostringstream text;
        text.exceptions(std::ios::badbit); bool caught = false;
        try { text.setstate(std::ios::badbit); value.WriteJson(text); }
        catch (const std::ios_base::failure&) { caught = true; }
        assert(caught && Render(value).find(key) != std::string::npos);
    }
    else return 2;
    std::cout << "{\"result\":\"Passed\",\"runtimeAcceptance\":false}\n";
}
