#pragma once
// IR-LOCAL-RUNTIME-01. Native replacement of ONE engine reporting contract.
// This never admits or invokes its managed body, a custom ILogger, or a user
// callback. All ordinary RequireActiveMethod checks remain unchanged.
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "vm/AssemblyShadow.h"
#include "vm/Image.h"
#include "vm/MetadataCache.h"
#include "vm/Object.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace il2cpp { namespace vm { namespace assembly_shadow_reporting {

inline std::atomic<uint64_t>& HandledCounter()
{
    static std::atomic<uint64_t> count{0};
    return count;
}

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
        method->klass->image->assembly != physical) return false;
    return PhysicalClassType(method->parameters[0], Image::GetCorlib(), "System", "Exception") &&
        PhysicalClassType(method->parameters[1], physical->image, "UnityEngine", "Object");
}

inline void WriteMessage(const Il2CppException* error) noexcept
{
    // Inspect only object header and the native Exception message field, never
    // Exception.ToString/Message virtual dispatch or Unity logging callbacks.
    if (!error) { std::fputs("null", stderr); return; }
    const Il2CppClass* klass = error->object.klass;
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

inline bool TryHandle(const MethodInfo* method, void* instance, void** arguments,
    Il2CppObject*& result)
{
    if (!IsExactEngineReport(method, instance)) return false;
    // Reuse the actual durable-failure-aware policy, including the publication
    // race. Healthy and OFF invocations continue through the ordinary path.
    if (AssemblyShadow::AssertMethodIsActive(method, "TerminalReporting.classify")) return false;
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
    // Boxing Boolean is fixed VM allocation with no Boolean .cctor/user body.
    bool handled = true;
    result = Object::Box(il2cpp_defaults.boolean_class, &handled);
    return true;
}

}}} // namespace il2cpp::vm::assembly_shadow_reporting
#endif
