#include "il2cpp-config.h"
#include "vm/Assembly.h"
#include "vm/AssemblyShadow.h"
#include "vm/AssemblyName.h"
#include "vm/MetadataCache.h"
#include "vm/Runtime.h"
#include "vm-utils/VmStringUtils.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class-internals.h"

#include "Baselib.h"
#include "Cpp/ReentrantLock.h"
#include "os/Atomic.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "vm/AssemblyShadowName.h"
#include "hybridclr/metadata/MetadataModule.h"
#endif

#include <vector>
#include <string>

namespace il2cpp
{
namespace vm
{
    static baselib::ReentrantLock s_assemblyLock;
    // copy on write
    static int32_t s_assemblyVersion = 0;
    static AssemblyVector s_Assemblies;

    static int32_t s_snapshotAssemblyVersion = 0;
    static AssemblyVector* s_snapshotAssemblies = &s_Assemblies;

    static void CopyValidAssemblies(AssemblyVector& dst, const AssemblyVector& src)
    {
        for (AssemblyVector::const_iterator assIt = src.begin(); assIt != src.end(); ++assIt)
        {
            const Il2CppAssembly* ass = *assIt;
            if (ass->token)
            {
                dst.push_back(ass);
            }
        }
    }

    static void CopyLogicalAssemblies(AssemblyVector& dst, const AssemblyVector& src)
    {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        assembly_shadow_detail::NameIndex<const Il2CppAssembly*> seen;
        seen.Reserve(src.size());
        for (const Il2CppAssembly* physical : src)
        {
            if (!physical->token) continue;
            const Il2CppAssembly* logical = AssemblyShadow::ResolveAssembly(physical);
            if (seen.Find(logical->aname.name)) continue;
            seen.Add(logical->aname.name, logical);
            dst.push_back(logical);
        }
#else
        CopyValidAssemblies(dst, src);
#endif
    }

    AssemblyVector* Assembly::GetAllAssemblies()
    {
        os::FastAutoLock lock(&s_assemblyLock);
        if (s_assemblyVersion != s_snapshotAssemblyVersion)
        {
            s_snapshotAssemblies = new AssemblyVector();
            CopyLogicalAssemblies(*s_snapshotAssemblies, s_Assemblies);
            s_snapshotAssemblyVersion = s_assemblyVersion;
        }

        return s_snapshotAssemblies;
    }


    void Assembly::GetAllAssemblies(AssemblyVector& assemblies)
    {
        os::FastAutoLock lock(&s_assemblyLock);
        if (s_assemblyVersion != s_snapshotAssemblyVersion)
        {
            CopyLogicalAssemblies(assemblies, s_Assemblies);
        }
        else
        {
            assemblies = *s_snapshotAssemblies;
        }
    }

    const Il2CppAssembly* Assembly::GetLoadedAssembly(const char* name)
    {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        if (const Il2CppAssembly* shadow = AssemblyShadow::ResolveByName(name, AssemblyResolveContext::Normal))
            return shadow;
        const Il2CppAssembly* physical = GetLoadedAssemblyPhysical(name);
        return AssemblyShadow::ResolveAssembly(physical);
#else
        os::FastAutoLock lock(&s_assemblyLock);
        AssemblyVector& assemblies = s_Assemblies;
        for (AssemblyVector::const_reverse_iterator assembly = assemblies.rbegin(); assembly != assemblies.rend(); ++assembly)
        {
            if (strcmp((*assembly)->aname.name, name) == 0)
                return *assembly;
        }

        return NULL;
#endif
    }

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    const Il2CppAssembly* Assembly::GetLoadedAssemblyPhysical(const char* name, PhysicalAssemblyPreference preference)
    {
        os::FastAutoLock lock(&s_assemblyLock);
        const Il2CppAssembly* result = nullptr;
        for (const Il2CppAssembly* assembly : s_Assemblies)
        {
            bool interpreter = hybridclr::metadata::IsInterpreterImage(assembly->image);
            if ((preference == PhysicalAssemblyPreference::AotOnly && interpreter) ||
                (preference == PhysicalAssemblyPreference::InterpreterOnly && !interpreter)) continue;
            if (!assembly_shadow_detail::NameEquals(assembly_shadow_detail::ViewName(name),
                assembly_shadow_detail::ViewName(assembly->aname.name))) continue;
            result = assembly;
            if (preference != PhysicalAssemblyPreference::AnyNewest) break;
        }
        return result;
    }

    void Assembly::GetAllPhysicalAssemblies(AssemblyVector& assemblies)
    {
        os::FastAutoLock lock(&s_assemblyLock);
        CopyValidAssemblies(assemblies, s_Assemblies);
    }
#endif

