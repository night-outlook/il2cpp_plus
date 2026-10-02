#pragma once
// Pinned BCL adapter for R03 diagnostics; included only in the probe profile.
// No reflection, Class::Init/SetupFields, allocation, or name resolution occurs.
#include "AssemblyShadowRuntimeProbe.h"
#include "il2cpp-object-internals.h"
#include "il2cpp-class-internals.h"
#include <cstring>
namespace il2cpp { namespace vm { namespace assembly_shadow_r03 {
inline bool BclClass(const Il2CppClass* klass, const char* ns, const char* name)
{
    return klass && klass->image && klass->image->assembly &&
        klass->image->assembly->aname.name && klass->namespaze && klass->name &&
        !std::strcmp(klass->image->assembly->aname.name, "mscorlib") &&
        !std::strcmp(klass->namespaze, ns) && !std::strcmp(klass->name, name);
}
inline const MethodInfo* ArrayPoolCallback(Il2CppObject* object)
{
    if (!object || !BclClass(object->klass, "System", "Gen2GcCallback") ||
        !object->klass->initialized || !object->klass->fields) return nullptr;
    const FieldInfo* callbackField = nullptr;
    for (uint32_t i = 0; i < object->klass->field_count; ++i)
    {
        const FieldInfo* field = object->klass->fields + i;
        if (field->name && !std::strcmp(field->name, "_callback"))
        { if (callbackField) return nullptr; callbackField = field; }
    }
    if (!callbackField || !callbackField->type || callbackField->type->type != IL2CPP_TYPE_GENERICINST ||
        (callbackField->type->attrs & 0x0010) || // FIELD_ATTRIBUTE_STATIC
        callbackField->offset < static_cast<int32_t>(sizeof(Il2CppObject)) ||
        static_cast<uint32_t>(callbackField->offset) > object->klass->instance_size ||
        object->klass->instance_size - static_cast<uint32_t>(callbackField->offset) < sizeof(void*)) return nullptr;
    // Copy an object reference, never reinterpret the field value as an integer
    // or invoke its delegate. A valid finalizable object keeps this field alive.
    Il2CppDelegate* callback = nullptr;
    std::memcpy(&callback, reinterpret_cast<const char*>(object) + callbackField->offset, sizeof(callback));
    if (!callback || !callback->object.klass ||
        callback->object.klass->parent != il2cpp_defaults.multicastdelegate_class) return nullptr;
    const MethodInfo* method = callback->method;
    if (!method || !BclClass(method->klass, "System.Buffers", "TlsOverPerCoreLockedStacksArrayPool`1") ||
        !method->klass->generic_class || method->parameters_count != 1 ||
        !method->return_type || method->return_type->type != IL2CPP_TYPE_BOOLEAN ||
        !method->name || std::strcmp(method->name, "Gen2GcCallbackFunc") ||
        !(method->flags & 0x0010)) return nullptr; // METHOD_ATTRIBUTE_STATIC
    return method;
}
}}}
