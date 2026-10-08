// Host metadata doubles execute the exact production terminal predicates.
// These records are NOT real MethodInfo values or Player acceptance evidence.
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
static size_t allocations;
void* operator new(std::size_t n) { ++allocations; if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
#if __cplusplus >= 201402L
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#endif

enum class AssemblyShadowState { Idle, Committed, Failed, FailedAfterCommit };
enum class AssemblyShadowError { Success, InternalError, BaselineMethodExecution, ModuleInitializerFailed };
enum {IL2CPP_TYPE_CLASS=1,IL2CPP_TYPE_VALUETYPE,IL2CPP_TYPE_GENERICINST,IL2CPP_TYPE_SZARRAY,IL2CPP_TYPE_PTR,
 IL2CPP_TYPE_BYREF,IL2CPP_TYPE_ARRAY,IL2CPP_TYPE_BOOLEAN,IL2CPP_TYPE_CHAR,IL2CPP_TYPE_I1,IL2CPP_TYPE_U1,
 IL2CPP_TYPE_I2,IL2CPP_TYPE_U2,IL2CPP_TYPE_I4,IL2CPP_TYPE_U4,IL2CPP_TYPE_I8,IL2CPP_TYPE_U8,
 IL2CPP_TYPE_R4,IL2CPP_TYPE_R8,IL2CPP_TYPE_I,IL2CPP_TYPE_U,IL2CPP_TYPE_OBJECT,IL2CPP_TYPE_STRING,IL2CPP_TYPE_VAR};
struct Il2CppType; struct Il2CppGenericClass;
struct Il2CppAssembly {int tag;};
struct Il2CppImage {const Il2CppAssembly* assembly;};
struct Il2CppGenericInst {uint32_t type_argc; const Il2CppType** type_argv;};
struct Il2CppGenericContext {const Il2CppGenericInst* class_inst; const Il2CppGenericInst* method_inst;};
struct Il2CppGenericClass {const Il2CppType* type;Il2CppGenericContext context;};
struct Il2CppArrayType {const Il2CppType* etype;uint8_t rank,numsizes,numlobounds;int* sizes;int* lobounds;};
struct Il2CppType {int type=IL2CPP_TYPE_I4;struct Data {const void* typeHandle=nullptr;const Il2CppType* type=nullptr;
 const Il2CppGenericClass* generic_class=nullptr;const Il2CppArrayType* array=nullptr;} data;};
struct Il2CppClass {const Il2CppImage* image; Il2CppType byval_arg;const Il2CppGenericClass* generic_class=nullptr;
 Il2CppClass(const Il2CppImage* i,Il2CppType t):image(i),byval_arg(t){} };
struct MethodInfo;
struct Il2CppGenericMethod {const MethodInfo* methodDefinition; Il2CppGenericContext context;};
struct MethodInfo {const Il2CppClass* klass;bool is_inflated=false;const Il2CppGenericMethod* genericMethod=nullptr;
 explicit MethodInfo(const Il2CppClass* k):klass(k){} };
struct CandidateRegistry {std::map<const Il2CppAssembly*,int> byAssembly;std::map<const void*,int> byTypeHandle;};
struct ActiveSnapshot {std::map<const Il2CppAssembly*,int> byAssembly,shadowToBaseline;};
struct TypeFailure {AssemblyShadowError error; std::string detail;};
static std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Idle};
static std::atomic<const TypeFailure*> s_typeFailure{nullptr};
static std::atomic<bool> s_unexpectedFailure{false},s_lateBaselineUse{false},s_referenceViolation{false};
static std::atomic<const CandidateRegistry*> s_candidates{nullptr};
static std::atomic<const ActiveSnapshot*> s_active{nullptr};
static bool metadataScope,stagingScope;
static int currentReads,visibleReads,exceptions;
struct Transaction {std::mutex mutex;AssemblyShadowError recoveryTerminalError=AssemblyShadowError::Success;std::string recoveryTerminalDetail;};
static Transaction tx;
Transaction& Current() {++currentReads;return tx;}
struct AssemblyShadow {static bool IsResolvingTypeMetadata(){return metadataScope;}};
struct StagingBridge {static bool IsStaging(){return stagingScope;}};
static std::set<const void*> privateHandles;
struct AssemblyShadowVisibility {static bool IsTypeVisible(const Il2CppType* t,uint64_t generation){++visibleReads;
 if(generation)throw std::logic_error("generation zero required");
 return !privateHandles.count(t->data.typeHandle);}};
struct ShadowTypeResolutionFailure:std::runtime_error {AssemblyShadowError error;
 ShadowTypeResolutionFailure(AssemblyShadowError e,const std::string& s):std::runtime_error(s),error(e){}};
struct Exception {static const char* GetInvalidOperationException(const char* text){++exceptions;return text;}
 static void Raise(const char* text){throw std::logic_error(text);}};
#include "vm/AssemblyShadowTerminalExecution.inc"

