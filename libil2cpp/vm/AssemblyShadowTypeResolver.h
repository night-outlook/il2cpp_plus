#pragma once
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadowTypeKey.h"

namespace il2cpp { namespace vm {
class AssemblyShadowTypeResolver
{
public:
    // Internal methods report structured native failures, without sealing.
    // Public semantic callers seal; the observational diagnostic API does not.
    static Il2CppClass* ResolveDefinition(Il2CppClass* klass);
    static Il2CppClass* ResolveClass(Il2CppClass* klass);
    static const Il2CppType* Resolve(const Il2CppType* type);
    // Reflection may observe physical AOT stack-frame metadata after a shadow
    // commit. Canonicalize that metadata structurally without weakening the
    // separate managed-execution guard, which must never remap a MethodInfo.
    static const MethodInfo* ResolveReflectionMethod(const MethodInfo* method);
    static Il2CppClass* ResolveAllocation(Il2CppClass* klass, const char* site);
    static void RecordUse(const Il2CppType* type, BaselineUseKind kind, const char* site);
    static bool ContainsBaseline(const Il2CppType* type);
    static AssemblyShadowError GetInfo(const Il2CppType* type, std::string& json);
    static void CountGuardFailure();
};
}}
#endif
