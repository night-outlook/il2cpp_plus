#include "AssemblyShadowTypeResolver.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadow.h"
#include "AssemblyShadowDiagnostics.h"
#include "vm/Class.h"
#include "vm/GenericClass.h"
#include "vm/GlobalMetadata.h"
#include "vm/MetadataCache.h"
#include "metadata/GenericMethod.h"
#include "hybridclr/metadata/AssemblyShadowBridge.h"
#include "hybridclr/metadata/MetadataUtil.h"
#include "il2cpp-tabledefs.h"
#include <atomic>
#include <algorithm>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace il2cpp { namespace vm {
namespace {
const uint32_t kMaximumDepth = 128;
struct OwnedType
{
    Il2CppType type;
    Il2CppArrayType array;
    std::vector<int> sizes;
    std::vector<int> bounds;
};
struct ResolverState
{
    std::mutex mutex;
    std::unordered_map<Il2CppClass*, Il2CppClass*> definitions;
    std::unordered_map<const MethodInfo*, const MethodInfo*> reflectionMethods;
    std::unordered_map<const Il2CppType*, const Il2CppType*> knownBaselines;
    std::unordered_map<std::string, std::unique_ptr<OwnedType>> types;
    std::atomic<uint64_t> hits{0}, misses{0}, rebuilds{0}, allocations{0}, failures{0};
};
ResolverState& State() { static ResolverState* state = new ResolverState(); return *state; }
void Increment(std::atomic<uint64_t>& counter)
{
    uint64_t value = counter.load(std::memory_order_relaxed);
    while (value != UINT64_MAX && !counter.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {}
}
void Fail(const char* label, const std::string& detail, AssemblyShadowError code = AssemblyShadowError::ReferenceResolutionFailed)
{
    throw ShadowTypeResolutionFailure(code, std::string(label) + " " + detail);
}
bool Private() { return hybridclr::metadata::AssemblyShadowBridge::IsStaging(); }
uint32_t Kind(const Il2CppClass* klass)
{
    if (klass->flags & TYPE_ATTRIBUTE_INTERFACE) return 3;
    if (klass->enumtype) return 2;
    if (klass->byval_arg.valuetype) return 1;
    if (klass->parent && (klass->parent == il2cpp_defaults.multicastdelegate_class || klass->parent == il2cpp_defaults.delegate_class)) return 4;
    return 0;
}
void MatchDefinition(const Il2CppClass* baseline, const Il2CppClass* active, const std::string& key)
{
    for (uint32_t depth = 0; baseline || active; ++depth)
    {
        if (!baseline || !active || depth > kMaximumDepth) Fail("ShadowDeclarationChainMismatch", key);
        if (AssemblyShadowTypeKey::GenericArity(baseline) != AssemblyShadowTypeKey::GenericArity(active))
            Fail("ShadowGenericArityMismatch", key);
        if (Kind(baseline) != Kind(active) || baseline->is_byref_like != active->is_byref_like)
            Fail("ShadowTypeKindMismatch", key, AssemblyShadowError::ResourceAbiMismatch);
        baseline = baseline->declaringType;
        active = active->declaringType;
    }
}
Il2CppClass* FindDefinition(const Il2CppImage* image, const ShadowTypeKey& key, bool optional = false)
{
    Il2CppClass* result = nullptr;
    for (size_t depth = 0; depth < key.declarations.size(); ++depth)
    {
        const auto& part = key.declarations[depth];
        Il2CppClass* found = nullptr;
        auto consider = [&](Il2CppMetadataTypeHandle handle) {
            auto name = MetadataCache::GetTypeNamespaceAndName(handle);
            if (part.namespaze != name.first || part.name != name.second) return;
            if (found) Fail("ShadowAmbiguousTypeDefinition", key.ToString());
            found = MetadataCache::GetTypeInfoFromHandle(handle);
        };
        if (depth == 0)
        {
            for (uint32_t index = 0; index < image->typeCount; ++index)
            {
                auto handle = MetadataCache::GetAssemblyTypeHandle(image, index);
                if (!MetadataCache::TypeIsNested(handle)) consider(handle);
            }
        }
        else
        {
            void* iterator = nullptr;
            while (auto handle = MetadataCache::GetNestedTypes(result->typeMetadataHandle, &iterator)) consider(handle);
        }
        if (!found)
        {
            if (optional) return nullptr;
            Fail("ShadowTypeNotFound", key.ToString() + " Declaration=" + part.name);
        }
        if (AssemblyShadowTypeKey::GenericArity(found) != part.genericArity)
            Fail("ShadowGenericArityMismatch", key.ToString() + " Declaration=" + part.name);
        if (found->image != image || (depth ? found->declaringType != result : found->declaringType != nullptr))
            Fail("ShadowDeclarationChainMismatch", key.ToString());
        result = found;
    }
    return result;
}

template<class T> void Binary(std::string& key, const T& value)
{
    key.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
// Ephemeral intern keys may contain actual metadata pointers; persisted
// TypeKey never does. Include every qualifier and array-bound component.
std::string InternKey(const Il2CppType& type)
{
    std::string key;
    uint32_t flags = type.attrs | (type.num_mods << 16) | (type.byref << 21) | (type.pinned << 22) | (type.valuetype << 23);
    Binary(key, type.type); Binary(key, flags);
    if (type.type == IL2CPP_TYPE_ARRAY)
    {
        const Il2CppArrayType& array = *type.data.array;
        Binary(key, array.etype); Binary(key, array.rank); Binary(key, array.numsizes); Binary(key, array.numlobounds);
        for (uint32_t index = 0; index < array.numsizes; ++index) Binary(key, array.sizes[index]);
        for (uint32_t index = 0; index < array.numlobounds; ++index) Binary(key, array.lobounds[index]);
    }
    else Binary(key, type.data.dummy);
    return key;
}
bool SameScalar(const Il2CppType& left, const Il2CppType& right)
{
    return left.type == right.type && left.data.dummy == right.data.dummy && left.attrs == right.attrs &&
        left.num_mods == right.num_mods && left.byref == right.byref && left.pinned == right.pinned && left.valuetype == right.valuetype;
}
const Il2CppType* Intern(const Il2CppType& type, const Il2CppType* canonical = nullptr)
{
    if (canonical && SameScalar(type, *canonical)) return canonical;
    std::string key = InternKey(type);
    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.types.find(key);
        if (found != state.types.end()) return &found->second->type;
    }
    std::unique_ptr<OwnedType> owned(new OwnedType());
    owned->type = type;
    if (type.type == IL2CPP_TYPE_ARRAY)
    {
        owned->array = *type.data.array;
        if (owned->array.numsizes) owned->sizes.assign(owned->array.sizes, owned->array.sizes + owned->array.numsizes);
        if (owned->array.numlobounds) owned->bounds.assign(owned->array.lobounds, owned->array.lobounds + owned->array.numlobounds);
        owned->array.sizes = owned->sizes.empty() ? nullptr : owned->sizes.data();
        owned->array.lobounds = owned->bounds.empty() ? nullptr : owned->bounds.data();
        owned->type.data.array = &owned->array;
    }
    std::lock_guard<std::mutex> lock(state.mutex);
    auto inserted = state.types.emplace(std::move(key), std::move(owned));
    return &inserted.first->second->type;
}
Il2CppClass* RawClass(const Il2CppType* type)
{
    AssemblyShadowTypeMetadataScope scope;
    if (type->type == IL2CPP_TYPE_CLASS || type->type == IL2CPP_TYPE_VALUETYPE) return AssemblyShadowTypeKey::RawDefinition(type);
    return Class::FromIl2CppType(type);
}

std::string MethodSignature(const MethodInfo* method)
{
    if (!method || !method->klass || !method->name || !method->return_type ||
        (method->parameters_count && !method->parameters) || method->is_inflated)
        Fail("ShadowInvalidMethodSignature", "MalformedDefinition");
    std::string key = std::string(method->name) + " flags=" + std::to_string(method->flags) +
        " iflags=" + std::to_string(method->iflags) + " slot=" + std::to_string(method->slot) +
        " generic=" + std::to_string(method->is_generic != 0) + " arity=" +
        std::to_string(method->is_generic ? MetadataCache::GetGenericContainerCount(method->genericContainerHandle) : 0) +
        " parameters=" + std::to_string(method->parameters_count) +
        " return=" + AssemblyShadowTypeKey::Format(method->return_type);
    for (uint16_t index = 0; index < method->parameters_count; ++index)
        key += " parameter=" + AssemblyShadowTypeKey::Format(method->parameters[index]);
    return key;
}

const MethodInfo* FindMethodDefinition(const MethodInfo* baseline, Il2CppClass* activeClass)
{
    const std::string signature = MethodSignature(baseline);
    const MethodInfo* result = nullptr;
    for (uint16_t index = 0; index < activeClass->method_count; ++index)
    {
        Il2CppMetadataMethodInfo raw = MetadataCache::GetMethodInfo(activeClass, index);
        const MethodInfo* candidate = MetadataCache::GetMethodInfoFromMethodHandle(raw.handle);
        if (!candidate || candidate->klass != activeClass) Fail("ShadowInvalidMethodSignature", signature);
        if (MethodSignature(candidate) != signature) continue;
        if (result) Fail("ShadowAmbiguousMethodDefinition", signature);
        result = candidate;
    }
    if (!result) Fail("ShadowMethodNotFound", signature);
    return result;
}

const Il2CppGenericInst* ResolveMethodInstantiation(const Il2CppGenericInst* input)
{
    if (!input) return nullptr;
    if (!input->type_argc || !input->type_argv) Fail("ShadowInvalidMethodInstantiation", "MissingArguments");
    std::vector<const Il2CppType*> arguments;
    arguments.reserve(input->type_argc);
    for (uint32_t index = 0; index < input->type_argc; ++index)
        arguments.push_back(AssemblyShadowTypeResolver::Resolve(input->type_argv[index]));
    AssemblyShadowTypeMetadataScope scope;
    return MetadataCache::GetGenericInst(arguments.data(), static_cast<uint32_t>(arguments.size()));
}
const Il2CppType* Resolve(const Il2CppType* input, uint32_t depth)
{
    if (!input || depth > kMaximumDepth) Fail("ShadowUnsupportedTypeShape", "NullOrRecursiveType");
    Il2CppType output = *input;
    const Il2CppType* canonical = nullptr;
    switch (input->type)
    {
        case IL2CPP_TYPE_CLASS:
        case IL2CPP_TYPE_VALUETYPE:
        {
            Il2CppClass* source = AssemblyShadowTypeKey::RawDefinition(input);
            Il2CppClass* active = AssemblyShadowTypeResolver::ResolveDefinition(source);
            if (active == source) return input;
            output.data = active->byval_arg.data;
            output.type = active->byval_arg.type;
            canonical = input->byref ? &active->this_arg : &active->byval_arg;
            break;
        }
        case IL2CPP_TYPE_GENERICINST:
        {
            const Il2CppGenericClass* generic = input->data.generic_class;
            if (!generic || !generic->context.class_inst || generic->context.method_inst)
                Fail("ShadowUnsupportedTypeShape", "GenericContext");
            const auto* inst = generic->context.class_inst;
            Il2CppClass* source = AssemblyShadowTypeKey::RawDefinition(generic->type);
            if (!source || AssemblyShadowTypeKey::GenericArity(source) != inst->type_argc)
                Fail("ShadowGenericArityMismatch", source ? AssemblyShadowTypeKey::Make(source).ToString() : "MissingDefinition");
            Il2CppClass* active = AssemblyShadowTypeResolver::ResolveDefinition(source);
            std::vector<const Il2CppType*> arguments;
            bool changed = source != active;
            if (changed) arguments.assign(inst->type_argv, inst->type_argv + inst->type_argc);
            for (uint32_t index = 0; index < inst->type_argc; ++index)
            {
                const Il2CppType* argument = Resolve(inst->type_argv[index], depth + 1);
                if (argument != inst->type_argv[index] && !changed)
                {
                    arguments.assign(inst->type_argv, inst->type_argv + inst->type_argc);
                    changed = true;
                }
                if (changed) arguments[index] = argument;
            }
            if (!changed) return input;
            // No resolver cache mutex is held while the upstream metadata
            // interner/inflater recursively constructs the new generic class.
            Il2CppClass* rebuilt;
            {
                AssemblyShadowTypeMetadataScope scope;
                rebuilt = MetadataCache::GetGenericInstanceType(active, arguments.data(), static_cast<uint32_t>(arguments.size()));
            }
            if (!rebuilt) Fail("ShadowGenericInflationFailed", AssemblyShadowTypeKey::Make(active).ToString());
            output.data.generic_class = rebuilt->generic_class;
            canonical = input->byref ? &rebuilt->this_arg : &rebuilt->byval_arg;
            break;
        }
        case IL2CPP_TYPE_PTR:
        case IL2CPP_TYPE_SZARRAY:
        {
            const Il2CppType* element = Resolve(input->data.type, depth + 1);
            if (element == input->data.type) return input;
            output.data.type = element;
            Il2CppClass* rebuilt;
            {
                AssemblyShadowTypeMetadataScope scope;
                rebuilt = input->type == IL2CPP_TYPE_PTR ? Class::GetPtrClass(element) : Class::GetArrayClass(RawClass(element), 1);
            }
            if (!rebuilt) Fail("ShadowCompositeRebuildFailed", "PointerOrArray");
            canonical = input->byref ? &rebuilt->this_arg : &rebuilt->byval_arg;
            break;
        }
        case IL2CPP_TYPE_ARRAY:
        {
            const Il2CppArrayType* array = input->data.array;
            if (!array || !array->rank || array->numsizes > array->rank || array->numlobounds > array->rank ||
                (array->numsizes && !array->sizes) || (array->numlobounds && !array->lobounds))
                Fail("ShadowUnsupportedTypeShape", "Array");
            const Il2CppType* element = Resolve(array->etype, depth + 1);
            if (element == array->etype) return input;
            Il2CppArrayType mapped = *array;
            mapped.etype = element;
            output.data.array = &mapped;
            {
                AssemblyShadowTypeMetadataScope scope;
                Il2CppClass* rebuilt = Class::GetBoundedArrayClass(RawClass(element), array->rank, true);
                if (!rebuilt) Fail("ShadowCompositeRebuildFailed", "BoundedArray");
            }
            Increment(State().rebuilds);
            return Intern(output);
        }
        case IL2CPP_TYPE_VAR:
        case IL2CPP_TYPE_MVAR:
            return input; // Preserve the exact current generic-parameter context.
        default:
            if (!Class::FromIl2CppTypeEnum(input->type))
                Fail("ShadowUnsupportedTypeShape", "TypeCode=" + std::to_string(input->type));
            return input;
    }
    if (input->type != IL2CPP_TYPE_CLASS && input->type != IL2CPP_TYPE_VALUETYPE) Increment(State().rebuilds);
    return Intern(output, canonical);
}

enum class VisitMode { Baseline, Shadow, Use };
bool Visit(const Il2CppType* type, VisitMode mode, BaselineUseKind kind, const char* site, uint32_t depth)
{
    if (!type || depth > kMaximumDepth) Fail("ShadowUnsupportedTypeShape", "NullOrRecursiveType");
    if (type->type == IL2CPP_TYPE_PTR || type->type == IL2CPP_TYPE_SZARRAY)
        return Visit(type->data.type, mode, kind, site, depth + 1);
    if (type->type == IL2CPP_TYPE_ARRAY)
    {
        if (!type->data.array) Fail("ShadowUnsupportedTypeShape", "Array");
        return Visit(type->data.array->etype, mode, kind, site, depth + 1);
    }
    bool found = false;
    if (type->type == IL2CPP_TYPE_GENERICINST)
    {
        const auto* generic = type->data.generic_class;
        if (!generic || !generic->context.class_inst || generic->context.method_inst) Fail("ShadowUnsupportedTypeShape", "GenericContext");
        found = Visit(generic->type, mode, kind, site, depth + 1);
        for (uint32_t index = 0; index < generic->context.class_inst->type_argc; ++index)
            found |= Visit(generic->context.class_inst->type_argv[index], mode, kind, site, depth + 1);
        return found;
    }
    Il2CppClass* owner;
    {
        AssemblyShadowTypeMetadataScope scope;
        owner = type->type == IL2CPP_TYPE_VAR || type->type == IL2CPP_TYPE_MVAR ?
            MetadataCache::GetParameterDeclaringType(type->data.genericParameterHandle) : AssemblyShadowTypeKey::RawDefinition(type);
    }
    if (!owner || !owner->image) return false;
    if (mode == VisitMode::Baseline) return AssemblyShadow::IsShadowedBaseline(owner->image->assembly);
    if (mode == VisitMode::Shadow) return AssemblyShadow::IsActiveShadow(owner->image->assembly) || AssemblyShadow::IsShadowedBaseline(owner->image->assembly);
    AssemblyShadow::RecordBaselineUse(owner->image->assembly, kind, site, owner);
    return false;
}

Il2CppClass* Owner(const Il2CppType* type, uint32_t depth = 0)
{
    if (!type || depth > kMaximumDepth) Fail("ShadowUnsupportedTypeShape", "Owner");
    if (type->type == IL2CPP_TYPE_PTR || type->type == IL2CPP_TYPE_SZARRAY) return Owner(type->data.type, depth + 1);
    if (type->type == IL2CPP_TYPE_ARRAY && type->data.array) return Owner(type->data.array->etype, depth + 1);
    if (type->type == IL2CPP_TYPE_VAR || type->type == IL2CPP_TYPE_MVAR)
        return MetadataCache::GetParameterDeclaringType(type->data.genericParameterHandle);
    return AssemblyShadowTypeKey::RawDefinition(type);
}

struct InstanceFieldLayout
{
    std::string signature;
    const Il2CppType* type;
    uint32_t offset;
};

std::vector<InstanceFieldLayout> InstanceFieldLayouts(const Il2CppClass* klass)
{
    std::vector<InstanceFieldLayout> fields;
    for (uint32_t index = 0; index < klass->field_count; ++index)
    {
        Il2CppMetadataFieldInfo field = MetadataCache::GetFieldInfo(klass, index);
        if (field.type->attrs & FIELD_ATTRIBUTE_STATIC) continue;
        // These raw table offsets do not allocate static/thread-static storage.
        FieldInfo raw = {};
        raw.name = field.name; raw.type = field.type; raw.parent = const_cast<Il2CppClass*>(klass);
        Il2CppType signature = *field.type;
        signature.attrs = 0; // Field visibility is not a storage/type qualifier.
        fields.push_back({std::string(field.name) + ":" + AssemblyShadowTypeKey::Format(&signature), field.type,
            GlobalMetadata::GetFieldOffset(klass, index, &raw)});
    }
    return fields;
}

std::vector<std::string> StructuralInstanceFields(const Il2CppClass* klass)
{
    std::vector<std::string> fields;
    for (const InstanceFieldLayout& field : InstanceFieldLayouts(klass)) fields.push_back(field.signature);
    std::sort(fields.begin(), fields.end());
    return fields;
}

std::vector<std::string> Interfaces(const Il2CppClass* klass)
{
    std::vector<std::string> interfaces;
    for (uint16_t index = 0; index < klass->interfaces_count; ++index)
    {
        const Il2CppType* type = MetadataCache::GetInterfaceFromOffset(klass, index);
        if (!type) Fail("ShadowInterfaceLayoutMismatch", AssemblyShadowTypeKey::Format(&klass->byval_arg));
        interfaces.push_back(AssemblyShadowTypeKey::Format(type));
    }
    std::sort(interfaces.begin(), interfaces.end());
    return interfaces;
}

uint32_t PrimitiveStorageSize(const Il2CppType* type)
{
    if (!type || type->byref || type->pinned) return 0;
    switch (type->type)
    {
        case IL2CPP_TYPE_BOOLEAN: case IL2CPP_TYPE_I1: case IL2CPP_TYPE_U1: return 1;
        case IL2CPP_TYPE_CHAR: case IL2CPP_TYPE_I2: case IL2CPP_TYPE_U2: return 2;
        case IL2CPP_TYPE_I4: case IL2CPP_TYPE_U4: case IL2CPP_TYPE_R4: return 4;
        case IL2CPP_TYPE_I8: case IL2CPP_TYPE_U8: case IL2CPP_TYPE_R8: return 8;
        case IL2CPP_TYPE_I: case IL2CPP_TYPE_U: return static_cast<uint32_t>(sizeof(void*));
        default: return 0;
    }
}

bool CompatibleInstanceFields(const Il2CppClass* source, const Il2CppClass* target)
{
    const std::vector<InstanceFieldLayout> baseline = InstanceFieldLayouts(source);
    const std::vector<InstanceFieldLayout> active = InstanceFieldLayouts(target);
    if (active.size() < baseline.size()) return false;
    for (size_t index = 0; index < baseline.size(); ++index)
        if (baseline[index].signature != active[index].signature || baseline[index].offset != active[index].offset) return false;
    if (active.size() == baseline.size()) return source->instance_size == target->instance_size;
    // Existing objects are never admitted before Commit. A post-Commit Unity
    // allocation may therefore use a larger active reference type, but only for
    // strictly appended, private, explicitly nonserialized primitive storage.
    // Value types can be embedded in already frozen owners and may never grow.
    if (source->byval_arg.valuetype || !source->instance_size || target->instance_size < source->instance_size) return false;
    uint32_t previousEnd = source->instance_size;
    for (size_t index = baseline.size(); index < active.size(); ++index)
    {
        const InstanceFieldLayout& field = active[index];
        const uint32_t size = PrimitiveStorageSize(field.type);
        const uint16_t attributes = field.type ? field.type->attrs : 0;
        if (!size || (attributes & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK) != FIELD_ATTRIBUTE_PRIVATE ||
            !(attributes & FIELD_ATTRIBUTE_NOT_SERIALIZED) || field.offset < previousEnd ||
            field.offset > UINT32_MAX - size || field.offset + size > target->instance_size)
            return false;
        previousEnd = field.offset + size;
    }
    return true;
}
void CheckLayout(Il2CppClass* source, Il2CppClass* target, const char* site, uint32_t depth = 0, bool structural = false)
{
    if (source == target) return;
    if (!source || !target || depth > kMaximumDepth) Fail("ShadowLayoutMismatch", site, AssemblyShadowError::ResourceAbiMismatch);
    const std::string key = AssemblyShadowTypeKey::Format(&source->byval_arg) + " Site=" + site;
    MatchDefinition(source, target, key);
    if (source->initialized || source->cctor_started || source->is_vtable_initialized)
        Fail("ShadowBaselineAlreadyInitialized", key, AssemblyShadowError::BaselineAlreadyUsed);
    if (!structural && (source->generic_class || target->generic_class || source->rank || target->rank))
    {
        // A generic/array handle with no physical layout is not permission to
        // initialize its old business components just to manufacture proof.
        if (!source->size_inited || !target->size_inited)
            Fail("ShadowLayoutUnavailable", key, AssemblyShadowError::ResourceAbiMismatch);
    }
    // native_size describes marshaling layout, not managed heap allocation.
    // IL2CPP gives delegate reference types a method-pointer native size while
    // interpreter reference types deliberately use -1. Requiring those values
    // to match rejects an otherwise identical delegate allocation. Preserve
    // the native-size guard for value types, where it is part of the physical
    // representation that can be copied or boxed.
    const bool nativeSizeMismatch = source->byval_arg.valuetype && source->native_size != target->native_size;
    if ((!structural && (!source->instance_size || !target->instance_size || target->instance_size < source->instance_size ||
        nativeSizeMismatch)) || source->packingSize != target->packingSize ||
        ((source->flags ^ target->flags) & TYPE_ATTRIBUTE_LAYOUT_MASK))
        Fail("ShadowLayoutMismatch", key + " BaselineSize=" + std::to_string(source->instance_size) +
            " ActiveSize=" + std::to_string(target->instance_size), AssemblyShadowError::ResourceAbiMismatch);
    if (source->typeMetadataHandle && target->typeMetadataHandle)
    {
        if (!structural && source->instance_size != target->instance_size &&
            InstanceFieldLayouts(source).size() == InstanceFieldLayouts(target).size())
            Fail("ShadowLayoutMismatch", key + " SizeChangedWithoutAppendedStorage", AssemblyShadowError::ResourceAbiMismatch);
        const bool fieldsMatch = structural ? StructuralInstanceFields(source) == StructuralInstanceFields(target) :
            CompatibleInstanceFields(source, target);
        if (!fieldsMatch) Fail("ShadowFieldLayoutMismatch", key, AssemblyShadowError::ResourceAbiMismatch);
    }
    else if (!structural && source->instance_size != target->instance_size)
        Fail("ShadowLayoutMismatch", key + " MissingFieldMetadata", AssemblyShadowError::ResourceAbiMismatch);
    if (Interfaces(source) != Interfaces(target))
        Fail("ShadowInterfaceLayoutMismatch", key, AssemblyShadowError::ResourceAbiMismatch);
    if ((source->parent == nullptr) != (target->parent == nullptr) ||
        (source->parent && AssemblyShadowTypeKey::Format(&source->parent->byval_arg) != AssemblyShadowTypeKey::Format(&target->parent->byval_arg)))
        Fail("ShadowParentLayoutMismatch", key, AssemblyShadowError::ResourceAbiMismatch);
    if (source->parent && source->parent != target->parent)
        CheckLayout(source->parent, target->parent, site, depth + 1, structural);
}

void CheckActiveComponents(const Il2CppType* type, const char* site, uint32_t depth = 0)
{
    if (!type || depth > kMaximumDepth) Fail("ShadowUnsupportedTypeShape", "AllocationComponents");
    if (type->type == IL2CPP_TYPE_PTR || type->type == IL2CPP_TYPE_SZARRAY)
    {
        CheckActiveComponents(type->data.type, site, depth + 1);
        return;
    }
    if (type->type == IL2CPP_TYPE_ARRAY)
    {
        if (!type->data.array) Fail("ShadowUnsupportedTypeShape", "Array");
        CheckActiveComponents(type->data.array->etype, site, depth + 1);
        return;
    }
    if (type->type == IL2CPP_TYPE_GENERICINST)
    {
        const Il2CppGenericClass* generic = type->data.generic_class;
        if (!generic || !generic->context.class_inst || generic->context.method_inst)
            Fail("ShadowUnsupportedTypeShape", "GenericContext");
        CheckActiveComponents(generic->type, site, depth + 1);
        for (uint32_t index = 0; index < generic->context.class_inst->type_argc; ++index)
            CheckActiveComponents(generic->context.class_inst->type_argv[index], site, depth + 1);
        return;
    }
    if (type->type == IL2CPP_TYPE_VAR || type->type == IL2CPP_TYPE_MVAR) return;
    Il2CppClass* active = AssemblyShadowTypeKey::RawDefinition(type);
    if (!active || !active->image || !AssemblyShadow::IsActiveShadow(active->image->assembly)) return;
    const Il2CppAssembly* baselineAssembly = MetadataCache::GetAotAssemblyByNamePhysical(active->image->assembly->aname.name);
    if (!baselineAssembly) Fail("ShadowLayoutUnavailable", "MissingPhysicalBaselineAssembly", AssemblyShadowError::ResourceAbiMismatch);
    // Active-first lookup is not layout proof. Read the matching physical
    // definition solely for safety comparison, never Init/SetupFields and never
    // to manufacture optional diagnostic pointer evidence. Patch-added types
    // have no baseline counterpart and remain legal.
    Il2CppClass* baseline = FindDefinition(baselineAssembly->image, AssemblyShadowTypeKey::Make(active), true);
    if (baseline) CheckLayout(baseline, active, site, depth, AssemblyShadowTypeKey::GenericArity(active) != 0);
}
std::string Pointer(const void* pointer)
{
    std::ostringstream text; text << "0x" << std::hex << reinterpret_cast<uintptr_t>(pointer); return text.str();
}
}

Il2CppClass* AssemblyShadowTypeResolver::ResolveDefinition(Il2CppClass* klass)
{
    if (!klass || !klass->image || Private() || !AssemblyShadow::IsShadowedBaseline(klass->image->assembly)) return klass;
    if (klass->generic_class || klass->rank) Fail("ShadowInvalidDefinitionKey", "CompositeClass");
    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.definitions.find(klass);
        if (found != state.definitions.end()) { Increment(state.hits); return found->second; }
    }
    Increment(state.misses);
    Il2CppClass* mapped;
    {
        AssemblyShadowTypeMetadataScope scope;
        ShadowTypeKey key = AssemblyShadowTypeKey::Make(klass);
        mapped = FindDefinition(AssemblyShadow::ResolveImage(klass->image), key);
        if (!mapped) Fail("ShadowTypeNotFound", key.ToString());
        MatchDefinition(klass, mapped, key.ToString());
        if (!(AssemblyShadowTypeKey::Make(mapped) == key)) Fail("ShadowDeclarationChainMismatch", key.ToString());
    }
    std::lock_guard<std::mutex> lock(state.mutex);
    auto inserted = state.definitions.emplace(klass, mapped);
    state.knownBaselines.emplace(&mapped->byval_arg, &klass->byval_arg);
    state.knownBaselines.emplace(&mapped->this_arg, &klass->this_arg);
    return inserted.first->second;
}

