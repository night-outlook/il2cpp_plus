#pragma once
// R03 B2 INACTIVE. Read existing il2cpp_defaults class pointer ONLY.
// Null/missing default is a normal rejected source, never IL2CPP_ASSERT.
// Exact tag-to-field mapping must match the pinned original
// Class::FromIl2CppTypeEnum switch; the cross-repo source gate enforces that.
// No class initialization, cache lookup, allocation, metadata traversal,
// state change, terminal V2 authority, or retained class pointer.
// Requires the original Il2CppTypeEnum, Il2CppClass, Il2CppDefaults ABI.
namespace il2cpp { namespace vm { namespace assembly_shadow_existing_primitive_defaults {

inline Il2CppClass* Peek(Il2CppTypeEnum tag) noexcept {
    switch(tag) {
        case IL2CPP_TYPE_OBJECT: return il2cpp_defaults.object_class;
        case IL2CPP_TYPE_VOID: return il2cpp_defaults.void_class;
        case IL2CPP_TYPE_BOOLEAN: return il2cpp_defaults.boolean_class;
        case IL2CPP_TYPE_CHAR: return il2cpp_defaults.char_class;
        case IL2CPP_TYPE_I1: return il2cpp_defaults.sbyte_class;
        case IL2CPP_TYPE_U1: return il2cpp_defaults.byte_class;
        case IL2CPP_TYPE_I2: return il2cpp_defaults.int16_class;
        case IL2CPP_TYPE_U2: return il2cpp_defaults.uint16_class;
        case IL2CPP_TYPE_I4: return il2cpp_defaults.int32_class;
        case IL2CPP_TYPE_U4: return il2cpp_defaults.uint32_class;
        case IL2CPP_TYPE_I: return il2cpp_defaults.int_class;
        case IL2CPP_TYPE_U: return il2cpp_defaults.uint_class;
        case IL2CPP_TYPE_I8: return il2cpp_defaults.int64_class;
        case IL2CPP_TYPE_U8: return il2cpp_defaults.uint64_class;
        case IL2CPP_TYPE_R4: return il2cpp_defaults.single_class;
        case IL2CPP_TYPE_R8: return il2cpp_defaults.double_class;
        case IL2CPP_TYPE_STRING: return il2cpp_defaults.string_class;
        case IL2CPP_TYPE_TYPEDBYREF: return il2cpp_defaults.typed_reference_class;
        case IL2CPP_TYPE_IL2CPP_TYPE_INDEX: return il2cpp_defaults.systemtype_class;
        default: return nullptr;
    }
}
}}} // namespaces
