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
    typedef std::vector<const Il2CppAssembly*> AssemblyVector;
    typedef std::vector<const Il2CppAssemblyName*> AssemblyNameVector;

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
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        // Caller owns transaction -> metadata locks. Both callbacks are native,
        // nonthrowing and must not allocate or enter managed code. Storage is
        // reserved before tryBegin; publication and version bump share one lock.
        static bool PublishShadowBatch(const AssemblyVector& assemblies,
            bool (*tryBegin)(void*), void (*publish)(void*), void* context);
        static uint64_t CaptureShadowEnumeration(AssemblyVector& assemblies);
#endif
        static void InvalidateAssemblyList();
        static void ClearAllAssemblies();
        static void Initialize();

    private:
    };
} /* namespace vm */
} /* namespace il2cpp */