const Il2CppType* AssemblyShadowTypeResolver::Resolve(const Il2CppType* type)
{
    if (!type || Private() || !AssemblyShadow::ActiveGeneration()) return type;
    return ::il2cpp::vm::Resolve(type, 0);
}

Il2CppClass* AssemblyShadowTypeResolver::ResolveClass(Il2CppClass* klass)
{
    if (!klass || Private() || !AssemblyShadow::ActiveGeneration()) return klass;
    const Il2CppType* active = Resolve(&klass->byval_arg);
    return active == &klass->byval_arg ? klass : RawClass(active);
}

const MethodInfo* AssemblyShadowTypeResolver::ResolveReflectionMethod(const MethodInfo* method)
{
    if (!method || Private() || !AssemblyShadow::ActiveGeneration()) return method;

    const MethodInfo* definition = method;
    const Il2CppGenericContext* context = nullptr;
    if (method->is_inflated)
    {
        if (!method->genericMethod || !method->genericMethod->methodDefinition ||
            method->genericMethod->methodDefinition->is_inflated)
            Fail("ShadowInvalidMethodInstantiation", "MissingDefinition");
        definition = method->genericMethod->methodDefinition;
        context = &method->genericMethod->context;
    }
    if (!definition->klass || !definition->klass->image || !definition->klass->image->assembly ||
        !AssemblyShadow::IsShadowedBaseline(definition->klass->image->assembly))
        return method;

    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.reflectionMethods.find(method);
        if (found != state.reflectionMethods.end()) return found->second;
    }

    const MethodInfo* activeDefinition;
    {
        AssemblyShadowTypeMetadataScope scope;
        activeDefinition = FindMethodDefinition(definition, ResolveDefinition(definition->klass));
    }
    const MethodInfo* active = activeDefinition;
    if (context)
    {
        const Il2CppGenericInst* classInst = ResolveMethodInstantiation(context->class_inst);
        const Il2CppGenericInst* methodInst = ResolveMethodInstantiation(context->method_inst);
        {
            AssemblyShadowTypeMetadataScope scope;
            active = il2cpp::metadata::GenericMethod::GetMethod(activeDefinition, classInst, methodInst);
        }
        if (!active || active->klass != ResolveClass(method->klass))
            Fail("ShadowMethodInflationFailed", MethodSignature(definition));
    }

    std::lock_guard<std::mutex> lock(state.mutex);
    return state.reflectionMethods.emplace(method, active).first->second;
}

