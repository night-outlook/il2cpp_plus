#pragma once
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadowTypeKey.h"
#include "vm/MetadataCache.h"
#include "il2cpp-tabledefs.h"
#include <algorithm>
#include <string>
#include <vector>

namespace il2cpp { namespace vm { namespace assembly_shadow_evolution {

inline void MethodFailure(const char* reason, const std::string& detail = std::string())
{
    throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed,
        std::string(reason) + " " + detail);
}

inline std::string MethodPart(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

// The pinned MethodInfo ABI does not preserve full custom-modifier identities
// or function-pointer calling conventions. Reject those shapes instead of
// treating equal modifier counts as ABI proof. The Editor encoder retains the
// complete CLI signature; this native path supports its representable subset.
inline std::string MethodTypeShape(const Il2CppType* type, const MethodInfo* owner,
    uint32_t depth = 0)
{
    if (!type || depth >= 128) MethodFailure("ShadowInvalidMethodSignature", "NullOrRecursiveType");
    if (type->num_mods || type->pinned)
        MethodFailure("ShadowMethodSignatureUnsupported", "CustomModifierOrPinned");
    std::string key = "t" + std::to_string(type->type) + ":v" +
        std::to_string(type->valuetype) + ":r" + std::to_string(type->byref) + ":";
    switch (type->type)
    {
        case IL2CPP_TYPE_MVAR:
        {
            if (!owner || !owner->is_generic || !type->data.genericParameterHandle ||
                MetadataCache::GetParameterDeclaringMethod(type->data.genericParameterHandle) != owner)
                MethodFailure("ShadowInvalidMethodSignature", "ForeignMethodGenericParameter");
            auto parameter = MetadataCache::GetGenericParameterInfo(type->data.genericParameterHandle);
            if (parameter.num >= MetadataCache::GetGenericContainerCount(owner->genericContainerHandle))
                MethodFailure("ShadowInvalidMethodSignature", "MethodGenericParameterOrdinal");
            return key + "!!" + std::to_string(parameter.num);
        }
        case IL2CPP_TYPE_GENERICINST:
        {
            const Il2CppGenericClass* generic = type->data.generic_class;
            if (!generic || !generic->type || !generic->context.class_inst || generic->context.method_inst)
                MethodFailure("ShadowInvalidMethodSignature", "GenericContext");
            const auto* arguments = generic->context.class_inst;
            if (!arguments->type_argc || !arguments->type_argv)
                MethodFailure("ShadowInvalidMethodSignature", "GenericArguments");
            key += MethodPart(MethodTypeShape(generic->type, owner, depth + 1));
            key += ":" + std::to_string(arguments->type_argc);
            for (uint32_t index = 0; index < arguments->type_argc; ++index)
                key += MethodPart(MethodTypeShape(arguments->type_argv[index], owner, depth + 1));
            return key;
        }
        case IL2CPP_TYPE_PTR:
        case IL2CPP_TYPE_SZARRAY:
            return key + MethodPart(MethodTypeShape(type->data.type, owner, depth + 1));
        case IL2CPP_TYPE_ARRAY:
        {
            const auto* array = type->data.array;
            if (!array || !array->rank || (array->numsizes && !array->sizes) ||
                (array->numlobounds && !array->lobounds))
                MethodFailure("ShadowInvalidMethodSignature", "ArrayShape");
            key += MethodPart(MethodTypeShape(array->etype, owner, depth + 1)) +
                ":rank=" + std::to_string(array->rank) + ":sizes=" + std::to_string(array->numsizes);
            for (uint32_t index = 0; index < array->numsizes; ++index) key += ":" + std::to_string(array->sizes[index]);
            key += ":bounds=" + std::to_string(array->numlobounds);
            for (uint32_t index = 0; index < array->numlobounds; ++index) key += ":" + std::to_string(array->lobounds[index]);
            return key;
        }
        case IL2CPP_TYPE_FNPTR:
            MethodFailure("ShadowMethodSignatureUnsupported", "FunctionPointerCallingConvention");
            return std::string();
        default:
        {
            // Parameter/field attributes are not type identity. Their effective
            // metadata is compared separately before a remapping is cached.
            Il2CppType physical = *type;
            physical.attrs = 0;
            return key + MethodPart(AssemblyShadowTypeKey::Format(&physical));
        }
    }
}

inline std::string GenericContract(Il2CppMetadataGenericContainerHandle container,
    const MethodInfo* method = nullptr)
{
    if (!container) return "0";
    const uint32_t count = MetadataCache::GetGenericContainerCount(container);
    if (count > UINT16_MAX) MethodFailure("ShadowGenericContractUnsupported", "GenericParameterCount");
    std::string key = std::to_string(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        // Container-local parameter and parameter-local constraint ordinals.
        auto parameter = MetadataCache::GetGenericParameterFromIndex(container, index);
        if (!parameter) MethodFailure("ShadowGenericContractUnsupported", "MissingParameter");
        auto info = MetadataCache::GetGenericParameterInfo(parameter);
        if (info.num != index) MethodFailure("ShadowGenericContractUnsupported", "ParameterOrdinal");
        const int16_t constraintCount = MetadataCache::GetGenericConstraintCount(parameter);
        if (constraintCount < 0) MethodFailure("ShadowGenericContractUnsupported", "ConstraintCount");
        key += ":" + std::to_string(MetadataCache::GetGenericParameterFlags(parameter));
        std::vector<std::string> constraints;
        constraints.reserve(static_cast<size_t>(constraintCount));
        for (int32_t ordinal = 0; ordinal < constraintCount; ++ordinal)
            constraints.push_back(MethodTypeShape(MetadataCache::GetGenericParameterConstraintFromIndex(parameter, ordinal), method));
        std::sort(constraints.begin(), constraints.end());
        key += ":" + std::to_string(constraints.size());
        for (const auto& constraint : constraints) key += MethodPart(constraint);
    }
    return key;
}

inline std::string DeclarationContracts(const Il2CppClass* klass)
{
    std::string key;
    for (uint32_t depth = 0; klass; klass = klass->declaringType, ++depth)
    {
        if (depth >= 128) MethodFailure("ShadowGenericContractUnsupported", "DeclarationDepth");
        key += MethodPart(GenericContract(klass->genericContainerHandle));
    }
    return key;
}

inline std::string LogicalMethodKey(const MethodInfo* method)
{
    if (!method || !method->klass || !method->name || !method->return_type ||
        (method->parameters_count && !method->parameters) || method->is_inflated)
        MethodFailure("ShadowInvalidMethodSignature", "MalformedDefinition");
    const uint32_t arity = method->is_generic ?
        MetadataCache::GetGenericContainerCount(method->genericContainerHandle) : 0;
    if (method->is_generic && !arity) MethodFailure("ShadowInvalidMethodSignature", "MissingGenericContainer");
    std::string key = "LogicalMethodKeyV1:" + MethodPart(AssemblyShadowTypeKey::Make(method->klass).ToString()) +
        MethodPart(method->name) + ":managed-default:" +
        std::to_string((method->flags & METHOD_ATTRIBUTE_STATIC) != 0) + ":" +
        std::to_string(arity) + ":" + std::to_string(method->parameters_count) +
        MethodPart(MethodTypeShape(method->return_type, method));
    for (uint32_t index = 0; index < method->parameters_count; ++index)
        key += MethodPart(MethodTypeShape(method->parameters[index], method));
    // No token, slot, pointer, visibility or implementation optimization flag.
    return key;
}

inline void RequireMethodCompatibility(const MethodInfo* baseline, const MethodInfo* target)
{
    if (LogicalMethodKey(baseline) != LogicalMethodKey(target))
        MethodFailure("ShadowMethodCompatibilityMismatch", "LogicalSignature");
    // ECMA MethodImpl flags: NoInlining, NoOptimization, AggressiveInlining and
    // AggressiveOptimization are implementation hints, not invocation identity.
    const uint16_t optimizationHints = 0x0348;
    bool compatible = baseline->flags == target->flags &&
        (baseline->iflags & ~optimizationHints) == (target->iflags & ~optimizationHints) &&
        baseline->return_type->attrs == target->return_type->attrs &&
        GenericContract(baseline->is_generic ? baseline->genericContainerHandle : nullptr, baseline) ==
            GenericContract(target->is_generic ? target->genericContainerHandle : nullptr, target) &&
        DeclarationContracts(baseline->klass) == DeclarationContracts(target->klass);
    for (uint32_t index = 0; compatible && index < baseline->parameters_count; ++index)
        compatible = baseline->parameters[index]->attrs == target->parameters[index]->attrs;
    if (!compatible)
        MethodFailure("ShadowMethodCompatibilityMismatch", LogicalMethodKey(baseline));
    // This approves a reflection description only. The physical execution guard
    // must still reject the old AOT MethodInfo and must never call this remapper.
}

}}}
#endif
