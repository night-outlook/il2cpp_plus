#pragma once
// IR-LOCAL-RUNTIME-01. Native replacement of ONE engine reporting contract.
// This never admits or invokes its managed body, a custom ILogger, or a user
// callback. All ordinary RequireActiveMethod checks remain unchanged.
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "il2cpp-tabledefs.h"
#include "vm/AssemblyShadow.h"
#include "vm/Image.h"
#include "vm/MetadataCache.h"
#include "vm/Object.h"
#include "vm/Class.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace il2cpp { namespace vm { namespace assembly_shadow_reporting {

inline std::atomic<uint64_t>& HandledCounter()
{
    static std::atomic<uint64_t> count{0};
    return count;
}

// Completion is published only after the fixed response is ready. Test-only
// native observers may read this counter; it grants no managed execution.
inline std::atomic<uint64_t>& CompletedCounter()
{
    static std::atomic<uint64_t> count{0};
    return count;
}

inline bool& TransportActive()
{
    static thread_local bool active = false;
    return active;
}

struct NativeTransportScope
{
    NativeTransportScope() noexcept
    {
        if (TransportActive())
        {
            std::fputs("[AssemblyShadowTerminalReport] fatal=NativeReportingReentry\n", stderr);
            std::fflush(stderr);
            std::abort();
        }
        TransportActive() = true;
    }
    ~NativeTransportScope() { TransportActive() = false; }
    NativeTransportScope(const NativeTransportScope&) = delete;
    NativeTransportScope& operator=(const NativeTransportScope&) = delete;
};

inline bool PhysicalClassType(const Il2CppType* type, const Il2CppImage* image,
    const char* namespaze, const char* name)
{
    if (!type || type->byref || type->type != IL2CPP_TYPE_CLASS ||
        !type->data.typeHandle || !image) return false;
    // Read original definition handles only. No Class::FromIl2CppType,
    // initialization, reflection remapping, or managed formatting is allowed.
    for (uint32_t i = 0; i < image->typeCount; ++i)
    {
        auto handle = MetadataCache::GetAssemblyTypeHandle(image, i);
        if (handle != type->data.typeHandle) continue;
        auto identity = MetadataCache::GetTypeNamespaceAndName(handle);
        return identity.first && identity.second &&
            !std::strcmp(identity.first, namespaze) && !std::strcmp(identity.second, name);
    }
    return false;
}

inline bool IsExactEngineReport(const MethodInfo* method, const void* instance)
{
    if (instance || !method || !method->name ||
        std::strcmp(method->name, "CallOverridenDebugHandler") ||
        !method->klass || !method->klass->image || !method->klass->name ||
        !method->klass->namespaze || std::strcmp(method->klass->name, "Debug") ||
        std::strcmp(method->klass->namespaze, "UnityEngine") ||
        method->is_inflated || method->is_generic || method->klass->generic_class ||
        !(method->flags & METHOD_ATTRIBUTE_STATIC) || method->parameters_count != 2 ||
        !method->parameters || !method->return_type || method->return_type->byref ||
        method->return_type->type != IL2CPP_TYPE_BOOLEAN) return false;
    const Il2CppAssembly* physical =
        MetadataCache::GetAotAssemblyByNamePhysical("UnityEngine.CoreModule");
    if (!physical || !physical->image || method->klass->image != physical->image ||
        method->klass->image->assembly != physical ||
        AssemblyShadow::IsCandidate(physical) ||
        AssemblyShadow::IsShadowedBaseline(physical) ||
        AssemblyShadow::IsActiveShadow(physical)) return false;
    return PhysicalClassType(method->parameters[0], Image::GetCorlib(), "System", "Exception") &&
        PhysicalClassType(method->parameters[1], physical->image, "UnityEngine", "Object");
}

