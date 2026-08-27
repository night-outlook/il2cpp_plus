#pragma once

#include "il2cpp-config.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include <string>

struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppObject;

namespace il2cpp { namespace vm {
    // Experimental M01 only: one non-generic Internal shadow, no unload or transaction.
    class AssemblyShadowPrototype
    {
    public:
        static bool IsCandidateName(const char* name);
        static bool HasStagedAssembly();
        static bool Stage(const Il2CppAssembly* baseline, const Il2CppAssembly* shadow);
        static bool Activate(const char* name);
        static const Il2CppAssembly* ResolveName(const char* name, const char* site);
        static const Il2CppImage* ResolveImage(const Il2CppImage* image);
        static void TraceImage(const char* site, const Il2CppImage* image);
        static void TraceClass(const char* site, const Il2CppClass* klass);
        static void TraceTypeCheck(const char* site, const Il2CppClass* actual,
            const Il2CppClass* expected, bool checkInterfaces, bool matches);
        static void SetPhase(const char* phase);
        static std::string Diagnostics();
        static std::string InspectObject(const Il2CppObject* object);
        static std::string InspectAssembly(const Il2CppAssembly* assembly);
    };
}}
#endif
