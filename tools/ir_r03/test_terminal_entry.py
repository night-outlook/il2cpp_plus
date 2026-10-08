#!/usr/bin/env python3
"""Compile production guard fragments with explicit borrowed-metadata adapters.

No Unity, IL2CPP ABI or Player proof is claimed. Each scenario is a fresh process.
"""
import argparse, hashlib, json, os, pathlib, subprocess
PREFIX = r'''
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdlib>
using Il2CppMetadataTypeHandle = const void*;
enum Kind { IL2CPP_TYPE_CLASS=1,IL2CPP_TYPE_VALUETYPE,IL2CPP_TYPE_GENERICINST,IL2CPP_TYPE_SZARRAY,
 IL2CPP_TYPE_PTR,IL2CPP_TYPE_ARRAY,IL2CPP_TYPE_VAR,IL2CPP_TYPE_MVAR,IL2CPP_TYPE_BOOLEAN,IL2CPP_TYPE_CHAR,
 IL2CPP_TYPE_I1,IL2CPP_TYPE_U1,IL2CPP_TYPE_I2,IL2CPP_TYPE_U2,IL2CPP_TYPE_I4,IL2CPP_TYPE_U4,IL2CPP_TYPE_I8,
 IL2CPP_TYPE_U8,IL2CPP_TYPE_R4,IL2CPP_TYPE_R8,IL2CPP_TYPE_I,IL2CPP_TYPE_U,IL2CPP_TYPE_OBJECT,IL2CPP_TYPE_STRING };
struct Il2CppType; struct Il2CppGenericClass;
struct Il2CppGenericInst { uint32_t type_argc; const Il2CppType** type_argv; };
struct Il2CppGenericContext { const Il2CppGenericInst* class_inst=nullptr; const Il2CppGenericInst* method_inst=nullptr; };
struct Il2CppArrayType { uint32_t rank=1, numsizes=0,numlobounds=0; int* sizes=nullptr; int* lobounds=nullptr; const Il2CppType* etype=nullptr; };
struct Il2CppType { int type=IL2CPP_TYPE_CLASS; struct { Il2CppMetadataTypeHandle typeHandle=nullptr;
 const Il2CppType* type=nullptr; const Il2CppArrayType* array=nullptr; const Il2CppGenericClass* generic_class=nullptr; } data; };
struct Il2CppGenericClass { const Il2CppType* type=nullptr; Il2CppGenericContext context; };
struct Il2CppTypeDefinition { int32_t byvalTypeIndex=1; };
struct Il2CppAssembly { struct { const char* name; } aname; };
struct Il2CppImage { const Il2CppAssembly* assembly; };
const Il2CppImage* coreImage=nullptr;
struct Image { static const Il2CppImage* GetCorlib(){return coreImage;} };
struct Il2CppClass { const Il2CppImage* image; const Il2CppGenericClass* generic_class=nullptr;
 const char* namespaze="Fixture"; const char* name="Node"; explicit Il2CppClass(const Il2CppImage* p):image(p){} };
struct MethodInfo;
struct Il2CppGenericMethod { const MethodInfo* methodDefinition=nullptr; Il2CppGenericContext context; };
struct MethodInfo { Il2CppClass* klass; const char* name="Run"; bool is_inflated=false; const Il2CppGenericMethod* genericMethod=nullptr; explicit MethodInfo(Il2CppClass* p):klass(p){} };
namespace hybridclr { namespace metadata { bool IsInterpreterType(const Il2CppTypeDefinition* p){return p->byvalTypeIndex < -1;} } }
enum class AssemblyShadowState { Disabled=0,CandidatesRegistered=1,Staging=2,Staged=3,Validated=4,Committing=5,Committed=6,Aborted=7,Failed=8,FailedAfterCommit=9 };
enum class AssemblyShadowError { Success=0,InvalidArgument=3,InvalidState=2,BaselineAlreadyUsed=7,ResourceAbiMismatch=16,ModuleInitializerFailed=17,BaselineMethodExecution=21,InternalError=23 };
enum class BaselineUseKind { MethodExecution };
struct UseRecord { bool present=false; BaselineUseKind kind; char detail[160]={}; const Il2CppClass* klass=nullptr; uint64_t thread=0,timestamp=0,sequence=0; };
struct Candidate { const char* name="Business"; const Il2CppAssembly* baseline=nullptr; UseRecord firstUse; };
struct CandidateRegistry { std::unordered_map<const Il2CppAssembly*,Candidate*> byAssembly; std::unordered_map<Il2CppMetadataTypeHandle,Candidate*> byTypeHandle; };
template<class T> struct NameIndex { std::vector<std::pair<const char*,T>> entries;
 T Find(const char* n)const{for(const auto& e:entries)if(n&&!std::strcmp(n,e.first))return e.second;return nullptr;} };
struct StableAotConfiguration { NameIndex<const Il2CppAssembly*> stableByName; };
struct ActiveSnapshot { std::unordered_map<const Il2CppAssembly*,const Il2CppAssembly*> byAssembly, shadowToBaseline; };
struct TypeFailure { AssemblyShadowError error; std::string detail; };
std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Disabled};
std::atomic<const TypeFailure*> s_typeFailure{nullptr};
std::atomic<bool> s_unexpectedFailure{false}, s_lateBaselineUse{false};
std::atomic<const StableAotConfiguration*> s_configuration{nullptr};
std::atomic<const CandidateRegistry*> s_candidates{nullptr};
std::atomic<const ActiveSnapshot*> s_active{nullptr};
std::atomic<uint64_t> s_methodChecks{0},s_shadowMethodChecks{0},s_rejectedBaselineMethods{0};
uint64_t s_usageGeneration=0;
struct Transaction { std::mutex mutex; std::string recoveryTerminalDetail; } transaction;
Transaction& Current(){return transaction;}
struct UsageLock { UsageLock(){} };
const Il2CppAssembly* PhysicalMethodAssembly(const MethodInfo* m){return m&&m->klass&&m->klass->image?m->klass->image->assembly:nullptr;}
void ObserveExecutionClass(Il2CppClass*){}
void CopyDetail(char* dst,size_t cap,const char* src){std::snprintf(dst,cap,"%s",src?src:"");}
namespace assembly_shadow_r02 { enum class Metric { GenericContextChecks }; struct ObservationCounters { static void Add(Metric,int){} }; }
struct ShadowTypeResolutionFailure: std::runtime_error { AssemblyShadowError error; ShadowTypeResolutionFailure(AssemblyShadowError e,const std::string& d):std::runtime_error(d),error(e){} };
bool privateScope=false;
struct StagingBridge { static bool IsStaging(){return privateScope;} };
struct AssemblyShadowTypeResolver { static void CountGuardFailure(){} };
struct Exception { static const char* GetInvalidOperationException(const char* d){return d;} [[noreturn]]static void Raise(const char* d){throw std::runtime_error(d);} };
struct AssemblyShadow {
 static bool AssertMethodIsActive(const MethodInfo*,const char*) noexcept;
 static void RequireActiveMethod(const MethodInfo*,const char*);
 static void RequireUserCodeAllowed();
 static bool IsResolvingTypeMetadata(){return privateScope;}
 static void FailTypeResolution(AssemblyShadowError,const std::string&);
};
'''
MAIN = r'''
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("scenario required");
 std::string scenario=argv[1]; int checks=0;
 auto check=[&](bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);};
 Il2CppAssembly business{{"Business"}},active{{"Business"}},fixed{{"mscorlib"}},lookalike{{"mscorlib"}},ordinary{{"Ordinary"}};
 Il2CppImage bi{&business},ai{&active},fi{&fixed},li{&lookalike},oi{&ordinary};coreImage=&fi;
 Il2CppClass bc{&bi},ac{&ai},fc{&fi},lc{&li},oc{&oi};
 MethodInfo baseline{&bc},method{&ac},diagnostic{&fc},spoof{&lc},normal{&oc};
 Candidate candidate; candidate.baseline=&business;
 CandidateRegistry candidates; candidates.byAssembly[&business]=&candidate;
 Il2CppTypeDefinition oldDef,activeDef,fixedDef; activeDef.byvalTypeIndex=-2;
 candidates.byTypeHandle[&oldDef]=&candidate;
 StableAotConfiguration configuration;configuration.stableByName.entries.push_back({"mscorlib",&fixed});
 ActiveSnapshot snapshot;snapshot.byAssembly[&business]=&active;snapshot.shadowToBaseline[&active]=&business;
 s_configuration.store(&configuration);s_candidates.store(&candidates);s_active.store(&snapshot);s_state.store(AssemblyShadowState::Committed);
 check(AssemblyShadow::AssertMethodIsActive(&method,"positive"),"active target must pass before poison");
 check(AssemblyShadow::AssertMethodIsActive(&diagnostic,"positive"),"fixed diagnostic positive");
 check(AssemblyShadow::AssertMethodIsActive(&normal,"positive"),"ordinary positive");
 if(scenario=="healthy"){check(TerminalMethodEntryAllowed(nullptr,nullptr),"no global poison when disabled/healthy");}
 else {
  if(scenario=="baseline")check(!AssemblyShadow::AssertMethodIsActive(&baseline,"first-baseline"),"old baseline must poison");
  else if(scenario=="type"){try{AssemblyShadow::FailTypeResolution(AssemblyShadowError::ResourceAbiMismatch,"first-type");}catch(const std::exception&){} }
  else if(scenario=="initializer"){transaction.recoveryTerminalDetail="original initializer failure";s_state.store(AssemblyShadowState::FailedAfterCommit);}
  else if(scenario=="precommit"){s_active.store(nullptr);transaction.recoveryTerminalDetail="original precommit failure";s_state.store(AssemblyShadowState::Failed);method=baseline;}
  else if(scenario=="latched"){s_typeFailure.store(new TypeFailure{AssemblyShadowError::ResourceAbiMismatch,"latched-before-state"});check(!AssemblyShadow::AssertMethodIsActive(&method,"latched-before-state"),"latched failure ignored before state publication");s_state.store(AssemblyShadowState::FailedAfterCommit);}
  else if(scenario=="unexpected"){s_unexpectedFailure.store(true);}
  else if(scenario=="generic"){
   Il2CppType oldType;oldType.data.typeHandle=&oldDef;const Il2CppType* args[]={&oldType};Il2CppGenericInst inst{1,args};
   Il2CppGenericMethod gm;gm.methodDefinition=&diagnostic;gm.context.method_inst=&inst;
   MethodInfo closed=diagnostic;closed.is_inflated=true;closed.genericMethod=&gm;
   check(!AssemblyShadow::AssertMethodIsActive(&closed,"first-captured"),"captured argument must poison");
  } else throw std::runtime_error("unknown scenario");
  const auto beforeState=s_state.load();const auto* beforeCause=s_typeFailure.load();const auto* beforeActive=s_active.load();
  const auto usage=s_usageGeneration;
  for(int i=0;i<8;i++)check(!AssemblyShadow::AssertMethodIsActive(&method,"post-poison"),"valid active entry after poison");
  bool threw=false;try{AssemblyShadow::RequireActiveMethod(&method,"post-poison-wrapper");}catch(const std::exception& ex){threw=true;if(!beforeCause&&!transaction.recoveryTerminalDetail.empty())check(ex.what()==transaction.recoveryTerminalDetail,"lost transaction-only cause");}
  check(threw,"throwing wrapper continued");check(s_typeFailure.load()==beforeCause,"first diagnostic pointer changed");
  check(s_state.load()==beforeState&&s_active.load()==beforeActive,"state or active world changed");
  check(s_usageGeneration==usage,"terminal check recorded new use");
  check(AssemblyShadow::AssertMethodIsActive(&diagnostic,"fixed-diagnostic"),"fixed diagnostic unavailable");
  check(!AssemblyShadow::AssertMethodIsActive(&spoof,"name-only-diagnostic"),"same-name physical spoof allowed");
  check(!AssemblyShadow::AssertMethodIsActive(&normal,"ordinary-during-terminal"),"unapproved interpreter continued terminal transaction");
  Il2CppType activeType;activeType.data.typeHandle=&activeDef;const Il2CppType* args[]={&activeType};Il2CppGenericInst inst{1,args};
  Il2CppGenericMethod gm;gm.methodDefinition=&diagnostic;gm.context.method_inst=&inst;
  MethodInfo closed=diagnostic;closed.is_inflated=true;closed.genericMethod=&gm;
  check(!AssemblyShadow::AssertMethodIsActive(&closed,"fixed-captures-active"),"fixed wrapper with active captured type permitted");
  activeType.data.typeHandle=&oldDef;
  check(!AssemblyShadow::AssertMethodIsActive(&closed,"fixed-captures-candidate"),"fixed wrapper with baseline captured type permitted");
  activeType.data.typeHandle=&fixedDef;
  check(AssemblyShadow::AssertMethodIsActive(&closed,"fixed-captures-fixed"),"independent fixed generic diagnostic rejected");
  s_configuration.store(nullptr);threw=false;
  try{AssemblyShadow::RequireActiveMethod(&method,"missing-config");}
  catch(const ShadowTypeResolutionFailure&){threw=true;}
  check(threw,"terminal missing-config did not unwind natively");
  s_configuration.store(&configuration);
  privateScope=true;threw=false;try{AssemblyShadow::RequireActiveMethod(&diagnostic,"metadata");}catch(const std::exception&){threw=true;}privateScope=false;
  check(threw,"fixed diagnostic bypassed metadata-only scope");check(s_typeFailure.load()==beforeCause,"diagnostic checks changed first cause");
 }
 std::printf("{\"result\":\"Passed\",\"checks\":%d,\"scenario\":\"%s\",\"playerRun\":false}\n",checks,argv[1]);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
'''
def fragment(text,first,last):
    if text.count(first)!=1:raise ValueError('Unique source anchor: '+first)
    start=text.index(first);return text[start:text.index(last,start+len(first))]
