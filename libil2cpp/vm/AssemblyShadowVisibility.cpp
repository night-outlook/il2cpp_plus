#include "AssemblyShadowVisibility.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadow.h"
#include "GlobalMetadataFileInternals.h"
#include "MetadataLock.h"
#include "il2cpp-api.h"
#include "il2cpp-class-internals.h"
#include "os/Mutex.h"
#include "hybridclr/metadata/MetadataUtil.h"
#include "hybridclr/metadata/InterpreterMetadataIndexRuntime.h"

#include <atomic>

namespace il2cpp { namespace vm {
namespace
{
    struct PrivateImage
    {
        const Il2CppImage* image;
        uint32_t index;
        const PrivateImage* previous;
    };

    // Single transaction-owned writer; readers need no VM or transaction lock.
    // Nodes and their images live for the process, including after Abort/failure.
    std::atomic<const PrivateImage*> s_privateImages{ nullptr };

    struct Inspection
    {
        const PrivateImage* images;
        uint64_t generation;
        bool includeActive;

        bool Matches(const PrivateImage* entry) const
        {
            // M03 permits exactly one active publication. A generation-zero
            // enumeration must not become visible halfway through its callbacks.
            return includeActive || generation == 0 ||
                !AssemblyShadow::IsActiveShadow(entry->image->assembly);
        }

        bool Contains(const Il2CppImage* image) const
        {
            for (const PrivateImage* entry = images; entry; entry = entry->previous)
                if (entry->image == image) return Matches(entry);
            return false;
        }

        bool ContainsEncodedIndex(int32_t index) const
        {
            for (const PrivateImage* entry = images; entry; entry = entry->previous)
                if (hybridclr::metadata::InterpreterMetadataIndexRuntime::TokenBelongsToImageForVisibility(
                    index, entry->index)) return Matches(entry);
            return false;
        }
    };

    // A stack-only path set also handles recursive/cyclic metadata defensively.
    // Do not traverse cached_class: it points back to the identity being tested.
    enum class NodeKind { Class, Type, GenericClass };
    struct Path
    {
        const void* value;
        NodeKind kind;
        const Path* previous;

        static bool Contains(const Path* path, const void* value, NodeKind kind)
        {
            for (; path; path = path->previous)
                if (path->value == value && path->kind == kind) return true;
            return false;
        }
    };

    bool UsesPrivateType(const Il2CppType* type, const Inspection& inspection, const Path* path);

    bool UsesPrivateInstantiation(const Il2CppGenericInst* inst, const Inspection& inspection, const Path* path)
    {
        if (inst)
            for (uint32_t i = 0; i < inst->type_argc; ++i)
                if (UsesPrivateType(inst->type_argv[i], inspection, path)) return true;
        return false;
    }

    bool UsesPrivateGenericClass(const Il2CppGenericClass* genericClass, const Inspection& inspection, const Path* path)
    {
        if (!genericClass || Path::Contains(path, genericClass, NodeKind::GenericClass)) return false;
        const Path current{ genericClass, NodeKind::GenericClass, path };
        return UsesPrivateType(genericClass->type, inspection, &current) ||
            UsesPrivateInstantiation(genericClass->context.class_inst, inspection, &current) ||
            UsesPrivateInstantiation(genericClass->context.method_inst, inspection, &current);
    }