static int checks;
void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
Il2CppType cls(const void* handle){Il2CppType t;t.type=IL2CPP_TYPE_CLASS;t.data.typeHandle=handle;return t;}
void reset(){s_state=AssemblyShadowState::Idle;s_typeFailure=nullptr;s_unexpectedFailure=false;s_lateBaselineUse=false;
 s_referenceViolation=false;s_candidates=nullptr;s_active=nullptr;metadataScope=stagingScope=false;
 currentReads=visibleReads=exceptions=0;privateHandles.clear();tx.recoveryTerminalError=AssemblyShadowError::Success;tx.recoveryTerminalDetail.clear();}
int main(int argc,char** argv){try{
 check(argc==2,"case argument");std::string mode=argv[1];reset();
 Il2CppAssembly old{1},active{2},fixed{3},ordinary{4};Il2CppImage oi{&old},ai{&active},fi{&fixed},ui{&ordinary};
 int oh=1,ah=2,fh=3,uh=4,ph=5;Il2CppClass oc{&oi,cls(&oh)},ac{&ai,cls(&ah)},fc{&fi,cls(&fh)},uc{&ui,cls(&uh)};
 MethodInfo om{&oc},am{&ac},fm{&fc},um{&uc};CandidateRegistry candidates;ActiveSnapshot world;
 candidates.byAssembly[&old]=1;candidates.byTypeHandle[&oh]=1;
 world.byAssembly[&old]=1;world.shadowToBaseline[&active]=1;s_candidates=&candidates;s_active=&world;
 if(mode=="healthy"){
  check(!RejectTerminalMethod(&am)&&!RejectTerminalMethod(&om)&&!RejectTerminalMethod(&fm),"healthy predicate leaves existing identity validation in charge");
  check(!RejectTerminalMethod(nullptr),"healthy invalid-input policy remains original guard");check(currentReads==0&&visibleReads==0,"healthy no traversal or transaction lock");
 }else if(mode=="terminal"){
  for(auto state:{AssemblyShadowState::Failed,AssemblyShadowState::FailedAfterCommit}){s_state=state;check(RejectTerminalMethod(&am)&&RejectTerminalMethod(&om),"terminal related methods reject");
   check(!RejectTerminalMethod(&fm)&&!RejectTerminalMethod(&um),"fixed and ordinary unaffected");check(RejectTerminalMethod(nullptr),"null fail closed");}
 }else if(mode=="latched"){
  TypeFailure fail{AssemblyShadowError::BaselineMethodExecution,"first"};s_typeFailure=&fail;check(RejectTerminalMethod(&am),"failure publication before state");
  s_typeFailure=nullptr;s_unexpectedFailure=true;check(RejectTerminalMethod(&am),"allocation failure latch");s_unexpectedFailure=false;
  s_lateBaselineUse=true;check(RejectTerminalMethod(&am),"late use latch");s_lateBaselineUse=false;s_referenceViolation=true;check(RejectTerminalMethod(&am),"reference latch");
 }else if(mode=="generic"){
  s_state=AssemblyShadowState::FailedAfterCommit;Il2CppType arg=cls(&oh);const Il2CppType* args[]={&arg};Il2CppGenericInst inst{1,args};
  Il2CppGenericMethod gm{&fm,{nullptr,&inst}};MethodInfo method{&fc};method.is_inflated=true;method.genericMethod=&gm;
  check(RejectTerminalMethod(&method),"fixed generic owner captures obsolete baseline");arg=cls(&ah);privateHandles.insert(&ah);check(RejectTerminalMethod(&method),"fixed generic captures active shadow");
  arg=cls(&ph);privateHandles.insert(&ph);check(RejectTerminalMethod(&method),"fixed generic captures retained failed image");
  arg=cls(&fh);check(!RejectTerminalMethod(&method),"unrelated generic exception/diagnostic allowed");
  gm.context.class_inst=&inst;gm.context.method_inst=nullptr;arg=cls(&oh);check(RejectTerminalMethod(&method),"class context captured");
  gm.methodDefinition=&am;arg=cls(&fh);check(RejectTerminalMethod(&method),"related definition despite fixed inflated owner");
 }else if(mode=="shapes"){
  s_state=AssemblyShadowState::Failed;Il2CppType oldType=cls(&oh),fixedType=cls(&fh),nested;
  const Il2CppType* args[]={&nested};Il2CppGenericInst inst{1,args};Il2CppGenericMethod gm{&fm,{nullptr,&inst}};MethodInfo method{&fc};method.is_inflated=true;method.genericMethod=&gm;
  for(int kind:{IL2CPP_TYPE_SZARRAY,IL2CPP_TYPE_PTR,IL2CPP_TYPE_BYREF}){nested.type=kind;nested.data.type=&oldType;check(RejectTerminalMethod(&method),"wrapped captured old type");nested.data.type=&fixedType;check(!RejectTerminalMethod(&method),"wrapped fixed type");}
  Il2CppArrayType array{&oldType,2,0,0,nullptr,nullptr};nested.type=IL2CPP_TYPE_ARRAY;nested.data.array=&array;check(RejectTerminalMethod(&method),"array old");array.etype=&fixedType;check(!RejectTerminalMethod(&method),"array fixed");
  const Il2CppType* innerArgs[]={&oldType};Il2CppGenericInst inner{1,innerArgs};Il2CppGenericClass gen{&fixedType,{&inner,nullptr}};nested.type=IL2CPP_TYPE_GENERICINST;nested.data.generic_class=&gen;check(RejectTerminalMethod(&method),"nested generic old");innerArgs[0]=&fixedType;check(!RejectTerminalMethod(&method),"nested generic fixed");
 }else if(mode=="malformed"){
  s_state=AssemblyShadowState::Failed;Il2CppGenericMethod gm{&fm,{nullptr,nullptr}};MethodInfo method{&fc};method.is_inflated=true;
  check(RejectTerminalMethod(&method),"missing inflated context");method.genericMethod=&gm;gm.methodDefinition=&method;check(RejectTerminalMethod(&method),"self definition");
  gm.methodDefinition=&fm;Il2CppType arg;const Il2CppType* args[]={&arg};Il2CppGenericInst inst{0,args};gm.context.method_inst=&inst;check(RejectTerminalMethod(&method),"empty instance");inst.type_argc=1;arg.type=IL2CPP_TYPE_VAR;check(RejectTerminalMethod(&method),"unproved open type");
  arg.type=IL2CPP_TYPE_SZARRAY;arg.data.type=&arg;check(RejectTerminalMethod(&method),"recursive type bounded");
  inst.type_argc=2048;check(RejectTerminalMethod(&method),"argument budget");
 }else if(mode=="declaring"){
  s_state=AssemblyShadowState::Failed;Il2CppType oldType=cls(&oh),fixedType=cls(&fh);const Il2CppType* args[]={&oldType};Il2CppGenericInst inst{1,args};Il2CppGenericClass generic{&fixedType,{&inst,nullptr}};
  fc.generic_class=&generic;check(RejectTerminalMethod(&fm),"generic declaring class captured baseline");args[0]=&fixedType;check(!RejectTerminalMethod(&fm),"unrelated constructed declaring class");generic.context.class_inst=nullptr;check(RejectTerminalMethod(&fm),"unproved declaring construction");
 }else if(mode=="no-allocation"){
  s_state=AssemblyShadowState::Failed;size_t before=allocations;for(int i=0;i<10000;++i){check(RejectTerminalMethod(&am),"active");check(!RejectTerminalMethod(&fm),"fixed");}check(allocations==before,"boolean predicate allocates nothing");check(currentReads==0,"boolean predicate no tx lock");
 }else if(mode=="first-failure"){
  TypeFailure failure{AssemblyShadowError::BaselineMethodExecution,"original-method-failure"};s_typeFailure=&failure;s_state=AssemblyShadowState::FailedAfterCommit;
  for(int i=0;i<3;++i){try{RaiseTerminalMethodFailure();check(false,"must throw");}catch(const std::logic_error& e){check(std::string(e.what())==failure.detail,"original exception detail");}}
  check(s_typeFailure==&failure&&s_state==AssemblyShadowState::FailedAfterCommit,"first failure/state retained");check(currentReads==0,"existing failure read without tx lock");
 }else if(mode=="initializer"){
  s_state=AssemblyShadowState::FailedAfterCommit;tx.recoveryTerminalError=AssemblyShadowError::ModuleInitializerFailed;tx.recoveryTerminalDetail="original-initializer-failure";
  try{RaiseTerminalMethodFailure();check(false,"must throw");}catch(const std::logic_error& e){check(std::string(e.what())==tx.recoveryTerminalDetail,"initializer cause");}
  check(s_typeFailure==nullptr&&tx.recoveryTerminalError==AssemblyShadowError::ModuleInitializerFailed,"do not install InternalError over initializer");check(s_state==AssemblyShadowState::FailedAfterCommit,"state stable");
 }else if(mode=="private-scope"){
  s_state=AssemblyShadowState::Failed;metadataScope=true;std::lock_guard<std::mutex> lock(tx.mutex);
  try{RaiseTerminalMethodFailure();check(false,"native throw");}catch(const ShadowTypeResolutionFailure&){check(currentReads==0&&exceptions==0,"no recursive lock/managed construction");}
  metadataScope=false;stagingScope=true;TypeFailure failure{AssemblyShadowError::BaselineMethodExecution,"first"};s_typeFailure=&failure;
  try{RaiseTerminalMethodFailure();check(false,"native throw");}catch(const ShadowTypeResolutionFailure& e){check(e.error==failure.error&&std::string(e.what())==failure.detail,"first failure private reporting");}
 }else if(mode=="empty-world"){
  s_candidates=nullptr;s_active=nullptr;s_state=AssemblyShadowState::Failed;check(!RejectTerminalMethod(&fm)&&!RejectTerminalMethod(&um),"failed configuration does not globally block fixed code");
 }else throw std::runtime_error("unknown case");
 std::cout<<"{\"kind\":\"R03TerminalHostContract\",\"case\":\""<<mode<<"\",\"checks\":"<<checks<<",\"result\":\"Passed\",\"runtimeAcceptance\":false}\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
