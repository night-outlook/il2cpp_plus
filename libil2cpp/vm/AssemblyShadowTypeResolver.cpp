#include "AssemblyShadowTypeResolver.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadow.h"
#include "AssemblyShadowDiagnostics.h"
#include "AssemblyShadowAllocationProof.h"
#include "AssemblyShadowLayoutReadiness.h"
#include "AssemblyShadowLogicalMethod.h"
#include <cstdio>
#include <cstring>
#include "AssemblyShadowR02Diagnostics.h"
#include "vm/MetadataLock.h"
#include "os/Atomic.h"
#include "vm/Class.h"
#include "vm/GenericClass.h"
#include "vm/GlobalMetadata.h"
#include "vm/MetadataCache.h"
#include "metadata/GenericMethod.h"
#include "hybridclr/metadata/AssemblyShadowBridge.h"
#include "hybridclr/metadata/MetadataUtil.h"
#include "hybridclr/metadata/MetadataModule.h"
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
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::DefinitionHits> hits;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::DefinitionMisses> misses;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::CompositeRebuilds> rebuilds;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::AllocationRemaps> allocations;
    assembly_shadow_r02::ObservationCounter<assembly_shadow_r02::Metric::GuardFailures> failures;
};
ResolverState& State() { static ResolverState* state = new ResolverState(); return *state; }
template<assembly_shadow_r02::Metric Id>
void Increment(assembly_shadow_r02::ObservationCounter<Id>& counter)
{
    counter.fetch_add(1, std::memory_order_relaxed);
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
    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::DefinitionSearches, 1);
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
                assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::DefinitionRows, 1);
                auto handle = MetadataCache::GetAssemblyTypeHandle(image, index);
                if (!MetadataCache::TypeIsNested(handle)) consider(handle);
            }
        }
        else
        {
            void* iterator = nullptr;
            while (auto handle = MetadataCache::GetNestedTypes(result->typeMetadataHandle, &iterator))
            {
                assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::DefinitionRows, 1);
                consider(handle);
            }
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
    return assembly_shadow_evolution::LogicalMethodKey(method);
}

const MethodInfo* FindMethodDefinition(const MethodInfo* baseline, Il2CppClass* activeClass)
{
    const std::string signature = MethodSignature(baseline);
    const MethodInfo* result = nullptr;
    for (uint16_t index = 0; index < activeClass->method_count; ++index)
    {
        Il2CppMetadataMethodInfo raw = MetadataCache::GetMethodInfo(activeClass, index);
        const MethodInfo* candidate = MetadataCache::GetMethodInfoFromMethodHandle(raw.handle);
        if (!candidate || candidate->klass != activeClass || !candidate->name) Fail("ShadowInvalidMethodSignature", signature);
        // Unrelated overload families need not have representable signatures.
        // For a potentially matching family an unsupported shape fails closed;
        // equal counts of discarded custom modifiers are never accepted as proof.
        if (std::strcmp(candidate->name, baseline->name) || candidate->parameters_count != baseline->parameters_count ||
            candidate->is_generic != baseline->is_generic ||
            ((candidate->flags ^ baseline->flags) & METHOD_ATTRIBUTE_STATIC)) continue;
        if (candidate->is_generic && MetadataCache::GetGenericContainerCount(candidate->genericContainerHandle) !=
            MetadataCache::GetGenericContainerCount(baseline->genericContainerHandle)) continue;
        if (MethodSignature(candidate) != signature) continue;
        if (result) Fail("ShadowAmbiguousMethodDefinition", signature);
        result = candidate;
    }
    if (!result) Fail("ShadowMethodNotFound", signature);
    assembly_shadow_evolution::RequireMethodCompatibility(baseline, result);
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
    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::FieldWorkspaces, 1);
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
    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::InterfaceWorkspaces, 1);
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
    // strictly appended, private primitive storage. Whether that storage is
    // serialized is a deployment/resource-ABI decision: the Editor rejects a
    // DLL-only serialized change and Bootstrap binds a rebuilt catalog before
    // any Unity object is loaded. This layer proves only allocation safety.
    // Value types can be embedded in already frozen owners and may never grow.
    if (source->byval_arg.valuetype || !source->instance_size || target->instance_size < source->instance_size) return false;
    uint32_t previousEnd = source->instance_size;
    for (size_t index = baseline.size(); index < active.size(); ++index)
    {
        const InstanceFieldLayout& field = active[index];
        const uint32_t size = PrimitiveStorageSize(field.type);
        const uint16_t attributes = field.type ? field.type->attrs : 0;
        if (!size || (attributes & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK) != FIELD_ATTRIBUTE_PRIVATE || field.offset < previousEnd ||
            field.offset > UINT32_MAX - size || field.offset + size > target->instance_size)
            return false;
        previousEnd = field.offset + size;
    }
    return true;
}
assembly_shadow_r02::PhysicalLayoutState PhysicalLayout(Il2CppClass* klass)
{
    return {klass->size_inited != 0, klass->size_init_pending != 0,
        klass->image != nullptr, klass->typeMetadataHandle != nullptr,
        klass->image && hybridclr::metadata::IsInterpreterImage(klass->image),
        klass->generic_class != nullptr, klass->is_generic != 0, klass->rank != 0,
        klass->byval_arg.type == IL2CPP_TYPE_CLASS || klass->byval_arg.type == IL2CPP_TYPE_VALUETYPE,
        klass->instance_size};
}

