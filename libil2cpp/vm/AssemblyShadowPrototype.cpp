#include "AssemblyShadowPrototype.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "hybridclr/metadata/MetadataUtil.h"
#include "vm/Image.h"
#include "utils/Logging.h"
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <execinfo.h>
#endif

namespace il2cpp { namespace vm {
namespace {
    const char* const kName = "AssemblyA.Implementation.Internal";
    struct PrototypeState
    {
        std::atomic<const Il2CppAssembly*> baseline{nullptr};
        std::atomic<const Il2CppAssembly*> shadow{nullptr};
        std::atomic<bool> active{false};
        std::mutex mutex;
        std::string phase = "engine-before-bootstrap";
        std::vector<std::string> events;
        std::map<std::string, unsigned> counts;
    };

    PrototypeState& State()
    {
        // Process-lifetime prototype: avoid logger/allocator destruction ordering.
        static PrototypeState* state = new PrototypeState();
        return *state;
    }

    std::string Quote(const std::string& value)
    {
        std::string result = "\"";
        for (unsigned char c : value)
        {
            if (c == '\\' || c == '"') { result += '\\'; result += c; }
            else if (c == '\n') result += "\\n";
            else if (c == '\r') result += "\\r";
            else if (c == '\t') result += "\\t";
            else if (c < 32) { char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", c); result += escaped; }
            else result += c;
        }
        return result + '"';
    }

    std::string Pointer(const void* value)
    {
        char text[32];
        snprintf(text, sizeof(text), "%p", value);
        return Quote(text);
    }

    bool Interpreter(const Il2CppAssembly* assembly)
    {
        return assembly && assembly->image && hybridclr::metadata::IsInterpreterImage(assembly->image);
    }

    std::string AssemblyInfo(const Il2CppAssembly* assembly)
    {
        return "{\"assembly\":" + Pointer(assembly) +
            ",\"image\":" + Pointer(assembly ? assembly->image : nullptr) +
            ",\"name\":" + Quote(assembly ? assembly->aname.name : "") +
            ",\"isInterpreter\":" + (Interpreter(assembly) ? "true" : "false") +
            ",\"matchesShadow\":" + (assembly && assembly == State().shadow.load() ? "true" : "false") + "}";
    }

    std::string Stack()
    {
        std::string result = "[";
#if defined(__APPLE__) || defined(__linux__)
        void* frames[24];
        int count = backtrace(frames, 24);
        for (int index = 2; index < count; ++index)
        {
            Dl_info info{};
            dladdr(frames[index], &info);
            if (index != 2) result += ',';
            char offset[32];
            snprintf(offset, sizeof(offset), "0x%llx", static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(frames[index]) - reinterpret_cast<uintptr_t>(info.dli_fbase)));
            result += "{\"pc\":" + Pointer(frames[index]) +
                ",\"module\":" + Quote(info.dli_fname ? info.dli_fname : "unknown") +
                ",\"moduleOffset\":" + Quote(offset) +
                ",\"symbol\":" + Quote(info.dli_sname ? info.dli_sname : "") + "}";
        }
#endif
        return result + ']';
    }