    bool UsesPrivateType(const Il2CppType* type, const Inspection& inspection, const Path* path)
    {
        if (!type || Path::Contains(path, type, NodeKind::Type)) return false;
        const Path current{ type, NodeKind::Type, path };
        switch (type->type)
        {
            case IL2CPP_TYPE_CLASS:
            case IL2CPP_TYPE_VALUETYPE:
            {
                // Runtime handles point directly at immutable type definitions;
                // decoding their index never resolves the private image globally.
                const auto* definition = reinterpret_cast<const Il2CppTypeDefinition*>(type->data.typeHandle);
                return definition && inspection.ContainsEncodedIndex(definition->byvalTypeIndex);
            }
            case IL2CPP_TYPE_VAR:
            case IL2CPP_TYPE_MVAR:
            {
                const auto* parameter = reinterpret_cast<const Il2CppGenericParameter*>(type->data.genericParameterHandle);
                return parameter && inspection.ContainsEncodedIndex(parameter->ownerIndex);
            }
            case IL2CPP_TYPE_GENERICINST:
                return UsesPrivateGenericClass(type->data.generic_class, inspection, &current);
            case IL2CPP_TYPE_ARRAY:
                return type->data.array && UsesPrivateType(type->data.array->etype, inspection, &current);
            case IL2CPP_TYPE_SZARRAY:
            case IL2CPP_TYPE_PTR:
            case IL2CPP_TYPE_BYREF:
                return UsesPrivateType(type->data.type, inspection, &current);
            default:
                // The byref flag does not change the underlying type encoding.
                return false;
        }
    }

    bool UsesPrivateClass(const Il2CppClass* klass, const Inspection& inspection, const Path* path)
    {
        if (!klass || Path::Contains(path, klass, NodeKind::Class)) return false;
        const Path current{ klass, NodeKind::Class, path };
        return inspection.Contains(klass->image) ||
            UsesPrivateType(&klass->byval_arg, inspection, &current) ||
            UsesPrivateGenericClass(klass->generic_class, inspection, &current) ||
            UsesPrivateClass(klass->element_class, inspection, &current) ||
            UsesPrivateClass(klass->declaringType, inspection, &current);
    }

    void CollectClass(Il2CppClass* klass, void* context)
    {
        static_cast<std::vector<Il2CppClass*>*>(context)->push_back(klass);
    }
}

bool AssemblyShadowVisibility::RegisterPrivateImage(const Il2CppImage* image, uint32_t index)
{
    // The transaction supplies its retained InterpreterImage identity. A token
    // can confirm provenance, but cannot authorize private metadata decoding.
    if (!image || !image->assembly || !hybridclr::metadata::IsInterpreterImage(image) ||
        !hybridclr::metadata::InterpreterMetadataIndexRuntime::TokenBelongsToImageForVisibility(image->token, index))
        return false;
    const PrivateImage* previous = s_privateImages.load(std::memory_order_acquire);
    for (const PrivateImage* entry = previous; entry; entry = entry->previous)
    {
        if (entry->image == image) return entry->index == index;
        if (entry->index == index) return false;
    }
    const PrivateImage* entry = new PrivateImage{ image, index, previous };
    s_privateImages.store(entry, std::memory_order_release);
    return true;
}

bool AssemblyShadowVisibility::IsClassVisible(const Il2CppClass* klass)
{
    return IsClassVisible(klass, AssemblyShadow::ActiveGeneration());
}

bool AssemblyShadowVisibility::IsClassVisible(const Il2CppClass* klass, uint64_t generation)
{
    const Inspection inspection{ s_privateImages.load(std::memory_order_acquire), generation, false };
    return klass && (!inspection.images || !UsesPrivateClass(klass, inspection, nullptr));
}

bool AssemblyShadowVisibility::IsTypeVisible(const Il2CppType* type, uint64_t generation)
{
    const Inspection inspection{ s_privateImages.load(std::memory_order_acquire), generation, false };
    return type && (!inspection.images || !UsesPrivateType(type, inspection, nullptr));
}

bool AssemblyShadowVisibility::ClassUsesStagedMetadata(const Il2CppClass* klass)
{
    const Inspection inspection{ s_privateImages.load(std::memory_order_acquire), 0, true };
    return inspection.images && UsesPrivateClass(klass, inspection, nullptr);
}

void AssemblyShadowVisibility::CollectOrdinaryClasses(std::vector<Il2CppClass*>& classes, uint64_t& generation)
{
    os::FastAutoLock lock(&g_MetadataLock);
    classes.clear();
    generation = AssemblyShadow::ActiveGeneration();
    il2cpp_class_for_each(CollectClass, &classes);
}

}}
#endif
