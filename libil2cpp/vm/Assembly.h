#pragma once

#include <stdint.h>
#include <vector>
#include "il2cpp-config.h"
struct Il2CppAssembly;
struct Il2CppAssemblyName;
struct Il2CppImage;
struct Il2CppArray;

namespace il2cpp
{
namespace vm
{
    namespace assembly_shadow_bounded_physical { class Capture; }
    typedef std::vector<const Il2CppAssembly*> AssemblyVector;
    typedef std::vector<const Il2CppAssemblyName*> AssemblyNameVector;
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    enum class PhysicalAssemblyPreference { AotOnly, InterpreterOnly, AnyNewest, AnyOldest };
#endif

    class LIBIL2CPP_CODEGEN_API Assembly
    {
// exported
    public:
        static Il2CppImage* GetImage(const Il2CppAssembly* assembly);
        static void GetReferencedAssemblies(const Il2CppAssembly* assembly, AssemblyNameVector* target);
    public:
        static AssemblyVector* GetAllAssemblies();
        static void GetAllAssemblies(AssemblyVector& assemblies);
        static const Il2CppAssembly* GetLoadedAssembly(const char* name);
        static const Il2CppAssembly* Load(const char* name);
        static void Register(const Il2CppAssembly* assembly);
        static void ReserveRegistration(size_t additionalCount);
        // Caller holds g_MetadataLock. Callbacks must not throw, allocate, or
        // enter managed code. The assembly registry lock makes the placeholder
        // mutation and codec activation one reader-visible commit.
        static bool PublishInterpreterPlaceholder(void (*commit)(void*),
            bool (*publish)(void*), void (*rollback)(void*), void* context);
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        static void GetAllPhysicalAssemblies(AssemblyVector& assemblies);
        static const Il2CppAssembly* GetLoadedAssemblyPhysical(const char* name,
            PhysicalAssemblyPreference preference = PhysicalAssemblyPreference::AnyNewest);
        static const Il2CppAssembly* LoadOriginal(const char* name);
        // Caller owns transaction -> metadata locks. Both callbacks are native,
        // nonthrowing and must not allocate or enter managed code. Storage is
        // reserved before tryBegin; publication and version bump share one lock.
        static bool PublishShadowBatch(const AssemblyVector& assemblies,
            bool (*tryBegin)(void*), bool (*publish)(void*), void* context);
        static uint64_t CaptureShadowEnumeration(AssemblyVector& assemblies);
        // R03 B2 INACTIVE: synchronously visit the ORIGINAL registered AOT
        // physical image table under s_assemblyLock. The caller MUST hold
        // g_MetadataLock and a metadata Reader for the initial source session.
        // A borrowed image is valid only during this callback; no full-VM
        // epoch, class-lifetime or terminal read is implied.
        // The visitor must be nonthrowing, nonallocating, and never run
        // managed callbacks or invoke user code.
        static bool TryVisitRegisteredAotImages(
            bool (*visit)(const Il2CppImage*,void*),void* context) noexcept;
        // R03 B2 INACTIVE: mirrors the original V1 execution getter's
        // CaptureShadowEnumeration/CopyValidAssemblies token-valid physical
        // assembly enumeration INCLUDING interpreter and zero-TypeDef images.
        // No logical shadow mapping; no class creation or heap allocation.
        // Caller holds g_MetadataLock and initial metadata Reader; image
        // is borrowed only for this synchronous callback under s_assemblyLock.
        // This does not prove one coherent VM epoch or postterminal lifetime.
        static bool TryVisitOriginalV1ExecutionImages(
            bool (*visit)(const Il2CppImage*,void*),void* context) noexcept;

        // P1 additive, inactive: visit the original physical registry under
        // s_assemblyLock and synchronously deep-copy owned name/physical-kind
        // bytes. Caller must preallocate Capture while healthy. No VM type
        // access, heap vector, metadata/finalizer or managed callbacks.
        static bool TryCaptureBoundedPhysicalAssemblies(
            assembly_shadow_bounded_physical::Capture& capture) noexcept;
#endif
        static void InvalidateAssemblyList();
        static void ClearAllAssemblies();
        static void Initialize();

    private:
    };
} /* namespace vm */
} /* namespace il2cpp */