def generated(text):
    regions=[fragment(text,'    void RecordFirstGuardFailure(','    Transaction& Current()'),
      fragment(text,'    struct CapturedArgumentFailure','    bool Interpreter(const Il2CppAssembly* assembly)'),
      fragment(text,'    bool HasTerminalExecutionFailure()','    // Physical tables and raw TypeDef handles only; never materialize a class.\n    AssemblyShadowError BuildCandidateRegistry'),
      fragment(text,'bool AssemblyShadow::AssertMethodIsActive(','void AssemblyShadow::ObserveClassCctorStarted('),
      fragment(text,'void AssemblyShadow::RequireUserCodeAllowed()','Il2CppClass* AssemblyShadow::ResolveClassDefinition(')]
    return PREFIX+'\n'.join(regions)+MAIN

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--native',type=pathlib.Path,required=True);ap.add_argument('--output',type=pathlib.Path,required=True)
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    data=(a.native/'libil2cpp/vm/AssemblyShadow.cpp').read_bytes();cpp=a.output/'actual-guard-fragments.cpp';cpp.write_text(generated(data.decode()))
    results=[];commands=[]
    for std in ('c++11','c++17'):
        exe=a.output/('guard-'+std)
        cmd=['clang++','-std='+std,'-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(exe)]
        r=subprocess.run(cmd,capture_output=True,text=True);commands.append({'argv':cmd,'exit':r.returncode})
        (a.output/(std+'-compiler.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr)
        for scenario in ('healthy','baseline','generic','type','initializer','precommit','latched','unexpected'):
            r=subprocess.run([str(exe),scenario],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True)
            (a.output/(std+'-'+scenario+'.log')).write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError(scenario+': '+r.stderr)
            results.append(dict(configuration=std,**json.loads(r.stdout)))
    report={'kind':'IRR03ActualGuardFragmentTests','result':'Passed','sourceSha256':hashlib.sha256(data).hexdigest(),
      'testSha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'cases':results,'commands':commands,
      'boundary':'Actual source functions; explicit borrowed-metadata/exception adapters; not full IL2CPP ABI or Player execution.',
      'playerRun':False,'runtimeAcceptance':False}
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'result':'Passed','cases':len(results),'checks':sum(r['checks'] for r in results)}))
if __name__=='__main__':main()