Il2CppClass* AssemblyShadowTypeResolver::ResolveAllocation(Il2CppClass* klass, const char* site)
{
    if (!klass) return nullptr;
    if (!site) site = "<unspecified>";
    if (AssemblyShadow::IsResolvingTypeMetadata())
        Fail("ShadowAllocationDuringMetadataResolution", site, AssemblyShadowError::BaselineAlreadyUsed);
    Il2CppClass* target = ResolveClass(klass);
    if (target != klass)
    {
        AssemblyShadowTypeMetadataScope scope;
        CheckLayout(klass, target, site);
        Increment(State().allocations);
    }
    if (AssemblyShadow::ActiveGeneration())
    {
        AssemblyShadowTypeMetadataScope scope;
        CheckActiveComponents(&target->byval_arg, site);
    }
    return target;
}

bool AssemblyShadowTypeResolver::ContainsBaseline(const Il2CppType* type)
{
    return type && AssemblyShadow::ActiveGeneration() && Visit(type, VisitMode::Baseline, BaselineUseKind::TypeReflection, "", 0);
}

void AssemblyShadowTypeResolver::RecordUse(const Il2CppType* type, BaselineUseKind kind, const char* site)
{
    if (!type) return;
    Visit(type, VisitMode::Use, kind, site, 0);
}