inline void WriteMessage(const Il2CppException* error) noexcept
{
    // The official Il2CppException starts with the Il2CppObject base header;
    // it does not have a named `object` member. Never dispatch managed methods.
    if (!error) { std::fputs("null", stderr); return; }
    const Il2CppClass* klass = reinterpret_cast<const Il2CppObject*>(error)->klass;
    const Il2CppClass* ancestor = klass;
    unsigned depth = 0;
    while (ancestor && ancestor != il2cpp_defaults.exception_class && depth++ < 64)
        ancestor = ancestor->parent;
    if (ancestor != il2cpp_defaults.exception_class)
    { std::fputs("\"NonExceptionArgument\"", stderr); return; }
    std::fputc('"', stderr);
    const Il2CppString* message = error->message;
    if (message)
    {
        const int32_t length = message->length < 2048 ? message->length : 2048;
        for (int32_t i = 0; i < length; ++i)
        {
            unsigned ch = static_cast<unsigned>(message->chars[i]);
            if (ch >= 32 && ch < 127 && ch != '"' && ch != '\\')
                std::fputc(static_cast<int>(ch), stderr);
            else std::fprintf(stderr, "\\u%04x", ch);
        }
        if (message->length > length) std::fputs("[truncated]", stderr);
    }
    std::fputc('"', stderr);
}

// This allocation is deliberately NOT Object::Box/Object::New. Those generic
// paths include Runtime::ClassInit, finalizer registration and profiler callbacks.
// Only original physical Boolean metadata and reference-free native allocation
// are needed for this fixed engine return. Any failure terminates natively: it
// must not ask Unity to report another managed exception recursively.
inline Il2CppObject* FixedHandledResult() noexcept
{
    try
    {
        Il2CppClass* klass = il2cpp_defaults.boolean_class;
        if (!klass || klass->image != Image::GetCorlib() ||
            klass->generic_class || !klass->name || !klass->namespaze ||
            std::strcmp(klass->name, "Boolean") || std::strcmp(klass->namespaze, "System"))
            throw 0;
        Class::Init(klass); // Native metadata setup only, never Runtime::ClassInit.
        if (!klass->initialized_and_no_error || !klass->byval_arg.valuetype ||
            klass->byval_arg.type != IL2CPP_TYPE_BOOLEAN ||
            klass->has_references || klass->has_finalize ||
            !klass->cctor_finished_or_no_cctor ||
            klass->instance_size < sizeof(Il2CppObject) + sizeof(bool) ||
            klass->instance_size > sizeof(Il2CppObject) + 16)
            throw 0;
        Il2CppObject* boxed = Object::NewPtrFree(klass);
        if (!boxed || boxed->klass != klass) throw 0;
        *static_cast<bool*>(Object::Unbox(boxed)) = true;
        return boxed;
    }
    catch (...)
    {
        std::fputs("[AssemblyShadowTerminalReport] fatal=FixedBooleanAllocationFailed\n", stderr);
        std::fflush(stderr);
        std::abort();
    }
}

inline void TracePhase(int32_t phase) noexcept
{
    AssemblyShadowState state;
    AssemblyShadow::GetState(state);
    std::fprintf(stderr, "[AssemblyShadowPhase] phase=%d state=%d generation=%llu\n",
        static_cast<int>(phase), static_cast<int32_t>(state),
        static_cast<unsigned long long>(AssemblyShadow::ActiveGeneration()));
    std::fflush(stderr);
}

inline bool TryHandle(const MethodInfo* method, void* instance, void** arguments,
    Il2CppObject*& result)
{
    if (!IsExactEngineReport(method, instance)) return false;
    // Reuse the actual durable-failure-aware policy, including the publication
    // race. Healthy and OFF invocations continue through the ordinary path.
    if (AssemblyShadow::AssertMethodIsActive(method, "TerminalReporting.classify")) return false;
    NativeTransportScope transport;
    const uint64_t count = HandledCounter().fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 32)
    {
        AssemblyShadowState state;
        AssemblyShadow::GetState(state);
        std::fprintf(stderr, "[AssemblyShadowTerminalReport] sequence=%llu state=%d message=",
            static_cast<unsigned long long>(count), static_cast<int32_t>(state));
        WriteMessage(arguments ? static_cast<const Il2CppException*>(arguments[0]) : nullptr);
        std::fputs(" managedHandlerExecuted=false\n", stderr);
        std::fflush(stderr);
    }
    // Unity 2022.3's internal bool means the exception was handled. We handled
    // it via the native sink above, NOT via the managed callback. Returning true
    // avoids the default managed reporting cascade; returning a failure/throw
    // recursively re-enters ScriptingInvocation (the E stack-overflow defect).
    result = FixedHandledResult();
    CompletedCounter().fetch_add(1, std::memory_order_release);
    return true;
}

}}} // namespace il2cpp::vm::assembly_shadow_reporting
#endif
