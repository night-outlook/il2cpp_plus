#!/usr/bin/env python3
"""Actual fixed-rejection, Rethrow and unhandled-delegate source; VM dependencies mocked.
No Unity execution, no original K result promotion, no finalizer/GC disabling.
"""
import hashlib,json,os,signal,shutil,subprocess,tempfile
from pathlib import Path
from test_reporting import ROOT, STUB


def section(text,start,end):
    a=text.index(start); b=text.index(end,a); return text[a:b]


def main():
    vm=ROOT/'libil2cpp/vm'
    exception=(vm/'Exception.cpp').read_text();runtime=(vm/'Runtime.cpp').read_text()
    shadow=(vm/'AssemblyShadow.cpp').read_text()
    reraise=section(exception,'    NORETURN void Exception::Rethrow(', '    NORETURN void Exception::RaiseOutOfMemoryException(')
    unhandled=section(runtime,'    void Runtime::UnhandledException(', '    static inline Il2CppObject* InvokeConvertThis(')
    delegate=section(runtime,'    void Runtime::CallUnhandledExceptionDelegate(', '    static baselib::ReentrantLock s_TypeInitializationLock')
    failure=section(shadow,'void AssemblyShadow::FailTypeResolution(', 'Il2CppClass* AssemblyShadow::ResolveClassDefinition(')
    assert 'RethrowTerminalRejection();' in failure
    assert 'PrepareExceptionForThrow' not in reraise
    assert unhandled.index('TryReportRejectedUnhandled') < unhandled.index('Domain::GetCurrent')
    assert 'utils::Runtime::Abort();' in delegate
    stub=STUB.replace('#include <cstdint>','#include <cstdint>\n#include <string>\n#include <atomic>\n#define NORETURN [[noreturn]]\n#define IL2CPP_NOT_IMPLEMENTED_NO_ASSERT(...)')
    stub=stub.replace('struct Il2CppExceptionWrapper { Il2CppException* ex; };','struct Il2CppExceptionWrapper { Il2CppException* ex; explicit Il2CppExceptionWrapper(Il2CppException* e):ex(e){} };')
    stub=stub.replace('struct Defaults {','struct FieldInfo {}; inline FieldInfo rootField;\nstruct Il2CppDelegate : Il2CppObject {};\nstruct Il2CppDomain { Il2CppObject* domain=nullptr; };\ninline Il2CppDomain rootDomain; inline Il2CppDelegate rootDelegate;\ninline int fieldReads=0,delegateCalls=0,eventConstructions=0;\nstruct Defaults { Il2CppClass* appdomain_class=nullptr; Il2CppClass* threadabortexception_class=nullptr;')
    stub=stub.replace('enum class AssemblyShadowState','enum class AssemblyShadowError { InternalError=1, ReferenceResolutionFailed=13 };\nstruct ShadowTypeResolutionFailure { ShadowTypeResolutionFailure(AssemblyShadowError,const std::string&){} };\ninline bool resolving=false,staging=false;\ninline std::atomic<void*> s_active{nullptr};\ninline std::atomic<int> guardFailures{0};\nstruct AssemblyShadowTypeResolver { static void CountGuardFailure(){++guardFailures;} };\nstruct StagingBridge { static bool IsStaging(){return staging;} };\ninline void RecordFirstGuardFailure(AssemblyShadowError,const std::string&){denied=true;}\nenum class AssemblyShadowState')
    stub=stub.replace('struct AssemblyShadow {','inline std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Disabled};\nstruct AssemblyShadow {\n static bool IsResolvingTypeMetadata(){return resolving;}\n static void FailTypeResolution(AssemblyShadowError,const std::string&);')
    stub=stub.replace('struct Class { static void Init','struct Domain {static Il2CppDomain* GetCurrent(){++fieldReads;return &rootDomain;} static Il2CppDomain* GetRoot(){return &rootDomain;} };\nstruct Field {static void GetValue(Il2CppObject*,FieldInfo*,Il2CppObject** result){++fieldReads;*result=&rootDelegate;} };\nstruct Class { static FieldInfo* GetFieldFromName(Il2CppClass*,const char*){++fieldReads;return &rootField;} static void Init')
    stub=stub.replace('[[noreturn]] static void Rethrow(Il2CppException* e) { throw Il2CppExceptionWrapper{e}; }','[[noreturn]] static void Rethrow(Il2CppException*);')
    stub=stub.replace('struct Runtime {','struct Runtime {\n static void UnhandledException(Il2CppException*);\n static void CallUnhandledExceptionDelegate(Il2CppDomain*,Il2CppDelegate*,Il2CppException*);\n static Il2CppObject* CreateUnhandledExceptionEventArgs(Il2CppException*){++eventConstructions;return &boxedFalse;}\n static Il2CppObject* DelegateInvoke(Il2CppDelegate*,void**,Il2CppException** e){++delegateCalls;*e=denied?&rejection:nullptr;return nullptr;}')
    stub+='\nnamespace il2cpp { namespace utils {struct Runtime {[[noreturn]] static void Abort(){std::abort();}};} }\n'
    main_code=r'''
#include <cassert>
#include <thread>
#include <vector>
#include <iostream>
#include <sys/resource.h>
using namespace il2cpp::vm;
using namespace il2cpp::vm::assembly_shadow_reporting;
int main(int argc,char**argv){
 rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);
 (void)argc;(void)argv;
 InitializeTerminalRejection();
 assert(rejectionConstructors==1 && handleCreates==1 && handleReads==1);
 Il2CppException ordinary;ordinary.klass=&rejectionClass;
 assert(!IsReservedRejection(&ordinary));
 assert(!TryReportRejectedUnhandled(&reservedRejection)); // healthy is not exempt
 Runtime::UnhandledException(&ordinary);assert(delegateCalls==1 && eventConstructions==1);
 started=true;denied=true;
 if(argc>1){
  if(!std::strcmp(argv[1],"ordinary-unhandled")){Runtime::UnhandledException(&ordinary);return 100;}
  if(!std::strcmp(argv[1],"missing")){RejectionReserve().object.store(nullptr);RethrowTerminalRejection();return 101;}
  if(!std::strcmp(argv[1],"corrupt")){reservedRejection.klass=nullptr;RethrowTerminalRejection();return 102;}
 }
 int fields=fieldReads,delegates=delegateCalls,events=eventConstructions;
 std::vector<std::thread> threads;
 for(int t=0;t<4;++t)threads.emplace_back([](){for(int i=0;i<1000;++i){
  bool caught=false;try{RethrowTerminalRejection();}catch(const Il2CppExceptionWrapper& e){caught=e.ex==&reservedRejection;}
  assert(caught);Runtime::UnhandledException(&reservedRejection);
 }});
 for(auto& t:threads)t.join();
 assert(fieldReads==fields && delegateCalls==delegates && eventConstructions==events);
 assert(RejectionReserve().throws==4000 && RejectionReserve().unhandledReports==4000);
 assert(rejectionConstructors==1 && handleCreates==1 && handleReads==1);
 bool caught=false; s_active.store(&rootField);
 try{AssemblyShadow::FailTypeResolution(AssemblyShadowError::ReferenceResolutionFailed,"original");}
 catch(const Il2CppExceptionWrapper& e){caught=e.ex==&reservedRejection;}
 assert(caught && s_state==AssemblyShadowState::FailedAfterCommit && guardFailures==1);
 resolving=true;caught=false;
 try{AssemblyShadow::FailTypeResolution(AssemblyShadowError::ReferenceResolutionFailed,"native-scope");}
 catch(const ShadowTypeResolutionFailure&){caught=true;}
 assert(caught && RejectionReserve().throws==4001);resolving=false;
 ShutdownTerminalRejection();assert(handleFrees==1 && !RejectionReserve().object.load());
 started=false;denied=false;InitializeTerminalRejection();assert(rejectionConstructors==2);
 ShutdownTerminalRejection();assert(handleFrees==2);
 std::cout<<"{\"result\":\"Passed\",\"actualRethrowAndUnhandledBodies\":true,\"concurrentRejections\":4000,\"postTerminalConstructors\":0,\"postTerminalAppDomainDelegates\":0,\"unityRun\":false}\n";
}
'''
    cc=shutil.which('clang++') or shutil.which('g++');assert cc
    sanitizer=os.environ.get('R03_SANITIZERS')=='1'
    with tempfile.TemporaryDirectory() as td:
        root=Path(td).resolve();(root/'stub.h').write_text(stub)
        for name in ('il2cpp-config.h','il2cpp-class-internals.h','il2cpp-object-internals.h','gc/GCHandle.h','vm/AssemblyShadow.h','vm/Exception.h','vm/Image.h'):
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stub.h"\n')
        body='#include "stub.h"\n#include "'+str(vm/'AssemblyShadowTerminalRejection.h')+'"\nnamespace il2cpp { namespace vm {\n'+reraise+failure+unhandled+delegate+'\n}}\n'+main_code
        (root/'main.cpp').write_text(body);exe=root/'main'
        cmd=[cc,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-pthread','-DHYBRIDCLR_ENABLE_ASSEMBLY_SHADOW=1','-I'+str(root),str(root/'main.cpp'),'-o',str(exe)]
        if sanitizer:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        build=subprocess.run(cmd,capture_output=True,text=True,timeout=40);assert build.returncode==0,build.stderr
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);assert run.returncode==0,(run.returncode,run.stderr)
        print(run.stdout.strip())
        negative=[]
        for mode in ('ordinary-unhandled','missing','corrupt'):
            n=subprocess.run([str(exe),mode],capture_output=True,text=True,timeout=10)
            assert n.returncode==-signal.SIGABRT,(mode,n.returncode,n.stderr);negative.append(mode)
        print(json.dumps({'result':'Passed','sanitizers':sanitizer,'expectedNativeAbortControls':negative,'sourceSha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [vm/'Exception.cpp',vm/'Runtime.cpp',vm/'AssemblyShadow.cpp',vm/'AssemblyShadowTerminalRejection.h']},'scope':'Actual source functions; metadata/GC/runtime dependencies mocked; no Unity'}))
if __name__=='__main__':main()