void ObserveLayoutReadiness(Il2CppClass* source, Il2CppClass* target, bool sourceReady,
    const char* site)
{
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL >= 2
    // Diagnostic-only, bounded to sixteen cold decisions per process. No
    // managed log callback, class initialization or schema mutation is involved.
    static std::atomic<uint32_t> emitted{0};
    uint32_t slot = emitted.load(std::memory_order_relaxed);
    do { if (slot >= 16) return; }
    while (!emitted.compare_exchange_weak(slot, slot + 1, std::memory_order_relaxed));
    std::fprintf(stderr, "[R02LayoutReadiness] sample=%u source=%p target=%p "
        "sourceToken=%u targetToken=%u sourceImage=%p targetImage=%p "
        "sourceSizeInited=%u targetSizeInited=%u sourcePending=%u targetPending=%u "
        "sourceGeneric=%u targetGeneric=%u sourceRank=%u targetRank=%u "
        "sourceSize=%u targetSize=%u sourceReady=%u site=%.96s\n",
        slot, static_cast<void*>(source), static_cast<void*>(target), source->token, target->token,
        static_cast<const void*>(source->image), static_cast<const void*>(target->image),
        unsigned(source->size_inited), unsigned(target->size_inited),
        unsigned(source->size_init_pending), unsigned(target->size_init_pending),
        unsigned(source->generic_class != nullptr || source->is_generic),
        unsigned(target->generic_class != nullptr || target->is_generic),
        unsigned(source->rank), unsigned(target->rank), source->instance_size,
        target->instance_size, unsigned(sourceReady), site);
#else
    (void)source; (void)target; (void)sourceReady; (void)site;
#endif
}