    void Trace(const char* site, const Il2CppAssembly* assembly, const Il2CppImage* image, const Il2CppClass* klass,
        const Il2CppClass* comparisonTarget = nullptr, int comparisonResult = -1, bool checkInterfaces = false)
    {
        // Logging callbacks can re-enter IL2CPP; never hold this lock over a callback.
        static thread_local bool tracing = false;
        if (tracing) return;
        struct Guard { bool& flag; Guard(bool& f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(tracing);
        auto& state = State();
        std::string line;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            std::string key = state.phase + '|' + site + '|' + (klass ? klass->name : "") + '|' + Pointer(assembly);
            if (comparisonResult >= 0)
                key += '|' + Pointer(comparisonTarget) + '|' + std::to_string(comparisonResult) + (checkInterfaces ? "|interfaces" : "|class");
            // Bounded development evidence, not production telemetry.
            if (state.events.size() >= 1500 || state.counts[key]++ >= 2) return;
            line = "{\"sequence\":" + std::to_string(state.events.size() + 1) +
                ",\"phase\":" + Quote(state.phase) + ",\"site\":" + Quote(site) +
                ",\"active\":" + (state.active.load() ? "true" : "false") +
                ",\"assembly\":" + Pointer(assembly) + ",\"image\":" + Pointer(image) +
                ",\"class\":" + Pointer(klass) +
                ",\"type\":" + Quote(klass ? std::string(klass->namespaze) + "." + klass->name : "") +
                ",\"isInterpreter\":" + (Interpreter(assembly) ? "true" : "false");
            if (comparisonResult >= 0)
                line += ",\"comparisonTargetClass\":" + Pointer(comparisonTarget) +
                    ",\"comparisonTargetType\":" + Quote(comparisonTarget ? std::string(comparisonTarget->namespaze) + "." + comparisonTarget->name : "") +
                    ",\"comparisonTargetAssembly\":" + AssemblyInfo(comparisonTarget ? comparisonTarget->image->assembly : nullptr) +
                    ",\"comparisonResult\":" + (comparisonResult ? "true" : "false") +
                    ",\"checkInterfaces\":" + (checkInterfaces ? "true" : "false");
            line += ",\"stack\":" + Stack() + "}";
            state.events.push_back(line);
        }
        utils::Logging::Write("[AssemblyShadowPoC] %s", line.c_str());
    }
}

bool AssemblyShadowPrototype::IsCandidateName(const char* name)
{
    if (!name) return false;
    const char* slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    const char* backslash = strrchr(name, '\\');
    if (backslash) name = backslash + 1;
    size_t index = 0;
    for (; kName[index]; ++index)
        if (!name[index] || std::tolower(static_cast<unsigned char>(name[index])) !=
            std::tolower(static_cast<unsigned char>(kName[index]))) return false;
    const char* suffix = name + index;
    return !*suffix || *suffix == ',' || strcmp(suffix, ".dll") == 0 || strcmp(suffix, ".exe") == 0;
}

bool AssemblyShadowPrototype::HasStagedAssembly() { return State().shadow.load() != nullptr; }

bool AssemblyShadowPrototype::Stage(const Il2CppAssembly* baseline, const Il2CppAssembly* shadow)
{
    if (!baseline || !shadow || baseline == shadow || Interpreter(baseline) || !Interpreter(shadow) ||
        !IsCandidateName(baseline->aname.name) || strcmp(baseline->aname.name, shadow->aname.name) != 0) return false;
    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.shadow.load()) return false;
        state.baseline.store(baseline);
        state.shadow.store(shadow);
    }
    Trace("Stage.baseline", baseline, baseline->image, nullptr);
    Trace("Stage.shadow.no-module-initializer", shadow, shadow->image, nullptr);
    return true;
}

bool AssemblyShadowPrototype::Activate(const char* name)
{
    auto& state = State();
    if (!IsCandidateName(name) || !state.shadow.load()) return false;
    bool expected = false;
    if (!state.active.compare_exchange_strong(expected, true)) return false;
    auto* shadow = state.shadow.load();
    Trace("Activate", shadow, shadow->image, nullptr);
    return true;
}

const Il2CppAssembly* AssemblyShadowPrototype::ResolveName(const char* name, const char* site)
{
    if (!IsCandidateName(name)) return nullptr;
    auto& state = State();
    auto* assembly = state.active.load() ? state.shadow.load() : nullptr;
    Trace(site, assembly, assembly ? assembly->image : nullptr, nullptr);
    return assembly;
}

const Il2CppImage* AssemblyShadowPrototype::ResolveImage(const Il2CppImage* image)
{
    auto& state = State();
    if (!state.active.load()) return image;
    const Il2CppAssembly* baseline = state.baseline.load();
    const Il2CppAssembly* shadow = state.shadow.load();
    if (!baseline || !shadow || image != baseline->image) return image;
    // M01's observed MonoManager/MonoScript path retains the baseline image.
    // Redirect before class lookup, never after object layout/allocation.
    Trace("ResolveImage.baseline", baseline, image, nullptr);
    Trace("ResolveImage.shadow", shadow, shadow->image, nullptr);
    return shadow->image;
}

Il2CppClass* AssemblyShadowPrototype::ResolveUnityComparisonTarget(const Il2CppClass* actual, Il2CppClass* expected)
{
    auto& state = State();
    if (!state.active.load() || !actual || !expected) return expected;
    const Il2CppAssembly* baseline = state.baseline.load();
    const Il2CppAssembly* shadow = state.shadow.load();
    if (!baseline || !shadow || actual->image != shadow->image || expected->image != baseline->image ||
        actual->is_generic || actual->generic_class || actual->declaringType ||
        expected->is_generic || expected->generic_class || expected->declaringType) return expected;
    // M01 only: Unity's string component lookup compares a physical shadow object
    // against a cached baseline MonoScript class. Canonicalize only that target;
    // never change the actual object's class or the VM's general casting rules.
    Il2CppClass* mapped = Image::ClassFromName(shadow->image, expected->namespaze, expected->name);
    return mapped ? mapped : expected;
}

void AssemblyShadowPrototype::TraceImage(const char* site, const Il2CppImage* image)
{
    if (image && image->assembly && IsCandidateName(image->assembly->aname.name))
        Trace(site, image->assembly, image, nullptr);
}

void AssemblyShadowPrototype::TraceClass(const char* site, const Il2CppClass* klass)
{
    if (klass && klass->image && klass->image->assembly && IsCandidateName(klass->image->assembly->aname.name))
        Trace(site, klass->image->assembly, klass->image, klass);
}

void AssemblyShadowPrototype::TraceTypeCheck(const char* site, const Il2CppClass* actual,
    const Il2CppClass* expected, bool checkInterfaces, bool matches)
{
    const Il2CppAssembly* actualAssembly = actual && actual->image ? actual->image->assembly : nullptr;
    const Il2CppAssembly* expectedAssembly = expected && expected->image ? expected->image->assembly : nullptr;
    if ((actualAssembly && IsCandidateName(actualAssembly->aname.name)) ||
        (expectedAssembly && IsCandidateName(expectedAssembly->aname.name)))
        Trace(site, actualAssembly, actual ? actual->image : nullptr, actual, expected, matches ? 1 : 0, checkInterfaces);
}

void AssemblyShadowPrototype::SetPhase(const char* phase)
{
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.phase = phase ? phase : "unspecified";
}

std::string AssemblyShadowPrototype::Diagnostics()
{
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    std::string result = "{\"enabled\":true,\"canonicalName\":" + Quote(kName) +
        ",\"mappingPolicy\":\"name-image-and-unity-comparison-target\"" +
        ",\"active\":" + (state.active.load() ? "true" : "false") +
        ",\"baseline\":" + AssemblyInfo(state.baseline.load()) +
        ",\"shadow\":" + AssemblyInfo(state.shadow.load()) +
        ",\"moduleInitializerRun\":false,\"events\":[";
    for (size_t index = 0; index < state.events.size(); ++index)
    {
        if (index) result += ',';
        result += state.events[index];
    }
    return result + "]}";
}

std::string AssemblyShadowPrototype::InspectObject(const Il2CppObject* object)
{
    // Read the physical object header directly: no resolver or reflection cache.
    const Il2CppClass* klass = object ? object->klass : nullptr;
    TraceClass("InspectObject.physical-header", klass);
    return "{\"object\":" + Pointer(object) + ",\"class\":" + Pointer(klass) +
        ",\"type\":" + Quote(klass ? std::string(klass->namespaze) + "." + klass->name : "") +
        ",\"instanceSize\":" + std::to_string(klass ? klass->instance_size : 0) +
        ",\"physicalAssembly\":" + AssemblyInfo(klass ? klass->image->assembly : nullptr) + "}";
}

std::string AssemblyShadowPrototype::InspectAssembly(const Il2CppAssembly* assembly)
{
    return AssemblyInfo(assembly);
}
}}
#endif
