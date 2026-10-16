#pragma once

#include "il2cpp-config.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include <stdint.h>
#include <vector>

struct Il2CppImage;
struct Il2CppClass;
struct Il2CppType;

namespace il2cpp { namespace vm {
namespace assembly_shadow_bounded_classes { class Capture; }

class AssemblyShadowVisibility
{
public:
    // Called by the transaction owner after skeleton creation, before any
    // runtime metadata can reach a shared class/type cache. Retained on Abort.
    // imageId comes from the transaction-owned InterpreterImage, not its token.
    // False rejects mismatched or rebound identity without registering it.
    static bool RegisterPrivateImage(const Il2CppImage* image, uint32_t imageId);

    // Raw metadata inspection only: no allocation, locks, lazy materialization,
    // or private-image lookup through MetadataModule. The generation overloads
    // keep one public enumeration on its captured activation snapshot.
    static bool IsClassVisible(const Il2CppClass* klass);
    static bool IsClassVisible(const Il2CppClass* klass, uint64_t generation);
    static bool IsTypeVisible(const Il2CppType* type, uint64_t generation);
    static bool ClassUsesStagedMetadata(const Il2CppClass* klass);

    // Calls the actual public API without creating managed reflection handles.
    static void CollectOrdinaryClasses(std::vector<Il2CppClass*>& classes, uint64_t& generation);
    // P1 additive, inactive. Calls the same physical class iterator while
    // g_MetadataLock is held and deep-copies V1 diagnostic names and flags.
    // Caller must independently guarantee metadata shutdown quiescence.
    // The original class-pointer vector enumerator remains unchanged.
    static bool TryCaptureBoundedOrdinaryClasses(
        assembly_shadow_bounded_classes::Capture& capture);

};

}}
#endif
