#pragma once

#include "il2cpp-config.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include <stdint.h>
#include <vector>

struct Il2CppImage;
struct Il2CppClass;
struct Il2CppType;

namespace il2cpp { namespace vm {

class AssemblyShadowVisibility
{
public:
    // Called by the transaction owner after skeleton creation, before any
    // runtime metadata can reach a shared class/type cache. Retained on Abort.
    static void RegisterPrivateImage(const Il2CppImage* image);

    // Raw metadata inspection only: no allocation, locks, lazy materialization,
    // or private-image lookup through MetadataModule. The generation overloads
    // keep one public enumeration on its captured activation snapshot.
    static bool IsClassVisible(const Il2CppClass* klass);
    static bool IsClassVisible(const Il2CppClass* klass, uint64_t generation);
    static bool IsTypeVisible(const Il2CppType* type, uint64_t generation);
    static bool ClassUsesStagedMetadata(const Il2CppClass* klass);

    // Calls the actual public API without creating managed reflection handles.
    static void CollectOrdinaryClasses(std::vector<Il2CppClass*>& classes, uint64_t& generation);
};

}}
#endif
