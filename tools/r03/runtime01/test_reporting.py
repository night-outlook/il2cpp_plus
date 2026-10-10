#!/usr/bin/env python3
"""Actual Runtime::Invoke body/header with mocked VM dependencies; not Unity.

Covers healthy/OFF, terminal reporting, exact-identity negatives, actual header
message reading from an Il2CppObject base and repeated bounded native logging.
"""
import hashlib
import json
import os
import signal
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
struct Il2CppClass { Il2CppImage* image=nullptr; const char* name=nullptr; const char* namespaze=nullptr; void* generic_class=nullptr; Il2CppType byval_arg; Il2CppClass* parent=nullptr; bool cctor_finished_or_no_cctor=true,initialized_and_no_error=true,has_references=false,has_finalize=false; uint32_t instance_size=16; };
struct Il2CppObject { Il2CppClass* klass=nullptr; };
struct Il2CppString { int32_t length=0; uint16_t chars[2052]={}; };
// Mirror the SDK base-class relationship, not the old incorrect named member.
struct Il2CppException : Il2CppObject { Il2CppString* message=nullptr; };
struct MethodInfo { const char* name=nullptr; Il2CppClass* klass=nullptr; bool is_inflated=false,is_generic=false; int flags=16; int parameters_count=2; const Il2CppType** parameters=nullptr; const Il2CppType* return_type=nullptr; };
struct Il2CppExceptionWrapper { Il2CppException* ex; };
struct Defaults { Il2CppClass* exception_class=nullptr; Il2CppClass* boolean_class=nullptr; };
inline Defaults il2cpp_defaults;
inline bool denied=false, candidate=false, shadowed=false, active=false;
inline int allocationMode=0;
inline int managedBodies=0, boxes=0, firstFailure=73;
inline Il2CppImage coreImage, corlibImage, impostorImage;
inline Il2CppAssembly engineAssembly;
struct MockBox { Il2CppObject header; bool value=false; };
inline MockBox boxStorage;
inline Il2CppObject& boxedTrue=boxStorage.header;
inline Il2CppObject boxedFalse;
inline Il2CppException rejection;
namespace il2cpp { namespace gc { struct WriteBarrier {
 template<typename T> static void GenericStoreNull(T** p) { *p=nullptr; }
 template<typename T> static void GenericStore(T** p,T* v) { *p=v; }
}; } namespace vm {
enum class AssemblyShadowState { Committed=6, FailedAfterCommit=9 };
enum class BaselineUseKind { ClassInit };
struct AssemblyShadow {
 static bool AssertMethodIsActive(const MethodInfo*,const char*) noexcept { return !denied; }
 static bool IsCandidate(const Il2CppAssembly*) { return candidate; }
 static bool IsShadowedBaseline(const Il2CppAssembly*) { return shadowed; }
 static bool IsActiveShadow(const Il2CppAssembly*) { return active; }
 static uint64_t ActiveGeneration() { return 2; }
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
struct Class { static void Init(Il2CppClass*) {} };
// Intentionally no Object::Box or Object::New API exists in this dependency model.
namespace assembly_shadow_reporting { inline Il2CppObject* FixedHandledResult() noexcept; }
struct Object {
public:
 static void* Unbox(Il2CppObject*) { return &boxStorage.value; }
private:
 friend Il2CppObject* assembly_shadow_reporting::FixedHandledResult() noexcept;
 static Il2CppObject* NewPtrFree(Il2CppClass* c) {
  ++boxes;
  if(allocationMode==1) throw Il2CppExceptionWrapper{&rejection};
  if(allocationMode==2) return nullptr;
  boxedTrue.klass=c;return &boxedTrue;
 }
};
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
#include <sys/resource.h>
using il2cpp::vm::Runtime;
int main(int argc,char** argv) {
 rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);
 int h1=0,h2=0;
 engineAssembly.image=&coreImage;coreImage.assembly=&engineAssembly;
 coreImage.handle=&h1;coreImage.ns="UnityEngine";coreImage.name="Object";
 corlibImage.handle=&h2;corlibImage.ns="System";corlibImage.name="Exception";
 Il2CppClass cls;cls.image=&coreImage;cls.name="Debug";cls.namespaze="UnityEngine";
 Il2CppType arg0,arg1,ret;arg0.data.typeHandle=&h2;arg1.data.typeHandle=&h1;ret.type=IL2CPP_TYPE_BOOLEAN;
 const Il2CppType* args[]={&arg0,&arg1};
 MethodInfo m;m.name="CallOverridenDebugHandler";m.klass=&cls;m.parameters=args;m.return_type=&ret;
 Il2CppClass boolean;boolean.image=&corlibImage;boolean.name="Boolean";boolean.namespaze="System";
 boolean.byval_arg.valuetype=true;boolean.byval_arg.type=IL2CPP_TYPE_BOOLEAN;
 il2cpp_defaults.boolean_class=&boolean;
 Il2CppException* exc=&rejection;void* values[]={nullptr,nullptr};
 (void)argc;(void)argv;
 assert(Runtime::Invoke(&m,nullptr,values,&exc)==&boxedFalse && !exc && managedBodies==1);
 denied=true;
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
 if(argc>1) {
  if(!std::strcmp(argv[1],"throw")) allocationMode=1;
  else if(!std::strcmp(argv[1],"null")) allocationMode=2;
  else if(!std::strcmp(argv[1],"references")) boolean.has_references=true;
  else if(!std::strcmp(argv[1],"foreign")) boolean.image=&impostorImage;
  else if(!std::strcmp(argv[1],"cctor")) boolean.cctor_finished_or_no_cctor=false;
  else if(!std::strcmp(argv[1],"small")) boolean.instance_size=0;
  else if(!std::strcmp(argv[1],"large")) boolean.instance_size=65536;
  else if(!std::strcmp(argv[1],"reentry")) il2cpp::vm::assembly_shadow_reporting::TransportActive()=true;
  else if(!std::strcmp(argv[1],"metadata-error")) boolean.initialized_and_no_error=false;
  else return 99;
  Runtime::Invoke(&m,nullptr,values,&exc);return 100; // Must terminate, never recurse.
 }
 Il2CppClass exceptionClass,derivedClass;derivedClass.parent=&exceptionClass;
 il2cpp_defaults.exception_class=&exceptionClass;
 Il2CppString message;message.length=4;
 message.chars[0]='A';message.chars[1]='"';message.chars[2]='\n';message.chars[3]=0x4e2d;
 Il2CppException original;original.klass=&derivedClass;original.message=&message;
 values[0]=&original;
 auto rejected=[&](){exc=nullptr;int before=managedBodies;assert(!Runtime::Invoke(&m,nullptr,values,&exc));assert(exc==&rejection && managedBodies==before);};
 int before=managedBodies;
 for(int i=0;i<20000;++i) {exc=&rejection;assert(Runtime::Invoke(&m,nullptr,values,&exc)==&boxedTrue && !exc);}
 assert(managedBodies==before && firstFailure==73 && boxStorage.value);
 assert(il2cpp::vm::assembly_shadow_reporting::CompletedCounter().load()==20000);
 assert(!il2cpp::vm::assembly_shadow_reporting::TransportActive());
 // Directly exercise message bounds and invalid/null exception arguments.
 message.length=2050;
 for(int i=0;i<2050;++i)message.chars[i]='x';
 il2cpp::vm::assembly_shadow_reporting::WriteMessage(&original);
 original.klass=&cls;
 il2cpp::vm::assembly_shadow_reporting::WriteMessage(&original);
 il2cpp::vm::assembly_shadow_reporting::WriteMessage(nullptr);
 // No same-named callback with different identity/signature is admitted.
 m.name="Business";rejected();m.name="CallOverridenDebugHandler";
 candidate=true;rejected();candidate=false;
 shadowed=true;rejected();shadowed=false;
 active=true;rejected();active=false;
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
 std::cout << "{\"result\":\"Passed\",\"feature\":\"ON\",\"repeatReports\":20000,\"managedBodiesAfterPoison\":0,\"negativeIdentities\":18,\"exceptionMessageCases\":4,\"unityRun\":false}\n";
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
    cc=shutil.which('clang++') or shutil.which('g++');assert cc
    rows=[]
    sanitizers=os.environ.get('R03_SANITIZERS')=='1'
    with tempfile.TemporaryDirectory() as td:
        root=Path(td);(root/'stub.h').write_text(STUB)
        for name in ('il2cpp-config.h','il2cpp-class-internals.h','il2cpp-object-internals.h','il2cpp-tabledefs.h','vm/AssemblyShadow.h','vm/Image.h','vm/MetadataCache.h','vm/Object.h','vm/Class.h'):
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stub.h"\n')
        header=ROOT/'libil2cpp/vm/AssemblyShadowTerminalReporting.h'
        program='#include "stub.h"\n#include "'+str(header)+'"\nnamespace il2cpp { namespace vm {\n'+body+'\n}}\n'+MAIN
        (root/'invoke.cpp').write_text(program)
        for enabled in (1,0):
            exe=root/('test-'+str(enabled))
            cmd=[cc,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-DHYBRIDCLR_ENABLE_ASSEMBLY_SHADOW='+str(enabled),'-I'+str(root),str(root/'invoke.cpp'),'-o',str(exe)]
            if sanitizers:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            subprocess.run(cmd,check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,check=True,timeout=30)
            data=json.loads(run.stdout);assert data['result']=='Passed'
            if enabled:
                assert run.stderr.count('[AssemblyShadowTerminalReport]')==32
                assert 'A\\u0022\\u000a\\u4e2d' in run.stderr
                assert '[truncated]' in run.stderr and 'NonExceptionArgument' in run.stderr
                fatal=[]
                for mode in ('throw','null','references','foreign','cctor','small','metadata-error','large','reentry'):
                    death=subprocess.run([str(exe),mode],capture_output=True,text=True,timeout=10)
                    assert death.returncode == -signal.SIGABRT,(mode,death.returncode,death.stderr)
                    assert ('fatal=NativeReportingReentry' if mode == 'reentry' else 'fatal=FixedBooleanAllocationFailed') in death.stderr
                    fatal.append(mode)
                data['controlledNativeFatalCases']=fatal
            print(run.stdout.strip());rows.append(data)
    print(json.dumps({'result':'Passed','productionRuntimeSha256':hashlib.sha256(source.encode()).hexdigest(),'configurations':rows,'dependencyModel':'VM mocks; actual Runtime::Invoke and reporting header, not a Unity execution','unityRun':False,'sanitizers':sanitizers},sort_keys=True))

if __name__=='__main__':main()
