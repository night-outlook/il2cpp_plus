#pragma once
// Test-only host integration candidate: replace the PlayerLoop through Unity's
// own native setter after the authenticated uncaught sentinel is reported.
// No method-admission exception, report-counter cap, sleep or finalizer switch.
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW && HYBRIDCLR_R03_RUNTIME_PROBE
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "il2cpp-tabledefs.h"
#include "gc/GCHandle.h"
#include "vm/Array.h"
#include "vm/Class.h"
#include "vm/InternalCalls.h"
#include "vm/MetadataCache.h"
#include "vm/AssemblyShadow.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#if IL2CPP_TARGET_OSX || IL2CPP_TARGET_LINUX
#include <dlfcn.h>
#endif
namespace il2cpp { namespace vm { namespace assembly_shadow_engine {
using SetLoop = void (*)(Il2CppArray*);
struct Disposition
{
    std::atomic<int> state{0}; // 0 unused, 1 prepared, 2 applying, 3 applied, -1 failed
    Il2CppArray* idle = nullptr;
    Il2CppException* sentinel = nullptr;
    uint32_t arrayHandle = 0, sentinelHandle = 0;
    SetLoop setter = nullptr;
    std::thread::id owner;
    void* data = nullptr;
    uint32_t size = 0;
    unsigned char preparedBytes[256] = {};
};
inline Disposition& Data() { static Disposition data; return data; }
inline bool EmptyField(Il2CppClass* element, const char* name, int type,
    const unsigned char* data, size_t size, size_t width)
{
    FieldInfo* field = Class::GetFieldFromName(element, name);
    if (!field || field->parent != element || !field->type || field->type->byref ||
        field->type->type != type || (field->type->attrs & FIELD_ATTRIBUTE_STATIC) ||
        field->offset < sizeof(Il2CppObject)) return false;
    const size_t offset = field->offset - sizeof(Il2CppObject);
    if (offset > size || width > size - offset) return false;
    for (size_t i=0; i<width; ++i) if (data[offset+i] != 0) return false;
    return true;
}
inline bool SetterOrigin(SetLoop setter)
{
#if IL2CPP_TARGET_OSX || IL2CPP_TARGET_LINUX
    Dl_info info{};
    if (!setter || !::dladdr(reinterpret_cast<void*>(setter), &info) || !info.dli_fname) return false;
    const char* name=std::strrchr(info.dli_fname,'/');name=name?name+1:info.dli_fname;
    return !std::strcmp(name,"UnityPlayer.dylib") || !std::strcmp(name,"UnityPlayer.so") || !std::strcmp(name,"UnityPlayer");
#else
    (void)setter; return false;
#endif
}
inline int Prepare(void* arrayHandle, void* sentinelHandle)
{
    auto& d=Data(); AssemblyShadowState current;
    if (d.state.load()!=0 || AssemblyShadow::GetState(current)!=AssemblyShadowError::Success ||
        current!=AssemblyShadowState::Committed) return -1;
    const uintptr_t ah=reinterpret_cast<uintptr_t>(arrayHandle), eh=reinterpret_cast<uintptr_t>(sentinelHandle);
    if (!ah || !eh || ah>UINT32_MAX || eh>UINT32_MAX) return -2;
    Il2CppObject* object=gc::GCHandle::GetTarget(static_cast<uint32_t>(ah));
    Il2CppObject* error=gc::GCHandle::GetTarget(static_cast<uint32_t>(eh));
    const Il2CppAssembly* core=MetadataCache::GetAotAssemblyByNamePhysical("UnityEngine.CoreModule");
    if (!core || !object || !error || !object->klass || object->klass->rank!=1 ||
        !object->klass->element_class || !error->klass) return -3;
    Il2CppClass* element=object->klass->element_class;
    if (element->image!=core->image || !element->namespaze || !element->name ||
        std::strcmp(element->namespaze,"UnityEngine.LowLevel") ||
        std::strcmp(element->name,"PlayerLoopSystemInternal") || element->generic_class ||
        AssemblyShadow::IsCandidate(core) || AssemblyShadow::IsActiveShadow(core) ||
        AssemblyShadow::IsShadowedBaseline(core)) return -4;
    Il2CppArray* array=reinterpret_cast<Il2CppArray*>(object);
    const size_t bytes=object->klass->element_size;
    if (array->bounds || array->max_length!=1 || bytes==0 || bytes>sizeof(d.preparedBytes)) return -5;
    const auto* data=reinterpret_cast<const unsigned char*>(Array::GetFirstElementAddress(array));
    if (!EmptyField(element,"type",IL2CPP_TYPE_CLASS,data,bytes,sizeof(void*)) ||
        !EmptyField(element,"updateDelegate",IL2CPP_TYPE_CLASS,data,bytes,sizeof(void*)) ||
        !EmptyField(element,"updateFunction",IL2CPP_TYPE_I,data,bytes,sizeof(void*)) ||
        !EmptyField(element,"loopConditionFunction",IL2CPP_TYPE_I,data,bytes,sizeof(void*)) ||
        !EmptyField(element,"numSubSystems",IL2CPP_TYPE_I4,data,bytes,sizeof(int32_t))) return -6;
    Il2CppClass* ancestor=error->klass;
    unsigned depth=0;
    while (ancestor && ancestor!=il2cpp_defaults.exception_class && depth++<64) ancestor=ancestor->parent;
    if (ancestor!=il2cpp_defaults.exception_class) return -7;
    SetLoop setter=reinterpret_cast<SetLoop>(InternalCalls::Resolve(
        "UnityEngine.LowLevel.PlayerLoop::SetPlayerLoopInternal(UnityEngine.LowLevel.PlayerLoopSystemInternal[])"));
    if (!SetterOrigin(setter)) return -8;
    const uint32_t ar=gc::GCHandle::New(object,true);
    if (!ar) return -9;
    const uint32_t er=gc::GCHandle::New(error,true);
    if (!er) {gc::GCHandle::Free(ar);return -10;}
    if (gc::GCHandle::GetTarget(ar)!=object || gc::GCHandle::GetTarget(er)!=error)
    {gc::GCHandle::Free(ar);gc::GCHandle::Free(er);return -11;}
    d.idle=array;d.sentinel=reinterpret_cast<Il2CppException*>(error);
    d.arrayHandle=ar;d.sentinelHandle=er;d.setter=setter;d.owner=std::this_thread::get_id();
    d.data=const_cast<unsigned char*>(data);d.size=static_cast<uint32_t>(bytes);
    std::memcpy(d.preparedBytes,data,bytes);
    d.state.store(1,std::memory_order_release);
    std::fprintf(stderr,"[R03EngineDisposition] prepared=1 arrayLength=1 size=%u setter=%p ownerThread=1\n",
        d.size,reinterpret_cast<void*>(setter));std::fflush(stderr);
    return 1;
}
inline int ValidateAfterHealthyGc()
{
    auto& d=Data();AssemblyShadowState current;
    if (d.state.load()!=1 || AssemblyShadow::GetState(current)!=AssemblyShadowError::Success ||
        current!=AssemblyShadowState::Committed || d.owner!=std::this_thread::get_id()) return -1;
    return gc::GCHandle::GetTarget(d.arrayHandle)==reinterpret_cast<Il2CppObject*>(d.idle) &&
        gc::GCHandle::GetTarget(d.sentinelHandle)==reinterpret_cast<Il2CppObject*>(d.sentinel) &&
        !std::memcmp(d.data,d.preparedBytes,d.size) ? 1 : -2;
}
inline void OnCompletedReport(const Il2CppException* error) noexcept
{
    auto& d=Data();
    if (d.state.load(std::memory_order_acquire)!=1 || !error || error!=d.sentinel) return;
    AssemblyShadowState current;
    if (AssemblyShadow::GetState(current)!=AssemblyShadowError::Success ||
        current!=AssemblyShadowState::FailedAfterCommit || d.owner!=std::this_thread::get_id())
    {d.state.store(-1);std::fputs("[R03EngineDisposition] failed=StateOrThread\n",stderr);std::fflush(stderr);return;}
    int prepared=1;
    if (!d.state.compare_exchange_strong(prepared,2)) return;
    if (!d.setter || !d.data || std::memcmp(d.data,d.preparedBytes,d.size))
    {d.state.store(-1);std::fputs("[R03EngineDisposition] failed=PreparedInputChanged\n",stderr);std::fflush(stderr);return;}
    try
    {
        // Direct call to the original, already-resolved Unity native setter.
        // No managed callback, metadata initialization or GCHandle operation.
        d.setter(d.idle);
        d.state.store(3,std::memory_order_release);
        std::fputs("[R03EngineDisposition] applied=1 setterReturned=1 emptyPlayerLoop=1\n",stderr);
    }
    catch (...) {d.state.store(-1);std::fputs("[R03EngineDisposition] failed=SetterException\n",stderr);}
    std::fflush(stderr);
}
inline int State() noexcept { return Data().state.load(std::memory_order_acquire); }
}}}
#endif
