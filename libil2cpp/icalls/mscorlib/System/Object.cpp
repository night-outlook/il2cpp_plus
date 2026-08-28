#include "il2cpp-config.h"
#include "il2cpp-object-internals.h"
#include "il2cpp-class-internals.h"
#include "icalls/mscorlib/System/Object.h"
#include "vm/Object.h"
#include "vm/AssemblyShadow.h"
#include "vm/Reflection.h"
#include "vm/Exception.h"

namespace il2cpp
{
namespace icalls
{
namespace mscorlib
{
namespace System
{
    int32_t Object::InternalGetHashCode(Il2CppObject* obj)
    {
        return il2cpp::vm::Object::GetHash(obj);
    }

    Il2CppObject* Object::MemberwiseClone(Il2CppObject* obj)
    {
        return il2cpp::vm::Object::Clone(obj);
    }

    Il2CppReflectionType* Object::GetType(Il2CppObject* obj)
    {
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
        // An object's actual class is never a remappable handle.
        vm::AssemblyShadow::RequireActiveClass(obj->klass, vm::BaselineUseKind::TypeReflection, "Object.GetType.actual");
#endif
        return il2cpp::vm::Reflection::GetTypeObject(&obj->klass->byval_arg);
    }
} /* namespace System */
} /* namespace mscorlib */
} /* namespace icalls */
} /* namespace il2cpp */