void AssemblyShadowTypeResolver::CountGuardFailure() { Increment(State().failures); }

AssemblyShadowError AssemblyShadowTypeResolver::GetInfo(const Il2CppType* type, std::string& json)
{
    json.clear();
    if (!type) return AssemblyShadowError::InvalidArgument;
    try
    {
        Il2CppClass* owner;
        std::string key;
        {
            AssemblyShadowTypeMetadataScope scope;
            owner = Owner(type);
            key = AssemblyShadowTypeKey::Format(type);
        }
        if (!owner || !owner->image || !owner->image->assembly) return AssemblyShadowError::ReferenceResolutionFailed;
        const Il2CppType* active = Resolve(type);
        const bool isActive = !ContainsBaseline(type);
        const bool contains = Visit(active, VisitMode::Shadow, BaselineUseKind::TypeReflection, "", 0);
        AssemblyExecutionMode mode = AssemblyExecutionMode::AotBaseline;
        AssemblyShadow::GetAssemblyExecutionMode(owner->image->assembly->aname.name, mode);
        const Il2CppType* baseline = nullptr;
        std::string inputPointer, activePointer, baselinePointer;
        bool pointerDetails = false;
#if IL2CPP_DEBUG
        pointerDetails = true;
        if (!isActive) baseline = type;
        else
        {
            auto& state = State();
            std::lock_guard<std::mutex> lock(state.mutex);
            auto found = state.knownBaselines.find(type);
            if (found != state.knownBaselines.end()) baseline = found->second;
        }
        inputPointer = Pointer(type); activePointer = Pointer(active);
        if (baseline) baselinePointer = Pointer(baseline);
#endif
        auto& state = State();
        std::ostringstream output;
        output << std::boolalpha << "{\"schemaVersion\":1,\"logicalAssembly\":" << AssemblyShadowDiagnostics::Quote(owner->image->assembly->aname.name)
            << ",\"executionModeCode\":" << static_cast<int>(mode)
            << ",\"executionMode\":\"" << (mode == AssemblyExecutionMode::InterpreterShadow ? "InterpreterShadow" : "AotBaseline")
            << "\",\"isActive\":" << isActive << ",\"physicalImageKind\":\""
            << (hybridclr::metadata::IsInterpreterImage(owner->image) ? "Interpreter" : "Aot")
            << "\",\"typeKey\":" << AssemblyShadowDiagnostics::Quote(key)
            << ",\"inputTypePointer\":" << AssemblyShadowDiagnostics::Quote(inputPointer)
            << ",\"activeTypePointer\":" << AssemblyShadowDiagnostics::Quote(activePointer)
            << ",\"baselineTypePointer\":" << AssemblyShadowDiagnostics::Quote(baselinePointer)
            << ",\"pointerDetailsAvailable\":" << pointerDetails << ",\"baselinePointerAvailable\":" << (baseline != nullptr)
            << ",\"containsShadowTypes\":" << contains
            << ",\"definitionCacheHits\":" << state.hits.load() << ",\"definitionCacheMisses\":" << state.misses.load()
            << ",\"compositeRebuilds\":" << state.rebuilds.load() << ",\"allocationRemaps\":" << state.allocations.load()
            << ",\"guardFailures\":" << state.failures.load() << "}";
        json = output.str();
        return AssemblyShadowError::Success;
    }
    catch (const ShadowTypeResolutionFailure& error) { return error.error; }
    catch (const std::exception&) { return AssemblyShadowError::InternalError; }
}
}}
#endif
