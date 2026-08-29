#include "AssemblyShadowTypeKey.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadow.h"
#include "AssemblyShadowName.h"
#include "vm/Class.h"
#include "vm/MetadataCache.h"
#include "vm/GlobalMetadata.h"
#include "vm/GlobalMetadataFileInternals.h"
#include "vm/Method.h"
#include "utils/StringUtils.h"
#include <algorithm>

namespace il2cpp { namespace vm {
namespace {
thread_local uint32_t s_physicalTypeMetadataDepth = 0;
const uint32_t kMaximumTypeDepth = 128;

std::string Part(const std::string& value) { return std::to_string(value.size()) + ":" + value; }

std::string CanonicalAssembly(const char* input)
{
    using namespace assembly_shadow_detail;
    NameView view = ViewName(input);
    size_t hash;
    if (!NameHash(view, hash)) throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowInvalidTypeKey AssemblyName");
    UTF16String folded;
    const char* at = view.begin;
    while (at != view.end)
    {
        uint16_t first, second;
        if (!NextFolded(at, view.end, first, second))
            throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowInvalidTypeKey AssemblyEncoding");
        folded.push_back(first);
        if (second) folded.push_back(second);
    }
    return utils::StringUtils::Utf16ToUtf8(folded);
}

ShadowTypeKey MetadataDefinitionKey(Il2CppMetadataTypeHandle handle, const AssemblyShadowTypeKey::MetadataTypeImages& images)
{
    auto owner = images.find(handle);
    if (owner == images.end() || !owner->second || !owner->second->assembly)
        throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnobservedMetadataTypeOwner");
    ShadowTypeKey key;
    key.assemblyName = CanonicalAssembly(owner->second->assembly->aname.name);
    // These are physical metadata indices, read with their original meaning.
    // No class conversion, semantic resolution, or cache population occurs.
    for (uint32_t depth = 0; handle; ++depth)
    {
        if (depth == kMaximumTypeDepth)
            throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowInvalidMetadataDeclarationChain");
        auto declarationOwner = images.find(handle);
        if (declarationOwner == images.end() || declarationOwner->second != owner->second)
            throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowInvalidMetadataDeclarationOwner");
        const Il2CppTypeDefinition* definition = reinterpret_cast<const Il2CppTypeDefinition*>(handle);
        auto name = GlobalMetadata::GetTypeNamespaceAndName(handle);
        auto container = GlobalMetadata::GetGenericContainerFromIndex(definition->genericContainerIndex);
        key.declarations.push_back({name.first ? name.first : "", name.second ? name.second : "",
            GlobalMetadata::GetGenericContainerCount(container)});
        if (definition->declaringTypeIndex == kTypeIndexInvalid) break;
        const Il2CppType* declaring = GlobalMetadata::GetIl2CppTypeFromIndex(definition->declaringTypeIndex);
        if (!declaring || (declaring->type != IL2CPP_TYPE_CLASS && declaring->type != IL2CPP_TYPE_VALUETYPE))
            throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedMetadataDeclaringType");
        handle = declaring->data.typeHandle;
    }
    std::reverse(key.declarations.begin(), key.declarations.end());
    return key;
}

std::string FormatType(const Il2CppType* type, uint32_t depth, const MethodInfo* localMethod = nullptr,
    const AssemblyShadowTypeKey::MetadataTypeImages* images = nullptr)
{
    if (!type || depth > kMaximumTypeDepth)
        throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedTypeShape NullOrRecursiveType");
    std::string value;
    switch (type->type)
    {
        case IL2CPP_TYPE_CLASS:
        case IL2CPP_TYPE_VALUETYPE:
            value = images ? MetadataDefinitionKey(type->data.typeHandle, *images).ToString() :
                AssemblyShadowTypeKey::Make(AssemblyShadowTypeKey::RawDefinition(type)).ToString();
            break;
        case IL2CPP_TYPE_GENERICINST:
        {
            const Il2CppGenericClass* generic = type->data.generic_class;
            if (!generic || !generic->context.class_inst || generic->context.method_inst)
                throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedTypeShape GenericContext");
            value = "generic(" + FormatType(generic->type, depth + 1, localMethod, images);
            for (uint32_t index = 0; index < generic->context.class_inst->type_argc; ++index)
                value += "," + Part(FormatType(generic->context.class_inst->type_argv[index], depth + 1, localMethod, images));
            value += ")";
            break;
        }
        case IL2CPP_TYPE_SZARRAY:
        case IL2CPP_TYPE_PTR:
            value = std::string(type->type == IL2CPP_TYPE_PTR ? "ptr(" : "szarray(") + FormatType(type->data.type, depth + 1, localMethod, images) + ")";
            break;
        case IL2CPP_TYPE_ARRAY:
        {
            const Il2CppArrayType* array = type->data.array;
            if (!array || !array->rank || (array->numsizes && !array->sizes) || (array->numlobounds && !array->lobounds))
                throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedTypeShape Array");
            value = "array(" + std::to_string(array->rank) + "," + Part(FormatType(array->etype, depth + 1, localMethod, images)) + ",sizes=";
            for (uint32_t index = 0; index < array->numsizes; ++index) value += std::to_string(array->sizes[index]) + ",";
            value += "bounds=";
            for (uint32_t index = 0; index < array->numlobounds; ++index) value += std::to_string(array->lobounds[index]) + ",";
            value += ")";
            break;
        }
        case IL2CPP_TYPE_VAR:
        case IL2CPP_TYPE_MVAR:
        {
            if (images)
                throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowOpenExecutionClassTypeKeyUnsupported");
            if (!type->data.genericParameterHandle)
                throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedTypeShape GenericParameter");
            Il2CppGenericParameterInfo parameter = MetadataCache::GetGenericParameterInfo(type->data.genericParameterHandle);
            const MethodInfo* method = type->type == IL2CPP_TYPE_MVAR ?
                MetadataCache::GetParameterDeclaringMethod(type->data.genericParameterHandle) : nullptr;
            if (method && method == localMethod)
            {
                value = "!!" + std::to_string(parameter.num);
                break;
            }
            // The runtime handle is preserved by ResolveType. A stable key uses
            // the declaring type or complete method signature, never its token.
            Il2CppClass* owner = MetadataCache::GetParameterDeclaringType(type->data.genericParameterHandle);
            if (!owner)
                throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedTypeShape GenericParameterOwner");
            value = std::string(type->type == IL2CPP_TYPE_VAR ? "var(" : "mvar(") + AssemblyShadowTypeKey::Make(owner).ToString() + "," + std::to_string(parameter.num);
            if (type->type == IL2CPP_TYPE_MVAR)
            {
                if (!method) throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedTypeShape MethodParameterOwner");
                value += "," + Part(method->name) + ",arity=" + std::to_string(MetadataCache::GetGenericContainerCount(method->genericContainerHandle));
                value += ",static=" + std::to_string((method->flags & METHOD_ATTRIBUTE_STATIC) != 0);
                value += ",return=" + Part(FormatType(method->return_type, depth + 1, method));
                // Carry the declaring-method context through composite argument
                // shapes too: List<!!0[]> must not recursively expand its owner.
                for (uint32_t index = 0; index < method->parameters_count; ++index)
                    value += "," + Part(FormatType(method->parameters[index], depth + 1, method));
            }
            value += ")";
            break;
        }
        default:
        {
            Il2CppClass* primitive = Class::FromIl2CppTypeEnum(type->type);
            if (!primitive) throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed,
                "ShadowUnsupportedTypeShape TypeCode=" + std::to_string(type->type));
            value = AssemblyShadowTypeKey::Make(primitive).ToString();
            break;
        }
    }
    // These are the qualifiers represented by the pinned Il2CppType ABI.
    if (type->attrs || type->num_mods || type->byref || type->pinned)
        value = "qualified(" + std::to_string(type->attrs) + "," + std::to_string(type->num_mods) + "," +
            std::to_string(type->byref) + "," + std::to_string(type->pinned) + "," + value + ")";
    return value;
}
}

AssemblyShadowTypeMetadataScope::AssemblyShadowTypeMetadataScope() { ++s_physicalTypeMetadataDepth; }
AssemblyShadowTypeMetadataScope::~AssemblyShadowTypeMetadataScope() { --s_physicalTypeMetadataDepth; }
bool AssemblyShadow::IsResolvingTypeMetadata() { return s_physicalTypeMetadataDepth != 0; }

std::string ShadowTypeKey::ToString() const
{
    std::string key = "type(" + Part(assemblyName);
    for (const auto& declaration : declarations)
        key += "/" + Part(declaration.namespaze) + "/" + Part(declaration.name) + "@" + std::to_string(declaration.genericArity);
    return key + ")";
}

bool ShadowTypeKey::operator==(const ShadowTypeKey& other) const
{
    if (assemblyName != other.assemblyName || declarations.size() != other.declarations.size()) return false;
    for (size_t index = 0; index < declarations.size(); ++index)
        if (declarations[index].namespaze != other.declarations[index].namespaze || declarations[index].name != other.declarations[index].name ||
            declarations[index].genericArity != other.declarations[index].genericArity) return false;
    return true;
}

uint32_t AssemblyShadowTypeKey::GenericArity(const Il2CppClass* definition)
{
    return definition && definition->genericContainerHandle ? MetadataCache::GetGenericContainerCount(definition->genericContainerHandle) : 0;
}

Il2CppClass* AssemblyShadowTypeKey::RawDefinition(const Il2CppType* type)
{
    AssemblyShadowTypeMetadataScope scope;
    for (uint32_t depth = 0; type && depth < kMaximumTypeDepth; ++depth)
    {
        if (type->type == IL2CPP_TYPE_CLASS || type->type == IL2CPP_TYPE_VALUETYPE)
            return type->data.typeHandle ? MetadataCache::GetTypeInfoFromHandle(type->data.typeHandle) : nullptr;
        if (type->type != IL2CPP_TYPE_GENERICINST || !type->data.generic_class) return Class::FromIl2CppTypeEnum(type->type);
        type = type->data.generic_class->type;
    }
    throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowUnsupportedTypeShape RecursiveDefinition");
}

ShadowTypeKey AssemblyShadowTypeKey::Make(const Il2CppClass* definition)
{
    AssemblyShadowTypeMetadataScope scope;
    if (!definition || !definition->image || !definition->image->assembly || definition->rank || definition->generic_class)
        throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowInvalidDefinitionKey");
    ShadowTypeKey key;
    key.assemblyName = CanonicalAssembly(definition->image->assembly->aname.name);
    for (const Il2CppClass* at = definition; at; at = at->declaringType)
    {
        if (key.declarations.size() >= kMaximumTypeDepth || at->image != definition->image || at->generic_class)
            throw ShadowTypeResolutionFailure(AssemblyShadowError::ReferenceResolutionFailed, "ShadowInvalidDeclarationChain");
        key.declarations.push_back({at->namespaze ? at->namespaze : "", at->name ? at->name : "", GenericArity(at)});
    }
    std::reverse(key.declarations.begin(), key.declarations.end());
    return key;
}

std::string AssemblyShadowTypeKey::Format(const Il2CppType* type)
{
    AssemblyShadowTypeMetadataScope scope;
    return FormatType(type, 0);
}

std::string AssemblyShadowTypeKey::FormatMetadataOnly(const Il2CppType* type, const MetadataTypeImages& images)
{
    return FormatType(type, 0, nullptr, &images);
}
}}
#endif