void CheckLayout(Il2CppClass* source, Il2CppClass* target, const char* site, uint32_t depth = 0, bool structural = false)
{
    if (source == target) return;
    if (!source || !target || depth > kMaximumDepth) Fail("ShadowLayoutMismatch", site, AssemblyShadowError::ResourceAbiMismatch);
    assembly_shadow_r02::ObservationCounters::Add(assembly_shadow_r02::Metric::LayoutChecks, 1);
    if (auto* trace = assembly_shadow_r02::AllocationProofTrace<Il2CppClass>::Current())
    {
        trace->Observe(source, target, structural);
        if (!structural)
        {
            const bool sourceReady = assembly_shadow_r02::BaselineLayoutReady(
                PhysicalLayout(source), static_cast<uint32_t>(sizeof(Il2CppObject)));
            if (!sourceReady || !target->size_inited) trace->complete = false;
            if (!source->size_inited || !target->size_inited)
                ObserveLayoutReadiness(source, target, sourceReady, site);
        }
    }
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

// A staging screen reads signatures without asking for unmaterialized open
// generic offsets. It never creates objects, runs Class::Init or publishes a
// ClassAdmissionCertificate. Platform-dependent changed layouts reuse the
// exact existing CheckLayout implementation once their metadata is ready.
std::vector<std::string> StagedFieldShapes(const Il2CppClass* klass)
{
    std::vector<std::string> fields;
    for (uint32_t index = 0; index < klass->field_count; ++index)
    {
        const auto field = MetadataCache::GetFieldInfo(klass, index);
        if (!field.name || !field.type) Fail("NativeLayoutIncompatible", "MissingFieldMetadata", AssemblyShadowError::ResourceAbiMismatch);
        if (field.type->attrs & FIELD_ATTRIBUTE_STATIC) continue;
        Il2CppType type = *field.type;
        type.attrs = 0;
        fields.push_back(assembly_shadow_evolution::MethodPart(field.name) +
            assembly_shadow_evolution::MethodPart(AssemblyShadowTypeKey::Format(&type)));
    }
    return fields;
}

std::string StagedDeclarationIdentity(Il2CppClass* klass)
{
    ShadowTypeKey key = AssemblyShadowTypeKey::Make(klass);
    // Find an existing declaration even when malformed metadata changes its
    // arity without changing its name. MatchDefinition then rejects that change.
    for (auto& declaration : key.declarations) declaration.genericArity = 0;
    return key.ToString();
}

bool CheckStagedPair(Il2CppClass* baseline, Il2CppClass* target)
{
    const std::string key = AssemblyShadowTypeKey::Make(target).ToString();
    MatchDefinition(baseline, target, key);
    if ((baseline->parent == nullptr) != (target->parent == nullptr) ||
        (baseline->parent && AssemblyShadowTypeKey::Format(&baseline->parent->byval_arg) !=
            AssemblyShadowTypeKey::Format(&target->parent->byval_arg)) ||
        Interfaces(baseline) != Interfaces(target) ||
        baseline->packingSize != target->packingSize ||
        ((baseline->flags ^ target->flags) & TYPE_ATTRIBUTE_LAYOUT_MASK) ||
        assembly_shadow_evolution::DeclarationContracts(baseline) != assembly_shadow_evolution::DeclarationContracts(target))
        Fail("NativeLayoutIncompatible", key + " DeclarationOrInterfaceContract", AssemblyShadowError::ResourceAbiMismatch);
    const bool fieldsChanged = StagedFieldShapes(baseline) != StagedFieldShapes(target);
    if (AssemblyShadowTypeKey::GenericArity(baseline) || AssemblyShadowTypeKey::GenericArity(target))
    {
        if (fieldsChanged) Fail("NativeLayoutIncompatible", key + " OpenGenericStructuralChange", AssemblyShadowError::ResourceAbiMismatch);
        return false; // A later constructed physical type still needs its own certificate.
    }
    const bool baselineReady = assembly_shadow_r02::BaselineLayoutReady(
        PhysicalLayout(baseline), static_cast<uint32_t>(sizeof(Il2CppObject)));
    // InitRuntimeMetadatas already computes and seals concrete definition
    // sizes/offsets. size_inited additionally includes lazy field/static setup,
    // which must NOT be forced merely to manufacture prepublication proof.
    const Il2CppTypeDefinitionSizes* targetDefinition = nullptr;
    const bool definitionReady = hybridclr::metadata::MetadataModule::GetImage(target)->
        TryGetReadyDefinitionLayout(target, targetDefinition);
    const bool targetReady = !target->size_init_pending && (target->size_inited || definitionReady);
    assembly_shadow_r03::LayoutRow observation;
    observation.baseline = baseline; observation.target = target;
    observation.fieldsChanged = fieldsChanged; observation.baselineReady = baselineReady;
    observation.targetReady = targetReady; observation.targetDefinitionReady = definitionReady;
    observation.sourceSizeInited = baseline->size_inited; observation.targetSizeInited = target->size_inited;
    observation.sourcePending = baseline->size_init_pending; observation.targetPending = target->size_init_pending;
    observation.baselineInitialized = baseline->initialized; observation.baselineVtable = baseline->is_vtable_initialized;
    observation.baselineCctor = baseline->cctor_started; observation.targetInitialized = target->initialized;
    observation.targetVtable = target->is_vtable_initialized; observation.targetCctor = target->cctor_started;
    observation.sourceSize = baseline->instance_size; observation.targetSize = target->instance_size;
    observation.sourceNativeSize = baseline->native_size; observation.targetNativeSize = target->native_size;
    if (!baselineReady || !targetReady)
    {
        observation.error = fieldsChanged ? static_cast<int32_t>(AssemblyShadowError::ResourceAbiMismatch) : 0;
        assembly_shadow_r03::RuntimeProbe::RecordLayout(observation);
        if (fieldsChanged) Fail("NativeLayoutNeedsNativeProof", key + " ChangedLayoutUnavailableBeforePublication" +
            " BaselineReady=" + std::to_string(baselineReady) + " TargetReady=" + std::to_string(targetReady) +
            " TargetDefinitionReady=" + std::to_string(definitionReady), AssemblyShadowError::ResourceAbiMismatch);
        return false; // Unchanged metadata is not a physical allocation proof.
    }
    bool physicalChanged = baseline->instance_size != target->instance_size ||
        (baseline->byval_arg.valuetype && baseline->native_size != target->native_size);
    if (!physicalChanged && !fieldsChanged && baseline->typeMetadataHandle && target->typeMetadataHandle)
    {
        const auto before = InstanceFieldLayouts(baseline);
        const auto after = InstanceFieldLayouts(target);
        for (size_t index = 0; index < before.size(); ++index)
            if (before[index].offset != after[index].offset) { physicalChanged = true; break; }
    }
    if (!fieldsChanged && !physicalChanged)
    { assembly_shadow_r03::RuntimeProbe::RecordLayout(observation); return false; }
#if HYBRIDCLR_R03_RUNTIME_PROBE
    const auto sourceFields = InstanceFieldLayouts(baseline), targetFields = InstanceFieldLayouts(target);
    observation.sourceFieldCount = static_cast<uint32_t>(sourceFields.size());
    observation.targetFieldCount = static_cast<uint32_t>(targetFields.size());
    observation.truncated = sourceFields.size() > 16 || targetFields.size() > 16;
    for (size_t i = 0; i < sourceFields.size() && i < 16; ++i)
    { observation.sourceOffsets[i] = sourceFields[i].offset; observation.sourceAttrs[i] = sourceFields[i].type->attrs; }
    for (size_t i = 0; i < targetFields.size() && i < 16; ++i)
    { observation.targetOffsets[i] = targetFields[i].offset; observation.targetAttrs[i] = targetFields[i].type->attrs;
      observation.targetStorage[i] = PrimitiveStorageSize(targetFields[i].type); }
#endif
    try { CheckLayout(baseline, target, "NativeLayoutAdmissionV1:Validate"); }
    catch (const ShadowTypeResolutionFailure& error)
    { observation.error = static_cast<int32_t>(error.error); assembly_shadow_r03::RuntimeProbe::RecordLayout(observation); throw; }
    observation.physicalProof = true;
    assembly_shadow_r03::RuntimeProbe::RecordLayout(observation);
    return true;
}

Il2CppClass* BaselineCounterpart(Il2CppClass* active, const Il2CppImage* image)
{
    using namespace assembly_shadow_r02;
    const uint64_t generation = AssemblyShadow::ActiveGeneration();
    if (!generation || Private())
        return FindDefinition(image, AssemblyShadowTypeKey::Make(active), true);
    // A null value is an authenticated absence, not an allocation certificate.
    static AdmissionCache<Il2CppClass*>* cache = [] {
        auto* value = new AdmissionCache<Il2CppClass*>();
        ObservationCounters::Add(Metric::CacheFixedBytes, sizeof(*value));
        return value;
    }();
    const AdmissionKey key{generation, active, image, 2};
    if (const auto* found = cache->Find(key))
    {
        ObservationCounters::Add(Metric::CounterpartHits, 1);
        return *found;
    }
    ObservationCounters::Add(Metric::CounterpartMisses, 1);
    il2cpp::os::FastAutoLock metadataLock(&g_MetadataLock);
    if (const auto* found = cache->Find(key))
    {
        ObservationCounters::Add(Metric::CounterpartHits, 1);
        return *found;
    }
    Il2CppClass* baseline = FindDefinition(image, AssemblyShadowTypeKey::Make(active), true);
    bool inserted = false;
    const auto* result = cache->Publish(key, baseline, inserted);
    if (inserted)
    {
        ObservationCounters::Add(Metric::CounterpartEntries, 1);
        ObservationCounters::Add(Metric::CounterpartRetainedBytes, cache->EntryBytes());
        if (!baseline) ObservationCounters::Add(Metric::CounterpartAbsent, 1);
    }
    return *result;
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
    Il2CppClass* baseline = BaselineCounterpart(active, baselineAssembly->image);
    if (auto* trace = assembly_shadow_r02::AllocationProofTrace<Il2CppClass>::Current())
        trace->Observe(baseline, active, AssemblyShadowTypeKey::GenericArity(active) != 0);
    if (baseline) CheckLayout(baseline, active, site, depth, AssemblyShadowTypeKey::GenericArity(active) != 0);
}
std::string Pointer(const void* pointer)
{
    std::ostringstream text; text << "0x" << std::hex << reinterpret_cast<uintptr_t>(pointer); return text.str();
}
}

