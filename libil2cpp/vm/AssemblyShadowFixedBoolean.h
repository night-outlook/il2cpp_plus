#pragma once
// IR-LOCAL-I-RUNTIME-01: reserve the fixed physical Boolean while healthy.
// Terminal reporting performs no allocation, class initialization or GC-handle
// operation. Ordinary Object/AssemblyShadow guards are unchanged.
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "gc/GCHandle.h"
#include "vm/AssemblyShadow.h"
#include "vm/Image.h"
#include "vm/Object.h"
#include "vm/AssemblyShadowTerminalRejection.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace il2cpp { namespace vm { namespace assembly_shadow_reporting {

struct FixedBooleanReserve
{
    std::atomic<Il2CppObject*> object{nullptr};
    Il2CppClass* physicalClass = nullptr;
    uint32_t pinnedHandle = 0;
};

inline FixedBooleanReserve& BooleanReserve()
{
    static FixedBooleanReserve reserve;
    return reserve;
}

[[noreturn]] inline void FixedBooleanFatal(const char* reason) noexcept
{
    // Only fixed native text and primitive metadata: never managed formatting.
    const Il2CppClass* k = il2cpp_defaults.boolean_class;
    std::fprintf(stderr,
        "[AssemblyShadowTerminalReport] fatal=%s booleanPresent=%d initialized=%d cctorComplete=%d references=%d finalizer=%d size=%u\n",
        reason, k != nullptr, k ? static_cast<int>(k->initialized_and_no_error) : 0,
        k ? static_cast<int>(k->cctor_finished_or_no_cctor) : 0,
        k ? static_cast<int>(k->has_references) : 0,
        k ? static_cast<int>(k->has_finalize) : 0,
        k ? static_cast<unsigned>(k->instance_size) : 0u);
    std::fflush(stderr);
    std::abort();
}

inline bool IsPhysicalBoolean(const Il2CppClass* k) noexcept
{
    return k && k == il2cpp_defaults.boolean_class &&
        k->image && k->image == Image::GetCorlib() && !k->generic_class &&
        k->name && k->namespaze && !std::strcmp(k->name, "Boolean") &&
        !std::strcmp(k->namespaze, "System");
}

inline bool IsReadyBoolean(const Il2CppClass* k) noexcept
{
    return IsPhysicalBoolean(k) && k->initialized_and_no_error &&
        k->byval_arg.valuetype && k->byval_arg.type == IL2CPP_TYPE_BOOLEAN &&
        !k->has_references && !k->has_finalize && k->cctor_finished_or_no_cctor &&
        k->instance_size >= sizeof(Il2CppObject) + sizeof(bool) &&
        k->instance_size <= sizeof(Il2CppObject) + 16;
}

inline bool HasTruePayload(const Il2CppObject* object) noexcept
{
    // Read a byte rather than an invalid bool representation on corruption.
    static_assert(sizeof(bool) == 1, "Fixed Boolean transport requires one byte");
    return *(reinterpret_cast<const unsigned char*>(object) + sizeof(Il2CppObject)) == 1;
}

// Called once under Runtime's initialization lock, BEFORE MarkCoreReady opens
// the transaction startup gateway. Normal guarded allocation/cctor/profiler
// behavior is permitted only here, while Assembly Shadow is Disabled.
inline void InitializeFixedHandledResult() noexcept
{
    auto& reserve = BooleanReserve();
    const char* stage = "FixedBooleanBootstrapState";
    try
    {
        AssemblyShadowState state;
        AssemblyShadow::GetState(state);
        if (state != AssemblyShadowState::Disabled ||
            reserve.object.load(std::memory_order_acquire) || reserve.pinnedHandle)
            FixedBooleanFatal(stage);
        Il2CppClass* klass = il2cpp_defaults.boolean_class;
        stage = "FixedBooleanBootstrapIdentity";
        if (!IsPhysicalBoolean(klass)) FixedBooleanFatal(stage);
        stage = "FixedBooleanBootstrapBox";
        bool handled = true;
        Il2CppObject* object = Object::Box(klass, &handled);
        stage = "FixedBooleanBootstrapReadiness";
        if (!IsReadyBoolean(klass)) FixedBooleanFatal(stage);
        stage = "FixedBooleanBootstrapObject";
        if (!object || object->klass != klass || !HasTruePayload(object))
            FixedBooleanFatal(stage);
        stage = "FixedBooleanBootstrapRoot";
        const uint32_t handle = gc::GCHandle::New(object, true);
        if (!handle || gc::GCHandle::GetTarget(handle) != object)
            FixedBooleanFatal(stage);
        reserve.physicalClass = klass;
        reserve.pinnedHandle = handle;
        reserve.object.store(object, std::memory_order_release);
        InitializeTerminalRejection();
        std::fprintf(stderr,
            "[AssemblyShadowBooleanReserve] prepared=true pinned=true size=%u cctorComplete=%d\n",
            static_cast<unsigned>(klass->instance_size),
            static_cast<int>(klass->cctor_finished_or_no_cctor));
        std::fflush(stderr);
    }
    catch (...)
    {
        FixedBooleanFatal(stage);
    }
}

inline Il2CppObject* FixedHandledResult() noexcept
{
    auto& reserve = BooleanReserve();
    Il2CppObject* object = reserve.object.load(std::memory_order_acquire);
    if (!object) FixedBooleanFatal("FixedBooleanReserveUnavailable");
    if (!IsReadyBoolean(reserve.physicalClass) || object->klass != reserve.physicalClass)
        FixedBooleanFatal("FixedBooleanReserveIdentity");
    if (!HasTruePayload(object)) FixedBooleanFatal("FixedBooleanReserveValue");
    return object;
}

// Runtime shutdown calls this after all managed threads/finalizers stop and
// before metadata/GC teardown. No reader may outlive that quiescence boundary.
inline void ShutdownFixedHandledResult() noexcept
{
    auto& reserve = BooleanReserve();
    reserve.object.store(nullptr, std::memory_order_release);
    if (reserve.pinnedHandle) gc::GCHandle::Free(reserve.pinnedHandle);
    reserve.pinnedHandle = 0;
    reserve.physicalClass = nullptr;
    ShutdownTerminalRejection();
}

}}} // il2cpp::vm::assembly_shadow_reporting
#endif
