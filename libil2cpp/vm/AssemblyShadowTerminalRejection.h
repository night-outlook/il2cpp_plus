#pragma once
// L: transport one fixed guard-rejection exception prepared while healthy.
// The native recovery record, not this shared exception's message or stack,
// remains the authority for the original failure. No ctor/stack preparation
// or GC-handle operation is performed when the exception is rethrown.
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "gc/GCHandle.h"
#include "vm/AssemblyShadow.h"
#include "vm/Exception.h"
#include "vm/Image.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace il2cpp { namespace vm { namespace assembly_shadow_reporting {
struct TerminalRejectionReserve
{
    std::atomic<Il2CppException*> object{nullptr};
    Il2CppClass* physicalClass = nullptr;
    uint32_t handle = 0;
    std::atomic<uint64_t> throws{0};
    std::atomic<uint64_t> unhandledReports{0};
};
inline TerminalRejectionReserve& RejectionReserve()
{
    static TerminalRejectionReserve reserve;
    return reserve;
}
[[noreturn]] inline void RejectionFatal(const char* stage) noexcept
{
    std::fprintf(stderr, "[AssemblyShadowTerminalRejection] fatal=%s\n", stage);
    std::fflush(stderr);
    std::abort();
}
inline bool RejectionClass(const Il2CppClass* klass) noexcept
{
    return klass && klass->image == Image::GetCorlib() && !klass->generic_class &&
        klass->name && klass->namespaze &&
        !std::strcmp(klass->name, "InvalidOperationException") &&
        !std::strcmp(klass->namespaze, "System");
}
inline void InitializeTerminalRejection() noexcept
{
    auto& reserve = RejectionReserve();
    const char* stage = "BootstrapState";
    try
    {
        AssemblyShadowState state;
        AssemblyShadow::GetState(state);
        if (state != AssemblyShadowState::Disabled || reserve.object.load() || reserve.handle)
            RejectionFatal(stage);
        stage = "HealthyExceptionConstruction";
        Il2CppException* object = Exception::GetInvalidOperationException(
            "Assembly Shadow terminal execution rejected. Read the immutable native recovery record for the original failure.");
        stage = "PhysicalExceptionIdentity";
        if (!object || !RejectionClass(object->klass) ||
            !object->klass->initialized_and_no_error || !object->klass->cctor_finished_or_no_cctor)
            RejectionFatal(stage);
        stage = "StrongPinnedRoot";
        const uint32_t handle = gc::GCHandle::New(reinterpret_cast<Il2CppObject*>(object), true);
        if (!handle || gc::GCHandle::GetTarget(handle) != reinterpret_cast<Il2CppObject*>(object))
            RejectionFatal(stage);
        reserve.physicalClass = object->klass;
        reserve.handle = handle;
        reserve.object.store(object, std::memory_order_release);
        std::fputs("[AssemblyShadowTerminalRejection] prepared=true pinned=true\n", stderr);
        std::fflush(stderr);
    }
    catch (...) { RejectionFatal(stage); }
}
inline bool IsReservedRejection(const Il2CppException* object) noexcept
{
    auto& reserve = RejectionReserve();
    return object && object == reserve.object.load(std::memory_order_acquire) &&
        object->klass == reserve.physicalClass && RejectionClass(reserve.physicalClass);
}
[[noreturn]] inline void RethrowTerminalRejection()
{
    auto& reserve = RejectionReserve();
    Il2CppException* object = reserve.object.load(std::memory_order_acquire);
    if (!IsReservedRejection(object)) RejectionFatal("ReserveUnavailableOrChanged");
    const uint64_t count = reserve.throws.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 32)
    {
        std::fprintf(stderr, "[AssemblyShadowTerminalRejection] throw=%llu constructorRun=false stackPrepared=false\n",
            static_cast<unsigned long long>(count));
        std::fflush(stderr);
    }
    // Unlike Raise, Rethrow does not call PrepareExceptionForThrow, allocate
    // stack/reflection data, invoke constructors or mutate the exception.
    Exception::Rethrow(object);
}
inline bool TryReportRejectedUnhandled(Il2CppException* object) noexcept
{
    // Only the exact native guard-rejection object is eligible. An arbitrary
    // exception (even same type/message) retains the original runtime policy.
    if (!IsReservedRejection(object)) return false;
    AssemblyShadowState state;
    AssemblyShadow::GetState(state);
    if (state != AssemblyShadowState::Failed && state != AssemblyShadowState::FailedAfterCommit)
        return false;
    const uint64_t count = RejectionReserve().unhandledReports.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 32)
    {
        std::fprintf(stderr,
            "[AssemblyShadowTerminalRejection] unhandled=%llu state=%d nativeOnly=true delegateInvoked=false\n",
            static_cast<unsigned long long>(count), static_cast<int32_t>(state));
        std::fflush(stderr);
    }
    return true;
}
inline void ShutdownTerminalRejection() noexcept
{
    auto& reserve = RejectionReserve();
    reserve.object.store(nullptr, std::memory_order_release);
    if (reserve.handle) gc::GCHandle::Free(reserve.handle);
    reserve.handle = 0;
    reserve.physicalClass = nullptr;
}
}}}
#endif
