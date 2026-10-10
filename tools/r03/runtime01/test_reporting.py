#!/usr/bin/env python3
"""Run the actual Runtime::Invoke body and production reporting header.

Only VM dependencies are mocked. These are host regressions, not Unity tests.
No production conditional test switch is used. Also test feature-OFF unchanged.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
STUB = r'''
#pragma once
#include <cstdint>
#include <utility>
#include <cstring>
#define METHOD_ATTRIBUTE_STATIC 16
#define IL2CPP_TYPE_CLASS 18
#define IL2CPP_TYPE_BOOLEAN 2
struct Il2CppType { struct { const void* typeHandle=nullptr; } data; int type=18; bool byref=false, valuetype=false; };
struct Il2CppAssembly;
struct Il2CppImage { const Il2CppAssembly* assembly=nullptr; uint32_t typeCount=1; const void* handle=nullptr; const char* ns=nullptr; const char* name=nullptr; };
struct Il2CppAssembly { Il2CppImage* image=nullptr; };
struct Il2CppClass { Il2CppImage* image=nullptr; const char* name=nullptr; const char* namespaze=nullptr; void* generic_class=nullptr; Il2CppType byval_arg; Il2CppClass* parent=nullptr; bool cctor_finished_or_no_cctor=true; };
struct Il2CppObject { Il2CppClass* klass=nullptr; };
struct Il2CppString { int32_t length=0; uint16_t chars[4]={}; };
struct Il2CppException { Il2CppObject object; Il2CppString* message=nullptr; };
struct MethodInfo { const char* name=nullptr; Il2CppClass* klass=nullptr; bool is_inflated=false,is_generic=false; int flags=16; int parameters_count=2; const Il2CppType** parameters=nullptr; const Il2CppType* return_type=nullptr; };
struct Il2CppExceptionWrapper { Il2CppException* ex; };
struct Defaults { Il2CppClass* exception_class=nullptr; Il2CppClass* boolean_class=nullptr; };
inline Defaults il2cpp_defaults;
inline bool denied=false;
inline int managedBodies=0, boxes=0, firstFailure=73;
inline Il2CppImage coreImage, corlibImage, impostorImage;
inline Il2CppAssembly engineAssembly;
inline Il2CppObject boxedTrue, boxedFalse;
inline Il2CppException rejection;
namespace il2cpp { namespace gc { struct WriteBarrier {
 template<typename T> static void GenericStoreNull(T** p) { *p=nullptr; }
 template<typename T> static void GenericStore(T** p,T* v) { *p=v; }
}; } namespace vm {
enum class AssemblyShadowState { Committed=6, FailedAfterCommit=9 };
enum class BaselineUseKind { ClassInit };
struct AssemblyShadow {
 static bool AssertMethodIsActive(const MethodInfo*,const char*) noexcept { return !denied; }
 static void RequireUserCodeAllowed() {}
 static const MethodInfo* ResolveReflectionMethod(const MethodInfo* m) { return m; }
 static void RequireActiveMethod(const MethodInfo*,const char*) { if(denied) throw Il2CppExceptionWrapper{&rejection}; }
 static void RequireActiveClass(const Il2CppClass*,BaselineUseKind,const char*) {}
 static void GetState(AssemblyShadowState& s) { s=denied?AssemblyShadowState::FailedAfterCommit:AssemblyShadowState::Committed; }
};
struct MetadataCache {
 static const Il2CppAssembly* GetAotAssemblyByNamePhysical(const char* n) { return !std::strcmp(n,"UnityEngine.CoreModule")?&engineAssembly:nullptr; }
 static const void* GetAssemblyTypeHandle(const Il2CppImage* image,uint32_t) { return image->handle; }
 static std::pair<const char*,const char*> GetTypeNamespaceAndName(const void* h) {
  if(h==coreImage.handle) return {coreImage.ns,coreImage.name};
  if(h==corlibImage.handle) return {corlibImage.ns,corlibImage.name};
  return {"Wrong","Type"};
 }
};
struct Image { static const Il2CppImage* GetCorlib(){return &corlibImage;} };
struct Object { static Il2CppObject* Box(Il2CppClass*,void* p){++boxes;return *static_cast<bool*>(p)?&boxedTrue:&boxedFalse;} };
struct Runtime {
 static Il2CppObject* Invoke(const MethodInfo*,void*,void**,Il2CppException**);
 static Il2CppObject* InvokeWithThrow(const MethodInfo*,void*,void**) { ++managedBodies;return &boxedFalse; }
 static void ClassInit(Il2CppClass*) {}
};
}}
'''
MAIN = r'''
#include <cassert>
#include <iostream>
using il2cpp::vm::Runtime;
int main() {
 int h1=0,h2=0;
 engineAssembly.image=&coreImage;coreImage.assembly=&engineAssembly;
 coreImage.handle=&h1;coreImage.ns="UnityEngine";coreImage.name="Object";
 corlibImage.handle=&h2;corlibImage.ns="System";corlibImage.name="Exception";
 Il2CppClass cls;cls.image=&coreImage;cls.name="Debug";cls.namespaze="UnityEngine";
 Il2CppType arg0,arg1,ret;arg0.data.typeHandle=&h2;arg1.data.typeHandle=&h1;ret.type=IL2CPP_TYPE_BOOLEAN;
 const Il2CppType* args[]={&arg0,&arg1};
 MethodInfo m;m.name="CallOverridenDebugHandler";m.klass=&cls;m.parameters=args;m.return_type=&ret;
 Il2CppException* exc=&rejection;void* values[]={nullptr,nullptr};
 assert(Runtime::Invoke(&m,nullptr,values,&exc)==&boxedFalse && !exc && managedBodies==1);
 denied=true;
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
 auto rejected=[&](){exc=nullptr;int before=managedBodies;assert(!Runtime::Invoke(&m,nullptr,values,&exc));assert(exc==&rejection && managedBodies==before);};
 int before=managedBodies;
 for(int i=0;i<20000;++i) {exc=&rejection;assert(Runtime::Invoke(&m,nullptr,values,&exc)==&boxedTrue && !exc);}
 assert(managedBodies==before && firstFailure==73);
 // No permission is granted to a same-named callback with different identity/signature.
 m.name="Business";rejected();m.name="CallOverridenDebugHandler";
 cls.image=&impostorImage;rejected();cls.image=&coreImage;
 cls.name="Other";rejected();cls.name="Debug";
 cls.namespaze="User";rejected();cls.namespaze="UnityEngine";
 m.is_inflated=true;rejected();m.is_inflated=false;
 m.is_generic=true;rejected();m.is_generic=false;
 cls.generic_class=&h1;rejected();cls.generic_class=nullptr;
 m.flags=0;rejected();m.flags=16;
 m.parameters_count=1;rejected();m.parameters_count=2;
 ret.type=18;rejected();ret.type=2;
 ret.byref=true;rejected();ret.byref=false;
 arg0.data.typeHandle=&h1;rejected();arg0.data.typeHandle=&h2;
 arg1.byref=true;rejected();arg1.byref=false;
 coreImage.name="NotObject";rejected();coreImage.name="Object";
 Il2CppObject instance;instance.klass=&cls;exc=nullptr;
 assert(!Runtime::Invoke(&m,&instance,values,&exc)&&exc==&rejection);
 assert(Runtime::Invoke(&m,nullptr,values,nullptr)==&boxedTrue);
 m.name="Business";assert(!Runtime::Invoke(&m,nullptr,values,nullptr));
 assert(firstFailure==73 && managedBodies==1);
 std::cout << "{\"result\":\"Passed\",\"feature\":\"ON\",\"repeatReports\":20000,\"managedBodiesAfterPoison\":0,\"negativeIdentities\":15,\"unityRun\":false}\n";
#else
 assert(Runtime::Invoke(&m,nullptr,values,&exc)==&boxedFalse&&!exc&&managedBodies==2);
 std::cout << "{\"result\":\"Passed\",\"feature\":\"OFF\",\"unityRun\":false}\n";
#endif
}
'''

def main():
    source=(ROOT/'libil2cpp/vm/Runtime.cpp').read_text()
    start=source.index('    Il2CppObject* Runtime::Invoke(const MethodInfo *method,')
    end=source.index('    Il2CppObject* Runtime::InvokeWithThrow(',start)
    body=source[start:end]
    assert 'assembly_shadow_reporting::TryHandle' in body
    assert 'RequireActiveMethod(method, "Runtime::Invoke")' in body
    assert body.index('TryHandle') < body.index('RequireActiveMethod')
    cc=shutil.which('clang++') or shutil.which('g++')
    assert cc
    rows=[]
    with tempfile.TemporaryDirectory() as td:
        root=Path(td)
        (root/'stub.h').write_text(STUB)
        for name in ('il2cpp-config.h','il2cpp-class-internals.h','il2cpp-object-internals.h','vm/AssemblyShadow.h','vm/Image.h','vm/MetadataCache.h','vm/Object.h'):
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stub.h"\n')
        header=ROOT/'libil2cpp/vm/AssemblyShadowTerminalReporting.h'
        program='#include "stub.h"\n#include "'+str(header)+'"\nnamespace il2cpp { namespace vm {\n'+body+'\n}}\n'+MAIN
        (root/'invoke.cpp').write_text(program)
        for enabled in (1,0):
            exe=root/('test-'+str(enabled))
            cmd=[cc,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-DHYBRIDCLR_ENABLE_ASSEMBLY_SHADOW='+str(enabled),'-I'+str(root),str(root/'invoke.cpp'),'-o',str(exe)]
            subprocess.run(cmd,check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,check=True,timeout=30)
            data=json.loads(run.stdout)
            assert data['result']=='Passed'
            if enabled: assert run.stderr.count('[AssemblyShadowTerminalReport]')==32
            print(run.stdout.strip());rows.append(data)
    print(json.dumps({'result':'Passed','productionRuntimeBlobSha256':hashlib.sha256(source.encode()).hexdigest(),'configurations':rows,'dependencyModel':'VM mocks; actual production Runtime::Invoke body and reporting header','unityRun':False},sort_keys=True))

if __name__=='__main__':main()
