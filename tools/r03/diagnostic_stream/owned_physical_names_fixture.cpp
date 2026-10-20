// R03 P1: compile the ACTUAL owned-name header and source method below.
// VM allocator, metadata lifetime and iterator are controlled host mocks.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include "AssemblyShadowExecutionRegistrationWitness.h"
#include "AssemblyShadowPhysicalMembership.h"
#include "AssemblyShadowOwnedPhysicalNames.h"

struct Il2CppAssembly { struct {const char* name;} aname; };
struct Il2CppImage {Il2CppAssembly* assembly;};
struct Il2CppClass {Il2CppImage* image;const char* namespaze;const char* name;};
namespace il2cpp {namespace vm {
struct ActiveSnapshot{uint64_t generation;};
enum class AssemblyShadowState {Disabled,Committed,Failed};
static ActiveSnapshot active{1},secondary{2};
static std::atomic<const ActiveSnapshot*> s_active{&active};
static std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Committed};
static std::array<Il2CppClass*,2048> s_executionClasses{};
static size_t s_executionClassCount=0;
struct Revision{uint64_t value=0;uint64_t Current()const{return value;}void Bump(){++value;}};
static Revision s_executionRegistryRevision;
static assembly_shadow_execution_witness::Table s_executionRegistrationWitnesses;
static bool admissionOpen=true;
static size_t readers=0,observationLockDepth=0,metadataLockDepth=0;
static int g_MetadataLock=0;
static std::vector<Il2CppClass*> physicalEnumeration;
static std::function<void()> afterMetadataRelease;
namespace assembly_shadow_metadata_lifetime {
struct Gate{struct Reader{
 bool valid;
 explicit Reader(Gate&):valid(admissionOpen){if(valid)++readers;}
 ~Reader(){if(valid)--readers;}
 explicit operator bool()const noexcept{return valid;}
};};
static Gate g_MetadataLifetimeGate;
}
struct ExecutionObservationLock{
 ExecutionObservationLock(){if(metadataLockDepth)throw std::runtime_error("observation nested with metadata");++observationLockDepth;}
 ~ExecutionObservationLock(){--observationLockDepth;}
};
void il2cpp_class_for_each(void(*callback)(Il2CppClass*,void*),void*context){
 if(metadataLockDepth!=1||observationLockDepth)throw std::runtime_error("incorrect enum lock");
 for(auto* c:physicalEnumeration)callback(c,context);
}
class AssemblyShadow{public:
 static bool TryCaptureOwnedObservedPhysicalNames(
  assembly_shadow_physical_membership::Scratch&,
  assembly_shadow_owned_physical_names::Capture&,
  assembly_shadow_owned_physical_names::Receipt&);
};
}}
namespace il2cpp {namespace os {
struct FastAutoLock{
 explicit FastAutoLock(int*){
  if(vm::observationLockDepth)throw std::runtime_error("nested metadata lock");
  ++vm::metadataLockDepth;
 }
 ~FastAutoLock(){
  --vm::metadataLockDepth;
  if(!vm::metadataLockDepth&&vm::afterMetadataRelease)vm::afterMetadataRelease();
 }
};
}}
namespace il2cpp {namespace vm {
#include "AssemblyShadowOwnedPhysicalNamesSource.inl"
}}
using namespace il2cpp::vm;
namespace owned=il2cpp::vm::assembly_shadow_owned_physical_names;
namespace physical=il2cpp::vm::assembly_shadow_physical_membership;
namespace witness=il2cpp::vm::assembly_shadow_execution_witness;
static Il2CppClass classes[1024]{};
static Il2CppImage images[1024]{};
static Il2CppAssembly assemblies[1024]{};
static char mutableName[64]="MutableBefore";
static char longName[4100]{};
static char bigName[4100]{};
static void require(bool value,const char*what){if(!value)throw std::runtime_error(what);}
static size_t slot(Il2CppClass* c){return (reinterpret_cast<uintptr_t>(c)>>3)%s_executionClasses.size();}
static void reset(){
 s_active.store(&active);s_state.store(AssemblyShadowState::Committed);
 s_executionClasses.fill(nullptr);s_executionClassCount=0;
 s_executionRegistryRevision=Revision{};
 s_executionRegistrationWitnesses=witness::Table{};
 admissionOpen=true;readers=observationLockDepth=metadataLockDepth=0;
 physicalEnumeration.clear();afterMetadataRelease=nullptr;
 std::fill(std::begin(classes),std::end(classes),Il2CppClass{});
 for(size_t i=0;i<1024;++i){
  assemblies[i].aname.name="PhysicalAssembly";
  images[i].assembly=&assemblies[i];
  classes[i].image=&images[i];classes[i].name="Class";classes[i].namespaze="Namespace";
 }
 std::strcpy(mutableName,"MutableBefore");
 std::fill(std::begin(longName),std::end(longName),'Q'); longName[4097]='\0';
 std::fill(std::begin(bigName),std::end(bigName),'R'); bigName[4000]='\0';
}
static void publish(Il2CppClass* c,witness::Source role=witness::Source::Candidate){
 size_t at=slot(c);
 while(s_executionClasses[at])at=(at+1)%s_executionClasses.size();
 require(s_executionRegistrationWitnesses.Register(at,c,role),"witness publish");
 s_executionClasses[at]=c;++s_executionClassCount;s_executionRegistryRevision.Bump();
}
static std::string value(owned::TextView t){return std::string(t.data,t.bytes);}
static void test(const char*label,bool expected,std::function<void()> arrange={},
                 std::function<void(const owned::Capture&,const owned::Receipt&)> inspect={}){
 reset();if(arrange)arrange();
 physical::Scratch scratch;owned::Capture names;owned::Receipt receipt;
 const bool ok=AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,receipt);
 require(ok==expected,"unexpected method verdict");
 require(!readers&&!observationLockDepth&&!metadataLockDepth,"VM lock/gate leaked");
 require(!receipt.classPointerOwnershipProven&&!receipt.metadataPointerLifetimeProven&&
         !receipt.completeExecutionTypeKeys&&!receipt.coherentVmEpochProven&&
         !receipt.playerExecution&&!receipt.runtimeAcceptance,"false acceptance");
 if(ok){
  require(names.Complete()&&receipt.namesCopiedFromMatchedEnumeratorCallbacks,"owned capture missing");
  require(names.Count()==receipt.observedClasses && names.Count()==receipt.matchedClasses,
          "row count mismatched observed identities");
  require(receipt.ownedRows==names.Count()&&receipt.ownedTextBytes==names.TextBytes(),
          "receipt does not match owned source");
  require(scratch.Observed()==0,"original pointer scratch not cleared");
 }else{
  require(!names.Complete()&&!receipt.namesCopiedFromMatchedEnumeratorCallbacks,
          "partial source promoted to a successful result");
  require(scratch.Observed()==0,"failure retained pointer table");
 }
 if(inspect)inspect(names,receipt);
 std::cout<<"PASS "<<label<<"\n";
}
int main(){try{
 test("empty-physical-owned",true,{},[](const auto&n,const auto&){require(n.Count()==0,"empty name count");});
 test("one-physical-owned",true,[]{publish(&classes[0]);physicalEnumeration={&classes[0]};},
     [](const auto&n,const auto&){owned::RowView row{};require(n.Get(0,row),"missing owned row");
        require(value(row.physicalAssemblyName)=="PhysicalAssembly"&&
                value(row.nameSpace)=="Namespace"&&value(row.simpleName)=="Class","wrong names");});
 test("two-roles-owned",true,[]{publish(&classes[0]);publish(&classes[1],witness::Source::ActiveShadow);
                            physicalEnumeration={&classes[0],&classes[1]};},
     [](const auto&n,const auto&){require(n.Count()==2,"role-owned name count");});
 test("duplicate-enumerated-owned-once",true,[]{publish(&classes[0]);physicalEnumeration={&classes[0],&classes[0]};},
     [](const auto&n,const auto&){require(n.Count()==1,"duplicate row copied");});
 test("unrelated-malformed-class-not-dereferenced",true,[]{publish(&classes[0]);
       classes[1].image=nullptr;physicalEnumeration={&classes[1],&classes[0]};},
     [](const auto&n,const auto&){require(n.Count()==1,"unrelated row copied");});
 test("null-namespace-copied-as-empty",true,[]{publish(&classes[0]);classes[0].namespaze=nullptr;
       physicalEnumeration={&classes[0]};},
     [](const auto&n,const auto&){owned::RowView row{};require(n.Get(0,row)&&row.nameSpace.bytes==0,"null namespace incorrect");});
 test("owned-bytes-survive-vm-change",true,[]{assemblies[0].aname.name=mutableName;
       publish(&classes[0]);physicalEnumeration={&classes[0]};},
     [](const auto&n,const auto&){std::strcpy(mutableName,"ChangedLater");
       owned::RowView row{};require(n.Get(0,row)&&value(row.physicalAssemblyName)=="MutableBefore",
       "retained borrowed name pointer");});
 test("missing-physical-row-refused",false,[]{publish(&classes[0]);});
 test("bad-matched-image-refused",false,[]{publish(&classes[0]);classes[0].image=nullptr;
       physicalEnumeration={&classes[0]};});
 test("bad-matched-assembly-refused",false,[]{publish(&classes[0]);images[0].assembly=nullptr;
       physicalEnumeration={&classes[0]};});
 test("bad-matched-assembly-name-refused",false,[]{publish(&classes[0]);assemblies[0].aname.name=nullptr;
       physicalEnumeration={&classes[0]};});
 test("bad-matched-simple-name-refused",false,[]{publish(&classes[0]);classes[0].name=nullptr;
       physicalEnumeration={&classes[0]};});
 test("field-overlong-refused",false,[]{publish(&classes[0]);classes[0].name=longName;
       physicalEnumeration={&classes[0]};});
 test("owned-text-budget-refused",false,[]{for(size_t i=0;i<70;++i){
       publish(&classes[i]);classes[i].name=bigName;physicalEnumeration.push_back(&classes[i]);}});
 test("source-revision-changed-after-iteration",false,[]{publish(&classes[0]);
       physicalEnumeration={&classes[0]};afterMetadataRelease=[](){s_executionRegistryRevision.Bump();};});
 test("state-changed-after-iteration",false,[]{publish(&classes[0]);physicalEnumeration={&classes[0]};
       afterMetadataRelease=[](){s_state.store(AssemblyShadowState::Failed);};});
 test("active-changed-after-iteration",false,[]{publish(&classes[0]);physicalEnumeration={&classes[0]};
       afterMetadataRelease=[](){s_active.store(&secondary);};});
 test("closed-admission-refused",false,[]{admissionOpen=false;});
 test("unknown-callback-null-refused",false,[]{publish(&classes[0]);physicalEnumeration={nullptr,&classes[0]};});
 reset();{
  physical::Scratch scratch;owned::Capture names;owned::Receipt receipt;
  require(AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,receipt),"first empty capture failed");
  require(!AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,receipt),"reused capture accepted");
  require(receipt.error==owned::Error::ReusedCapture,"wrong reused capture error");
  std::cout<<"PASS "<<"reused-owned-capture-refused"<<"\n";
 }
 std::cout<<"P1 OWNED PHYSICAL NAMES SOURCE HOST PASS\n";
 return 0;
 }catch(const std::exception&err){std::cerr<<"FAILED: "<<err.what()<<"\n";return 1;}
}
