#pragma once
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

// Input-only view: evaluating readiness must never initialize a baseline class.
struct PhysicalLayoutState
{
    bool sizeInitialized;
    bool sizeInitializationPending;
    bool hasImage;
    bool hasDefinition;
    bool interpreterImage;
    bool genericInstance;
    bool genericDefinition;
    bool array;
    bool managedDefinition;
    uint32_t instanceSize;
};

// GlobalMetadata::FromTypeDefinition copies AOT definition sizes from compiled
// registration before SetupFields. GetFieldOffset reads that same immutable
// registration. size_inited additionally describes lazy field/static setup;
// requiring it would keep a deliberately untouched baseline unready forever.
// This alternative applies ONLY to concrete AOT definitions. Constructed
// generics, arrays and interpreter layouts still require runtime readiness.
inline bool HasFrozenAotDefinitionLayout(const PhysicalLayoutState& value,
    uint32_t objectHeaderSize) noexcept
{
    return !value.sizeInitializationPending && value.hasImage && value.hasDefinition &&
        !value.interpreterImage && !value.genericInstance && !value.genericDefinition &&
        !value.array && value.managedDefinition && objectHeaderSize != 0 &&
        value.instanceSize >= objectHeaderSize;
}

inline bool BaselineLayoutReady(const PhysicalLayoutState& value,
    uint32_t objectHeaderSize) noexcept
{
    return value.sizeInitialized || HasFrozenAotDefinitionLayout(value, objectHeaderSize);
}

}}}