    Il2CppImage* Assembly::GetImage(const Il2CppAssembly* assembly)
    {
        return assembly->image;
    }

    void Assembly::GetReferencedAssemblies(const Il2CppAssembly* assembly, AssemblyNameVector* target)
    {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        assembly = AssemblyShadow::ResolveAssembly(assembly);
        if (AssemblyShadow::IsActiveShadow(assembly))
        {
            hybridclr::metadata::MetadataModule::GetImage(assembly->image)->GetDeclaredReferencedAssemblyNames(*target);
            return;
        }
#endif
        for (int32_t sourceIndex = 0; sourceIndex < assembly->referencedAssemblyCount; sourceIndex++)
        {
            const Il2CppAssembly* refAssembly = MetadataCache::GetReferencedAssembly(assembly, sourceIndex);

            target->push_back(&refAssembly->aname);
        }
    }

    static bool ends_with(const char *str, const char *suffix)
    {
        if (!str || !suffix)
            return false;

        const size_t lenstr = strlen(str);
        const size_t lensuffix = strlen(suffix);
        if (lensuffix >  lenstr)
            return false;

        return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
    }

    const Il2CppAssembly* Assembly::Load(const char* name)
    {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        // Native type-name resolution also calls Load while initializing
        // private metadata. Preserve its strict TLS context; ordinary callers
        // have Normal context and never observe the private closure.
        AssemblyResolveContext context = AssemblyShadow::CurrentResolveContext();
        if (const Il2CppAssembly* shadow = AssemblyShadow::ResolveByName(name, context))
            return shadow;
        const Il2CppAssembly* loaded = LoadOriginal(name);
        if (const Il2CppAssembly* shadow = AssemblyShadow::ResolveByName(name, context))
            return shadow;
        return AssemblyShadow::ResolveAssembly(loaded);
    }

    const Il2CppAssembly* Assembly::LoadOriginal(const char* name)
    {
        const Il2CppAssembly* (*lookup)(const char*) = MetadataCache::GetAssemblyByNameOriginal;
#else
        const Il2CppAssembly* (*lookup)(const char*) = MetadataCache::GetAssemblyByName;
#endif
        const Il2CppAssembly* loadedAssembly = lookup(name);
        if (loadedAssembly)
        {
            return loadedAssembly;
        }

        if (!ends_with(name, ".dll") && !ends_with(name, ".exe"))
        {
            const size_t len = strlen(name);
            char *tmp = new char[len + 5];

            memset(tmp, 0, len + 5);

            memcpy(tmp, name, len);
            memcpy(tmp + len, ".dll", 4);

            loadedAssembly = lookup(tmp);

            if (!loadedAssembly)
            {
                memcpy(tmp + len, ".exe", 4);
                loadedAssembly = lookup(tmp);
            }

            delete[] tmp;

            return loadedAssembly;
        }
        else
        {
            return nullptr;
        }
    }

    void Assembly::Register(const Il2CppAssembly* assembly)
    {
        os::FastAutoLock lock(&s_assemblyLock);

        s_Assemblies.push_back(assembly);
        ++s_assemblyVersion;
    }

    void Assembly::InvalidateAssemblyList()
    {
        os::FastAutoLock lock(&s_assemblyLock);

        ++s_assemblyVersion;
    }

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    bool Assembly::PublishShadowBatch(const AssemblyVector& assemblies,
        bool (*tryBegin)(void*), void (*publish)(void*), void* context)
    {
        os::FastAutoLock lock(&s_assemblyLock);
        s_Assemblies.reserve(s_Assemblies.size() + assemblies.size());
        if (!tryBegin(context)) return false;
        // From here to publish there are only reserved pointer appends/stores.
        // A reader cannot observe half a batch or a batch with the old mapping.
        for (const Il2CppAssembly* assembly : assemblies) s_Assemblies.push_back(assembly);
        publish(context);
        ++s_assemblyVersion;
        return true;
    }

    uint64_t Assembly::CaptureShadowEnumeration(AssemblyVector& assemblies)
    {
        os::FastAutoLock lock(&s_assemblyLock);
        CopyValidAssemblies(assemblies, s_Assemblies);
        return AssemblyShadow::ActiveGeneration();
    }
#endif

    void Assembly::ClearAllAssemblies()
    {
        os::FastAutoLock lock(&s_assemblyLock);
        s_Assemblies.clear();
        delete s_snapshotAssemblies;
        s_snapshotAssemblies = &s_Assemblies;
        s_assemblyVersion = s_snapshotAssemblyVersion = 0;
    }

    void Assembly::Initialize()
    {
    }
} /* namespace vm */
} /* namespace il2cpp */