void AssemblyShadowTypeResolver::ValidateStagedImage(const Il2CppImage* image)
{
    if (!Private() || AssemblyShadow::ActiveGeneration() || !image || !image->assembly ||
        !hybridclr::metadata::IsInterpreterImage(image))
        Fail("NativeLayoutAdmissionContext", "PrivateUnpublishedImageRequired", AssemblyShadowError::InvalidState);
    AssemblyShadowTypeMetadataScope scope;
    const auto* baseline = MetadataCache::GetAotAssemblyByNamePhysical(image->assembly->aname.name);
    if (!baseline || !baseline->image || hybridclr::metadata::IsInterpreterImage(baseline->image))
        Fail("NativeLayoutAdmissionContext", "PhysicalAotBaselineRequired", AssemblyShadowError::InvalidState);
    // An explicit work bound, not a claim that every size below it is supported.
    // Existing metadata quotas, field-count limits and depth bounds also apply.
    const uint32_t maximumTypeRows = 1048576;
    if (baseline->image->typeCount > maximumTypeRows || image->typeCount > maximumTypeRows)
        Fail("NativeLayoutAdmissionLimit", "TypeRowBudget", AssemblyShadowError::ResourceAbiMismatch);
    std::unordered_map<std::string, Il2CppClass*> before;
    before.reserve(baseline->image->typeCount);
    for (uint32_t index = 0; index < baseline->image->typeCount; ++index)
    {
        auto* klass = MetadataCache::GetTypeInfoFromHandle(MetadataCache::GetAssemblyTypeHandle(baseline->image, index));
        if (!klass || klass->image != baseline->image || !before.emplace(StagedDeclarationIdentity(klass), klass).second)
            Fail("NativeLayoutIncompatible", "AmbiguousPhysicalBaseline", AssemblyShadowError::ResourceAbiMismatch);
    }
    uint32_t paired = 0, changed = 0, added = 0;
    std::unordered_map<std::string, bool> seen;
    seen.reserve(image->typeCount);
    for (uint32_t index = 0; index < image->typeCount; ++index)
    {
        auto* klass = MetadataCache::GetTypeInfoFromHandle(MetadataCache::GetAssemblyTypeHandle(image, index));
        if (!klass || klass->image != image)
            Fail("NativeLayoutIncompatible", "InvalidStagedTypeOwner", AssemblyShadowError::ResourceAbiMismatch);
        std::string key = StagedDeclarationIdentity(klass);
        if (!seen.emplace(key, true).second)
            Fail("NativeLayoutIncompatible", "AmbiguousStagedDeclaration", AssemblyShadowError::ResourceAbiMismatch);
        auto found = before.find(key);
        if (found == before.end()) { ++added; continue; }
        ++paired;
        if (CheckStagedPair(found->second, klass)) ++changed;
    }
#if HYBRIDCLR_ASSEMBLY_SHADOW_DIAGNOSTICS_LEVEL >= 2
    static std::atomic<uint32_t> emitted{0};
    const uint32_t sample = emitted.fetch_add(1, std::memory_order_relaxed);
    if (sample < 16)
        std::fprintf(stderr, "[R03NativeLayoutAdmissionV1] sample=%u assembly=%.160s baselineRows=%u targetRows=%u paired=%u changedProofs=%u added=%u\n",
            sample, image->assembly->aname.name, baseline->image->typeCount, image->typeCount, paired, changed, added);
#endif
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
    using namespace assembly_shadow_r02;
    using Certificate = AllocationCertificate<Il2CppClass>;
    if (!klass) return nullptr;
    if (!site) site = "<unspecified>";
    assembly_shadow_r03::RuntimeProbe::SiteScope probeSite(site);
    if (AssemblyShadow::IsResolvingTypeMetadata())
        Fail("ShadowAllocationDuringMetadataResolution", site, AssemblyShadowError::BaselineAlreadyUsed);
    const uint64_t generation = AssemblyShadow::ActiveGeneration();
    auto build = [&](Certificate& certificate) {
        Il2CppClass* target = ResolveClass(klass);
        if (target != klass)
        {
            AssemblyShadowTypeMetadataScope scope;
            CheckLayout(klass, target, site);
            Increment(State().allocations);
            certificate.involvesShadow = true;
        }
        if (generation)
        {
            AssemblyShadowTypeMetadataScope scope;
            CheckActiveComponents(&target->byval_arg, site);
        }
        // A first allocation can precede lazy size finalization. Preserve its
        // successful uncached behavior, but retry proof after metadata is ready.
        certificate.complete = certificate.complete && target->size_inited;
        return target;
    };
    if (!generation || Private())
    {
        Certificate temporary;
        return build(temporary);
    }
    static AllocationProofCache<Il2CppClass>* cache = [] {
        auto* value = new AllocationProofCache<Il2CppClass>();
        ObservationCounters::Add(Metric::CacheFixedBytes, sizeof(*value));
        return value;
    }();
    // Conservative allocation profile 1; full physical class identity keeps
    // closed generics, arrays, baseline inputs and active inputs distinct.
    const AdmissionKey key{generation, klass, nullptr, 1};
    auto validate = [&](const Certificate* certificate) {
        if (AssemblyShadow::IsResolvingTypeMetadata())
            Fail("ShadowAllocationDuringMetadataResolution", site, AssemblyShadowError::BaselineAlreadyUsed);
        if (Private() || AssemblyShadow::ActiveGeneration() != generation)
            Fail("ShadowAdmissionContextChanged", site, AssemblyShadowError::InvalidState);
        if (!certificate) return;
        if (certificate->involvesShadow)
        {
            AssemblyShadowState state;
            AssemblyShadow::GetState(state); // Atomic acquire, no transaction lock.
            if (state == AssemblyShadowState::Failed || state == AssemblyShadowState::FailedAfterCommit)
                Fail("ShadowAllocationAfterFailure", site, AssemblyShadowError::InvalidState);
        }
        // These are the same mutable baseline-state conditions as CheckLayout,
        // including every recursively checked parent/component. Guarded engine
        // entry points remain authoritative; no old object's class is remapped.
        for (const auto& dependency : certificate->dependencies)
        {
            Il2CppClass* baseline = dependency.baseline;
            if (!baseline) continue;
            ObservationCounters::Add(Metric::BaselineChecks, 1);
            if (baseline->initialized || baseline->is_vtable_initialized ||
                os::Atomic::LoadRelaxed(reinterpret_cast<const int32_t*>(&baseline->cctor_started)) != 0)
                Fail("ShadowBaselineAlreadyInitialized", AssemblyShadowTypeKey::Format(&baseline->byval_arg) +
                    " Site=" + site, AssemblyShadowError::BaselineAlreadyUsed);
        }
    };
    // Exceptions used to report failure are unrelated BCL allocations. They
    // must remain constructible; poison rejection applies to Shadow proofs.
    return cache->Resolve<il2cpp::os::FastAutoLock>(key, &g_MetadataLock, build, validate);
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
            << ",\"guardFailures\":" << state.failures.load();
        assembly_shadow_r03::RuntimeProbe::ObserverSnapshot();
        assembly_shadow_r02::AppendDiagnostics(output);
        output << "}";
        json = output.str();
        return AssemblyShadowError::Success;
    }
    catch (const ShadowTypeResolutionFailure& error) { return error.error; }
    catch (const std::exception&) { return AssemblyShadowError::InternalError; }
}
}}
#endif