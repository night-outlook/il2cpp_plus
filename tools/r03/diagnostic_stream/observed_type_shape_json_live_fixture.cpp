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
#include "AssemblyShadowObservedTypeShapeCensus.h"
#include "AssemblyShadowObservedTypeShapeJson.h"
#include <memory>

struct Il2CppAssembly { struct {const char* name;} aname; };
struct Il2CppImage {Il2CppAssembly* assembly;};
static constexpr uint8_t IL2CPP_TYPE_VALUETYPE=0x11;
static constexpr uint8_t IL2CPP_TYPE_CLASS=0x12;
struct Il2CppClass {
    Il2CppImage* image=nullptr;
    const char* namespaze=nullptr;
    const char* name=nullptr;
    struct {uint8_t type=IL2CPP_TYPE_CLASS;} byval_arg{};
    void* typeMetadataHandle=nullptr;
    Il2CppClass* declaringType=nullptr;
    void* generic_class=nullptr;
    uint8_t rank=0;
    bool is_generic=false;
};
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
static size_t readers=0,observationLockDepth=0,metadataLockDepth=0,iteratorCalls=0;
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
 ++iteratorCalls;
 for(auto* c:physicalEnumeration)callback(c,context);
}
class AssemblyShadow{public:
 static bool TryCaptureOwnedObservedPhysicalNames(
  assembly_shadow_physical_membership::Scratch&,
  assembly_shadow_owned_physical_names::Capture&,
  assembly_shadow_owned_physical_names::Receipt&);
 static bool TryCaptureObservedTypeShapeCensus(
  assembly_shadow_physical_membership::Scratch&,
  assembly_shadow_owned_physical_names::Capture&,
  assembly_shadow_owned_physical_names::Receipt&,
  assembly_shadow_observed_type_shape_census::Result&);
 static bool TryCaptureObservedTypeShapeJson(
  assembly_shadow_physical_membership::Scratch&,
  assembly_shadow_owned_physical_names::Capture&,
  assembly_shadow_owned_physical_names::Receipt&,
  assembly_shadow_observed_type_shape_census::Result&,
  assembly_shadow_observed_type_shape_json::Record&,
  assembly_shadow_observed_type_shape_json::Receipt&,size_t);
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
#include "AssemblyShadowObservedTypeShapeLiveSource.inl"
#include "AssemblyShadowObservedTypeShapeJsonLiveSource.inl"
}}
using namespace il2cpp::vm;
namespace owned=il2cpp::vm::assembly_shadow_owned_physical_names;
namespace physical=il2cpp::vm::assembly_shadow_physical_membership;
namespace witness=il2cpp::vm::assembly_shadow_execution_witness;
static Il2CppClass classes[2050]{};
static Il2CppImage images[2050]{};
static Il2CppAssembly assemblies[2050]{};
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
 admissionOpen=true;readers=observationLockDepth=metadataLockDepth=iteratorCalls=0;
 physicalEnumeration.clear();afterMetadataRelease=nullptr;
 std::fill(std::begin(classes),std::end(classes),Il2CppClass{});
 for(size_t i=0;i<2050;++i){
  assemblies[i].aname.name="PhysicalAssembly";
  images[i].assembly=&assemblies[i];
  classes[i].image=&images[i];classes[i].name="Class";classes[i].namespaze="Namespace";
  classes[i].typeMetadataHandle=&classes[i];
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
  require(receipt.allRowsBoundToObservedRegistry &&
          receipt.boundRegistrationRows==names.Count(),"owned binding count missing");
  for(size_t rowIndex=0;rowIndex<names.Count();++rowIndex){
   owned::RowView row{};require(names.Get(rowIndex,row),"bound row unavailable");
   require(row.registrySlot<physical::kSlots,"invalid bound registry slot");
   witness::Source observed=witness::Source::None;
   require(s_executionRegistrationWitnesses.Matches(row.registrySlot,
           s_executionClasses[row.registrySlot],observed),"binding witness not current");
   require(observed==row.registrationSource && observed!=witness::Source::None,
           "row binding role differs from observed registry witness");
  }
  require(receipt.ownedRows==names.Count()&&receipt.ownedTextBytes==names.TextBytes(),
          "receipt does not match owned source");
  require(scratch.Observed()==0,"original pointer scratch not cleared");
 }else{
  require(!names.Complete()&&!receipt.namesCopiedFromMatchedEnumeratorCallbacks &&
          !receipt.allRowsBoundToObservedRegistry && receipt.boundRegistrationRows==0,
          "partial source promoted to a successful result");
  require(scratch.Observed()==0,"failure retained pointer table");
 }
 if(inspect)inspect(names,receipt);
 std::cout<<"PASS "<<label<<"\n";
}
namespace census=il2cpp::vm::assembly_shadow_observed_type_shape_census;
namespace shape=il2cpp::vm::assembly_shadow_observed_type_shape;
static void shapeCase(const char*label,shape::Category expected,
    std::function<void()> arrange={}){
    reset();
    if(arrange)arrange();
    physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
    require(AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,source),
            "actual physical shape source capture failed");
    require(source.allObservedTypeShapeRowsFromMatchedCallbacks &&
            source.namesCopiedFromMatchedEnumeratorCallbacks &&
            source.allRowsBoundToObservedRegistry,
            "matched physical shape source proof missing");
    census::Result result{};
    require(census::Build(names,source,result),"shape census build refused actual captured rows");
    require(result.census.completeObservedSubset &&
            result.sourceOwnedAndMatched && result.firstMatchedClassHeaderBitsCopied,
            "shape census not positively qualified within narrow scope");
    require(!result.canonicalExecutionTypeKeys &&
            !result.coherentVmEpochProven &&
            !result.physicalClassPointerOwnershipProven &&
            !result.runtimeAcceptance && !result.playerExecution,
            "shape census promoted canonical identity/Player");
    require(result.census.total==names.Count(),"shape count inconsistent");
    if(names.Count()){
        require(result.census.rowsByCategory[shape::Index(expected)]==1 &&
                result.census.total==1,"incorrect physical class shape category");
    }
    require(!readers&&!observationLockDepth&&!metadataLockDepth,
            "metadata lifetime/lock leaked after shape sampling");
    names.Revoke();
    require(!names.Complete()&&!names.Count()&&!names.TextBytes() &&
            scratch.Observed()==0,
            "captured metadata pointers or name text survived revocation");
    std::cout<<"PASS SHAPE "<<label<<"\n";
}
static void liveCensusTest(const char* label,bool expectedSuccess,
    shape::Category expectedShape,
    std::function<void()> arrange={},
    std::function<void(const census::Result&,const owned::Receipt&)> inspect={})
{
    reset();if(arrange)arrange();
    physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
    census::Result result{};
    const bool ok=AssemblyShadow::TryCaptureObservedTypeShapeCensus(
        scratch,names,source,result);
    require(ok==expectedSuccess,"unexpected actual VM source→census verdict");
    require(!readers&&!observationLockDepth&&!metadataLockDepth,
            "live census leaked metadata reader/registry/metadata lock");
    require(!names.Complete()&&!names.Count()&&!names.TextBytes(),
            "live census retained class name or shape source rows");
    require(scratch.Observed()==0,"live census retained matched pointers");
    require(!result.canonicalExecutionTypeKeys &&
            !result.physicalClassPointerOwnershipProven &&
            !result.coherentVmEpochProven&&!result.runtimeAcceptance &&
            !result.playerExecution,"census claimed canonical/Player acceptance");
    if(ok){
        require(result.census.completeObservedSubset &&
                result.sourceOwnedAndMatched && result.firstMatchedClassHeaderBitsCopied &&
                result.census.total==source.ownedRows,
                "source did not produce a valid pointer-free census");
        if(result.census.total){
            require(result.census.total==1 &&
                result.census.rowsByCategory[shape::Index(expectedShape)]==1,
                "live census category or count incorrect");
        }
    }else{
        require(!result.census.completeObservedSubset &&
                !result.sourceOwnedAndMatched &&
                !result.firstMatchedClassHeaderBitsCopied &&
                result.census.total==0,
                "failed source retained partial shape facts");
    }
    if(inspect)inspect(result,source);
    std::cout<<"PASS LIVE CENSUS "<<label<<"\n";
}
namespace shapeJson=il2cpp::vm::assembly_shadow_observed_type_shape_json;
static census::Result readyShape(size_t total=0,
    shape::Category category=shape::Category::PlainReferenceDefinitionCandidate,
    size_t candidate=0,size_t activeRows=0,uint64_t revision=7){
    census::Result r{};
    r.census.completeObservedSubset=true;
    r.census.total=total;
    r.census.rowsByCategory[shape::Index(category)]=total;
    r.candidateRegistrationRows=candidate;
    r.activeShadowRegistrationRows=activeRows;
    r.registryRevision=revision;
    r.sourceOwnedAndMatched=true;r.firstMatchedClassHeaderBitsCopied=true;
    return r;
}
static void jsonCase(const char* label,const std::function<void()>& f){
    f();std::cout<<"PASS SHAPE JSON "<<label<<"\n";
}
static std::string shapeJsonBytes(const shapeJson::Record& r){
    require(r.Complete()&&r.Data()&&r.Bytes(),"noncanonical shape JSON not complete");
    return std::string(r.Data(),r.Bytes());
}
static void actualJson(const char*label,bool pass,shape::Category expected,
    std::function<void()> arrange={},
    std::function<void(const shapeJson::Record&,const shapeJson::Receipt&,
                       const census::Result&,const owned::Receipt&)> inspect={},
    size_t limit=shapeJson::Record::kMaximumBytes)
{
 reset();if(arrange)arrange();
 physical::Scratch scratch;owned::Capture names;owned::Receipt sourceReceipt{};
 census::Result source{};auto output=std::make_unique<shapeJson::Record>();
 shapeJson::Receipt receipt{};
 const bool ok=AssemblyShadow::TryCaptureObservedTypeShapeJson(
    scratch,names,sourceReceipt,source,*output,receipt,limit);
 require(ok==pass,"unexpected genuine VM->shape-JSON result");
 require(!readers&&!metadataLockDepth&&!observationLockDepth&&
         !names.Complete()&&!names.Count()&&!names.TextBytes()&&
         !scratch.Observed(),"real source/VM lock leaked");
 require(!receipt.canonicalExecutionTypeKeys&&
         !receipt.physicalClassPointerOriginProven&&
         !receipt.coherentVmEpochProven&&!receipt.playerExecution&&
         !receipt.runtimeAcceptance,"noncanonical scope elevated");
 if(ok){
    require(receipt.ownedShapeJsonComplete&&receipt.noncanonicalObservedSubsetOnly&&
            output->Complete()&&output->Bytes()&&
            receipt.observedRows==source.census.total&&
            source.census.completeObservedSubset&&
            sourceReceipt.allObservedTypeShapeRowsFromMatchedCallbacks,
            "successful source/receipt mismatch");
    if(source.census.total==1)require(
        source.census.rowsByCategory[shape::Index(expected)]==1,
        "incorrect shape category copied from matched callback");
 }else{
    require(!output->Complete()&&!output->Data()&&!output->Bytes()&&
            !receipt.ownedShapeJsonComplete,"failed source exposed JSON bytes");
 }
 if(inspect)inspect(*output,receipt,source,sourceReceipt);
 std::cout<<"PASS LIVE SHAPE JSON "<<label<<"\n";
}
int main(){try{
 test("empty-physical-owned",true,{},[](const auto&n,const auto&){require(n.Count()==0,"empty name count");});
 test("one-physical-owned",true,[]{publish(&classes[0]);physicalEnumeration={&classes[0]};},
     [](const auto&n,const auto&){owned::RowView row{};require(n.Get(0,row),"missing owned row");
        require(value(row.physicalAssemblyName)=="PhysicalAssembly"&&
                value(row.nameSpace)=="Namespace"&&value(row.simpleName)=="Class","wrong names");
       require(row.registrySlot==slot(&classes[0]) &&
         row.registrationSource==witness::Source::Candidate,"single registration not bound");});
 test("two-roles-owned",true,[]{publish(&classes[0]);publish(&classes[1],witness::Source::ActiveShadow);
                            physicalEnumeration={&classes[0],&classes[1]};},
     [](const auto&n,const auto&){require(n.Count()==2,"role-owned name count");
        owned::RowView a{},b{};require(n.Get(0,a)&&n.Get(1,b),"role rows unavailable");
        require(a.registrationSource==witness::Source::Candidate &&
                b.registrationSource==witness::Source::ActiveShadow,"role association lost");});
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
 test("reverse-enumeration-role-binding",true,[]{
       publish(&classes[0]);publish(&classes[1],witness::Source::ActiveShadow);
       physicalEnumeration={&classes[1],&classes[0]};},
     [](const auto&n,const auto&){owned::RowView a{},b{};require(n.Get(0,a)&&n.Get(1,b),"reverse rows");
        require(a.registrySlot==slot(&classes[1])&&
                a.registrationSource==witness::Source::ActiveShadow&&
                b.registrySlot==slot(&classes[0])&&
                b.registrationSource==witness::Source::Candidate,"enumerator order changed binding");});
 test("same-display-name-distinct-binding",true,[]{
       publish(&classes[0]);publish(&classes[1]);
       physicalEnumeration={&classes[0],&classes[1]};},
     [](const auto&n,const auto&){owned::RowView a{},b{};require(n.Get(0,a)&&n.Get(1,b),"name rows");
        require(value(a.simpleName)==value(b.simpleName) &&
                a.registrySlot!=b.registrySlot,"same names conflated registry identities");});
 test("hash-collision-binding",true,[]{
       require(slot(&classes[0])==slot(&classes[2048]),"fixture did not collide");
       publish(&classes[0]);publish(&classes[2048],witness::Source::ActiveShadow);
       physicalEnumeration={&classes[2048],&classes[0]};},
     [](const auto&n,const auto&){owned::RowView a{},b{};require(n.Get(0,a)&&n.Get(1,b),"collision rows");
        require(a.registrySlot==(slot(&classes[0])+1)%physical::kSlots &&
                a.registrationSource==witness::Source::ActiveShadow &&
                b.registrySlot==slot(&classes[0]) &&
                b.registrationSource==witness::Source::Candidate,"linear-probe binding lost");});
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
   publish(&classes[0]);physical::Scratch scratch;
   require(scratch.Consume(),"scratch consume");
   require(scratch.Capture(s_executionClasses.data(),s_executionClasses.size(),
       s_executionClassCount,s_executionRegistryRevision.Current(),
       s_executionRegistrationWitnesses),"binding capture");
   size_t index=0;witness::Source role=witness::Source::Candidate;
   require(!scratch.TryObservedRegistrationBinding(&classes[0],index,role) &&
           index==physical::kSlots&&role==witness::Source::None,
           "unmatched identity incorrectly bound");
   require(scratch.ObserveEnumeratedOwned(&classes[0])==physical::ObservationEvent::FirstMatch,
           "first match missing");
   require(scratch.TryObservedRegistrationBinding(&classes[0],index,role)&&
           index==slot(&classes[0])&&role==witness::Source::Candidate,
           "first matched observed binding wrong");
   scratch.Scrub();
   require(!scratch.TryObservedRegistrationBinding(&classes[0],index,role),
           "scrubbed scratch still permits binding");
   std::cout<<"PASS binding-not-matched-refused"<<"\n";
 }
 reset();{
   publish(&classes[0]);physical::Scratch scratch;require(scratch.Consume(),"consume");
   require(scratch.Capture(s_executionClasses.data(),s_executionClasses.size(),
       s_executionClassCount,s_executionRegistryRevision.Current(),
       s_executionRegistrationWitnesses),"capture");
   require(scratch.ObserveEnumeratedOwned(&classes[1])==physical::ObservationEvent::Ignored,
           "unexpectedly registered unrelated class");
   size_t index=0;witness::Source role=witness::Source::Candidate;
   require(!scratch.TryObservedRegistrationBinding(&classes[1],index,role),"unrelated got binding");
   std::cout<<"PASS unobserved-binding-refused"<<"\n";
 }
 reset();{
   owned::Capture n;require(n.Begin(),"begin");
   require(!n.Add(owned::kRegistrationSlots,witness::Source::Candidate,"Asm","Ns","Name") &&
           n.FirstError()==owned::Error::InvalidRegistrationBinding &&
           !n.Complete()&&n.Count()==0&&n.TextBytes()==0,"invalid slot accepted");
   owned::RowView v{};require(!n.Get(0,v),"failed slot exposed row");
   std::cout<<"PASS invalid-slot-refused-and-scrubbed"<<"\n";
 }
 reset();{
   owned::Capture n;require(n.Begin(),"begin");
   require(!n.Add(7,witness::Source::None,"Asm","Ns","Name") &&
           n.FirstError()==owned::Error::InvalidRegistrationBinding &&
           !n.Complete()&&n.TextBytes()==0,"invalid role accepted");
   std::cout<<"PASS invalid-role-refused-and-scrubbed"<<"\n";
 }
 reset();{
   owned::Capture n;require(n.Begin(),"begin");
   require(n.Add(7,witness::Source::Candidate,mutableName,"Ns","First"),"initial add");
   require(!n.Add(7,witness::Source::Candidate,"Asm","Ns","Second") &&
           n.FirstError()==owned::Error::DuplicateRegistrationBinding &&
           !n.Complete()&&n.Count()==0&&n.TextBytes()==0,"duplicate registry slot accepted");
   owned::RowView v{};require(!n.Get(0,v),"failed duplicate exposed row");
   std::cout<<"PASS duplicate-registration-binding-refused-and-scrubbed"<<"\n";
 }
 reset();{
  physical::Scratch scratch;owned::Capture names;owned::Receipt receipt;
  require(AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,receipt),"first empty capture failed");
  require(!AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,receipt),"reused capture accepted");
  require(receipt.error==owned::Error::ReusedCapture,"wrong reused capture error");
  std::cout<<"PASS "<<"reused-owned-capture-refused"<<"\n";
 }
 shapeCase("zero-observed-shape-subset",shape::Category::PlainReferenceDefinitionCandidate);
 shapeCase("plain-reference-definition-candidate",shape::Category::PlainReferenceDefinitionCandidate,[]{
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("plain-value-definition-candidate",shape::Category::PlainValueDefinitionCandidate,[]{
       classes[0].byval_arg.type=IL2CPP_TYPE_VALUETYPE;
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("missing-metadata-definition",shape::Category::MissingMetadataDefinition,[]{
       classes[0].typeMetadataHandle=nullptr;
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("ranked-array-is-not-definition-candidate",shape::Category::RankedOrArray,[]{
       classes[0].rank=2;
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("constructed-generic-is-not-definition-candidate",shape::Category::ConstructedGeneric,[]{
       classes[0].generic_class=&classes[1];
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("generic-definition-is-not-plain-candidate",shape::Category::GenericDefinition,[]{
       classes[0].is_generic=true;
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("nested-definition-is-not-plain-candidate",shape::Category::NestedDefinition,[]{
       classes[0].declaringType=&classes[1];
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("unsupported-raw-type-code",shape::Category::UnsupportedTypeCode,[]{
       classes[0].byval_arg.type=0x34;
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 shapeCase("conflicting-flags-reject-conservatively",shape::Category::RankedOrArray,[]{
       classes[0].rank=1;classes[0].is_generic=true;
       classes[0].generic_class=&classes[1];
       publish(&classes[0]);physicalEnumeration={&classes[0]};
 });
 reset();{
    publish(&classes[0]);publish(&classes[1],witness::Source::ActiveShadow);
    classes[1].is_generic=true;
    physicalEnumeration={&classes[1],&classes[0]};
    physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
    require(AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,source),
            "multi-row type shape source");
    census::Result result{};
    require(census::Build(names,source,result),"mixed shape census build");
    require(result.census.total==2 &&
            result.census.rowsByCategory[shape::Index(shape::Category::PlainReferenceDefinitionCandidate)]==1 &&
            result.census.rowsByCategory[shape::Index(shape::Category::GenericDefinition)]==1 &&
            result.candidateRegistrationRows==1 &&
            result.activeShadowRegistrationRows==1,"mixed census role/shape conflated");
    std::cout<<"PASS SHAPE mixed-candidate-active-witness-shapes\n";
 }
 reset();{
    owned::Capture names;require(names.Begin(),"manual begin");
    require(names.Add(10,witness::Source::Candidate,"A","N","C"),"manual add");
    require(names.Seal(1),"manual seal");
    owned::Receipt source{};
    source.observedClasses=source.matchedClasses=source.ownedRows=
        source.boundRegistrationRows=1;
    source.ownedTextBytes=names.TextBytes();
    source.registryRevisionBefore=source.registryRevisionAfter=1;
    source.namesCopiedFromMatchedEnumeratorCallbacks=true;
    source.allRowsBoundToObservedRegistry=true;
    source.allObservedTypeShapeRowsFromMatchedCallbacks=true;
    census::Result result{};
    require(!census::Build(names,source,result) &&
            result.error==census::Error::MissingShapeProof &&
            !result.census.completeObservedSubset &&
            !result.firstMatchedClassHeaderBitsCopied,
            "forged manually copied names promoted to VM shape proof");
    std::cout<<"PASS SHAPE manual-unobserved-raw-shape-refused\n";
 }
 reset();{
    publish(&classes[0]);physicalEnumeration={&classes[0]};
    physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
    require(AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,source),
            "tampered-source case capture");
    source.allObservedTypeShapeRowsFromMatchedCallbacks=false;
    census::Result result{};
    require(!census::Build(names,source,result) &&
            result.error==census::Error::InvalidSourceReceipt &&
            !result.census.completeObservedSubset,"missing source receipt flag accepted");
    std::cout<<"PASS SHAPE missing-shape-source-receipt-refused\n";
 }
 reset();{
    publish(&classes[0]);physicalEnumeration={&classes[0]};
    physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
    require(AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,source),
            "one-use census source");
    census::Result result{};
    require(census::Build(names,source,result),"one-use first census");
    require(!census::Build(names,source,result) &&
            result.error==census::Error::ReusedCensus &&
            !result.census.completeObservedSubset && result.census.total==0,
            "reused census retained successful facts");
    std::cout<<"PASS SHAPE reused-census-revokes-prior-success\n";
 }
 reset();{
    publish(&classes[0]);
    physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
    require(!AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(scratch,names,source),
            "incomplete iterator unexpectedly passed");
    census::Result result{};
    require(!census::Build(names,source,result) &&
            result.error==census::Error::IncompleteCapture &&
            !result.census.completeObservedSubset,
            "incomplete physical source promoted to shape census");
    std::cout<<"PASS SHAPE missing-physical-source-cannot-certify-shape\n";
 }
 liveCensusTest("empty-iterator-complete-only-subset",true,
     shape::Category::PlainReferenceDefinitionCandidate);
 liveCensusTest("firstmatched-plain-reference",true,
     shape::Category::PlainReferenceDefinitionCandidate,[]{
       publish(&classes[0]);physicalEnumeration={&classes[0]};
     },[](const auto& result,const auto& source){
       require(result.candidateRegistrationRows==1 &&
               result.activeShadowRegistrationRows==0 &&
               source.allObservedTypeShapeRowsFromMatchedCallbacks,
               "first match source shape proof missing");
     });
 liveCensusTest("firstmatched-plain-value",true,
     shape::Category::PlainValueDefinitionCandidate,[]{
       classes[0].byval_arg.type=IL2CPP_TYPE_VALUETYPE;
       publish(&classes[0]);physicalEnumeration={&classes[0]};
     });
 liveCensusTest("generic-definition-remains-uncertified",true,
     shape::Category::GenericDefinition,[]{
       classes[0].is_generic=true;publish(&classes[0]);
       physicalEnumeration={&classes[0]};
     });
 liveCensusTest("constructed-generic-remains-uncertified",true,
     shape::Category::ConstructedGeneric,[]{
       classes[0].generic_class=&classes[1];publish(&classes[0]);
       physicalEnumeration={&classes[0]};
     });
 liveCensusTest("nested-definition-remains-uncertified",true,
     shape::Category::NestedDefinition,[]{
       classes[0].declaringType=&classes[1];publish(&classes[0]);
       physicalEnumeration={&classes[0]};
     });
 liveCensusTest("ranked-array-remains-uncertified",true,
     shape::Category::RankedOrArray,[]{
       classes[0].rank=3;publish(&classes[0]);
       physicalEnumeration={&classes[0]};
     });
 liveCensusTest("missing-definition-handle-is-not-candidate",true,
     shape::Category::MissingMetadataDefinition,[]{
       classes[0].typeMetadataHandle=nullptr;publish(&classes[0]);
       physicalEnumeration={&classes[0]};
     });
 liveCensusTest("unsupported-typecode-is-not-candidate",true,
     shape::Category::UnsupportedTypeCode,[]{
       classes[0].byval_arg.type=0x31;publish(&classes[0]);
       physicalEnumeration={&classes[0]};
     });
 liveCensusTest("conflicting-metadata-flags-conservative",true,
     shape::Category::RankedOrArray,[]{
       classes[0].rank=1;classes[0].is_generic=true;
       classes[0].generic_class=&classes[1];
       publish(&classes[0]);physicalEnumeration={&classes[0]};
     });
 reset();{
     publish(&classes[0]);publish(&classes[1],witness::Source::ActiveShadow);
     classes[1].is_generic=true;
     physicalEnumeration={&classes[1],&classes[0]};
     physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
     census::Result result{};
     require(AssemblyShadow::TryCaptureObservedTypeShapeCensus(scratch,names,source,result),
             "mixed roles real source census refused");
     require(result.census.total==2 &&
             result.candidateRegistrationRows==1 &&
             result.activeShadowRegistrationRows==1 &&
             result.census.rowsByCategory[shape::Index(shape::Category::GenericDefinition)]==1 &&
             result.census.rowsByCategory[shape::Index(shape::Category::PlainReferenceDefinitionCandidate)]==1,
             "candidate/active role identity mixed with shape categorization");
     require(!names.Complete()&&!names.Count()&&!scratch.Observed()&&
             !readers&&!observationLockDepth&&!metadataLockDepth,
             "mixed cohort source or metadata lock leaked");
     std::cout<<"PASS LIVE CENSUS mixed-candidate-active-noncanonical-cohort\n";
 }
 liveCensusTest("unmatched-observed-class-refused",false,
     shape::Category::MissingMatchedCallbackProof,[]{
       publish(&classes[0]);
     },[](const auto& result,const auto& source){
       require(result.error==census::Error::IncompleteCapture &&
               !source.allObservedTypeShapeRowsFromMatchedCallbacks,
               "unmatched observed class admitted");
     });
 liveCensusTest("invalid-physical-matched-name-refused",false,
     shape::Category::MissingMatchedCallbackProof,[]{
       classes[0].name=nullptr;publish(&classes[0]);
       physicalEnumeration={&classes[0]};
     },[](const auto& result,const auto&){
       require(result.error==census::Error::IncompleteCapture,
               "invalid matched source promoted");
     });
 liveCensusTest("revision-after-metadata-pass-refused",false,
     shape::Category::MissingMatchedCallbackProof,[]{
       publish(&classes[0]);physicalEnumeration={&classes[0]};
       afterMetadataRelease=[](){s_executionRegistryRevision.Bump();};
     },[](const auto& result,const auto& source){
       require(result.error==census::Error::IncompleteCapture &&
               !source.allObservedTypeShapeRowsFromMatchedCallbacks,
               "changed registry source promoted");
     });
 reset();{
     publish(&classes[0]);physicalEnumeration={&classes[0]};
     physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
     census::Result result{};result.census.total=17;
     source.namesCopiedFromMatchedEnumeratorCallbacks=true;
     source.allObservedTypeShapeRowsFromMatchedCallbacks=true;
     require(!AssemblyShadow::TryCaptureObservedTypeShapeCensus(
         scratch,names,source,result),"stale output accepted");
     require(result.error==census::Error::ReusedCensus &&
             !result.census.total && !result.census.completeObservedSubset &&
             iteratorCalls==0 && !names.Count()&&!scratch.Observed() &&
             !source.namesCopiedFromMatchedEnumeratorCallbacks &&
             !source.allObservedTypeShapeRowsFromMatchedCallbacks,
             "stale output retained an earlier successful source receipt");
     std::cout<<"PASS LIVE CENSUS stale-output-rejected-before-iterator\n";
 }
 reset();{
     publish(&classes[0]);physicalEnumeration={&classes[0]};
     physical::Scratch scratch;owned::Capture names;owned::Receipt source{};
     census::Result result{};
     require(AssemblyShadow::TryCaptureObservedTypeShapeCensus(
        scratch,names,source,result),"first live count");
     const size_t countBefore=iteratorCalls;
     require(!AssemblyShadow::TryCaptureObservedTypeShapeCensus(
        scratch,names,source,result),"reused census accepted");
     require(result.error==census::Error::ReusedCensus &&
             !result.census.total&&!result.census.completeObservedSubset &&
             iteratorCalls==countBefore &&
             !source.namesCopiedFromMatchedEnumeratorCallbacks &&
             !source.allObservedTypeShapeRowsFromMatchedCallbacks,
             "reused successful census retained source success or iterated again");
     std::cout<<"PASS LIVE CENSUS reused-success-revoked-before-iterator\n";
 }

 jsonCase("empty-census-exact-json",[](){
   auto source=readyShape();
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(shapeJson::Compose(source,*out,receipt),"empty JSON");
   const std::string exact=
    "{\"schemaVersion\":1,\"kind\":\"R03P1ObservedTypeShapeCounts\","
    "\"scope\":\"ObservedRegistrySubsetShapeCountsOnly\",\"registryRevision\":7,"
    "\"observedRows\":0,\"candidateRows\":0,\"activeShadowRows\":0,"
    "\"canonicalExecutionTypeKeys\":false,\"physicalClassPointerOriginProven\":false,"
    "\"coherentVmEpochProven\":false,\"playerExecution\":false,\"runtimeAcceptance\":false,"
    "\"categories\":{\"plainReferenceDefinitionCandidates\":0,"
    "\"plainValueDefinitionCandidates\":0,\"missingMetadataDefinition\":0,"
    "\"rankedOrArray\":0,\"constructedGeneric\":0,\"genericDefinition\":0,"
    "\"nestedDefinition\":0,\"unsupportedTypeCode\":0,\"missingMatchedCallbackProof\":0}}";
   require(shapeJsonBytes(*out)==exact&&receipt.bytes==exact.size() &&
           receipt.ownedShapeJsonComplete&&receipt.noncanonicalObservedSubsetOnly,
           "empty JSON exact bytes/receipt differ");
 });
 jsonCase("plain-reference-candidate-only",[](){
   auto source=readyShape(1,shape::Category::PlainReferenceDefinitionCandidate,1);
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(shapeJson::Compose(source,*out,receipt),"one candidate JSON");
   const auto json=shapeJsonBytes(*out);
   require(json.find("\"plainReferenceDefinitionCandidates\":1")!=std::string::npos &&
           json.find("\"candidateRows\":1")!=std::string::npos &&
           json.find("\"canonicalExecutionTypeKeys\":false")!=std::string::npos,
           "shape-only candidate promoted to canonical");
 });
 jsonCase("genuine-iterator-value-source-json",[](){
   reset();classes[0].byval_arg.type=IL2CPP_TYPE_VALUETYPE;
   publish(&classes[0]);physicalEnumeration={&classes[0]};
   physical::Scratch scratch;owned::Capture names;owned::Receipt sourceReceipt{};
   census::Result source{};
   require(AssemblyShadow::TryCaptureObservedTypeShapeCensus(
       scratch,names,sourceReceipt,source),"actual class iterator shape");
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(shapeJson::Compose(source,*out,receipt) &&
           shapeJsonBytes(*out).find("\"plainValueDefinitionCandidates\":1")!=std::string::npos &&
           !readers&&!observationLockDepth&&!metadataLockDepth &&
           !names.Count()&&!scratch.Observed(),"actual physical value shape JSON");
 });
 jsonCase("genuine-iterator-generic-excluded",[](){
   reset();classes[0].generic_class=&classes[1];
   publish(&classes[0]);physicalEnumeration={&classes[0]};
   physical::Scratch scratch;owned::Capture names;owned::Receipt sourceReceipt{};
   census::Result source{};
   require(AssemblyShadow::TryCaptureObservedTypeShapeCensus(
       scratch,names,sourceReceipt,source),"actual constructed-generic source");
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(shapeJson::Compose(source,*out,receipt),"constructed generic JSON");
   const auto json=shapeJsonBytes(*out);
   require(json.find("\"constructedGeneric\":1")!=std::string::npos &&
           json.find("\"plainReferenceDefinitionCandidates\":0")!=std::string::npos,
           "constructed generic got a plain candidate certificate");
 });
 jsonCase("genuine-mixed-role-shape-cohort",[](){
   reset();publish(&classes[0]);publish(&classes[1],witness::Source::ActiveShadow);
   classes[1].is_generic=true;physicalEnumeration={&classes[1],&classes[0]};
   physical::Scratch scratch;owned::Capture names;owned::Receipt sourceReceipt{};
   census::Result source{};
   require(AssemblyShadow::TryCaptureObservedTypeShapeCensus(
       scratch,names,sourceReceipt,source),"mixed actual source");
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(shapeJson::Compose(source,*out,receipt),"mixed actual JSON");
   const auto json=shapeJsonBytes(*out);
   require(receipt.observedRows==2&&receipt.candidateRows==1&&
           receipt.activeShadowRows==1 &&
           json.find("\"genericDefinition\":1")!=std::string::npos &&
           json.find("\"plainReferenceDefinitionCandidates\":1")!=std::string::npos &&
           !names.Count()&&!scratch.Observed(),"mixed category and role lost");
 });
 jsonCase("output-independent-after-source-mutation",[](){
   auto source=readyShape(1,shape::Category::PlainReferenceDefinitionCandidate,1);
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(shapeJson::Compose(source,*out,receipt),"owned shape JSON");
   const auto before=shapeJsonBytes(*out);
   source.census.rowsByCategory.fill(0);source.census.total=0;source.registryRevision=UINT64_MAX;
   require(shapeJsonBytes(*out)==before,"owned output retained source view");
 });
 jsonCase("incomplete-census-zero-output",[](){
   census::Result source{};
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt) &&
           receipt.error==shapeJson::Error::IncompleteCensus &&
           !out->Complete()&&!out->Data()&&!out->Bytes(),
           "missing source admitted");
 });
 jsonCase("forged-typekey-or-epoch-refused",[](){
   auto source=readyShape(1,shape::Category::PlainReferenceDefinitionCandidate,1);
   source.canonicalExecutionTypeKeys=true;
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt) &&
           receipt.error==shapeJson::Error::InvalidScope &&
           !out->Data(),"forged canonical proof");
   auto second=readyShape();second.census.coherentVmEpochProven=true;
   auto next=std::make_unique<shapeJson::Record>();
   require(!shapeJson::Compose(second,*next,receipt) &&
           receipt.error==shapeJson::Error::InvalidScope &&
           !next->Data(),"forged epoch proof");
 });
 jsonCase("inconsistent-role-count-refused",[](){
   auto source=readyShape(1,shape::Category::PlainReferenceDefinitionCandidate,1,1);
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt) &&
           receipt.error==shapeJson::Error::InvalidCounts &&
           !out->Data(),"inconsistent roles admitted");
 });
 jsonCase("inconsistent-category-census-refused",[](){
   auto source=readyShape(2,shape::Category::PlainReferenceDefinitionCandidate,1,1);
   source.census.rowsByCategory[shape::Index(shape::Category::PlainReferenceDefinitionCandidate)]=1;
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt) &&
           receipt.error==shapeJson::Error::InvalidCategory &&
           !out->Data(),"incomplete category partition admitted");
 });
 jsonCase("unmatched-callback-category-refused",[](){
   auto source=readyShape(1,shape::Category::MissingMatchedCallbackProof,1);
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt) &&
           receipt.error==shapeJson::Error::InvalidCategory &&
           !out->Data(),"unmatched row promoted");
 });
 jsonCase("saturated-revision-refused",[](){
   auto source=readyShape(1,shape::Category::PlainReferenceDefinitionCandidate,1,0,UINT64_MAX);
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt) &&
           receipt.error==shapeJson::Error::InvalidRevision &&
           !out->Data(),"saturated source revision");
 });
 jsonCase("small-byte-budget-atomic-refusal",[](){
   auto source=readyShape(1,shape::Category::PlainReferenceDefinitionCandidate,1);
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt,128) &&
           receipt.error==shapeJson::Error::OutputCapacity &&
           !out->Complete()&&!out->Data()&&!out->Bytes() &&
           !receipt.ownedShapeJsonComplete,"partial byte stream escaped");
 });
 jsonCase("oversize-output-request-refused",[](){
   auto source=readyShape();
   auto out=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(source,*out,receipt,shapeJson::Record::kMaximumBytes+1) &&
           receipt.error==shapeJson::Error::InvalidOutputLimit &&
           !out->Data()&&!out->Bytes(),"oversized limit accepted");
 });
 jsonCase("reused-output-scrubs-earlier-success",[](){
   auto source=readyShape();auto out=std::make_unique<shapeJson::Record>();
   shapeJson::Receipt receipt{};
   require(shapeJson::Compose(source,*out,receipt)&&out->Bytes()>0,"initial success");
   require(!shapeJson::Compose(source,*out,receipt) &&
           receipt.error==shapeJson::Error::ReusedOutput &&
           out->FirstError()==shapeJson::Error::ReusedOutput &&
           !out->Complete()&&!out->Data()&&!out->Bytes(),
           "reused JSON kept previously emitted facts");
 });
 jsonCase("failed-output-cannot-be-reused",[](){
   census::Result bad{};auto out=std::make_unique<shapeJson::Record>();
   shapeJson::Receipt receipt{};
   require(!shapeJson::Compose(bad,*out,receipt),"unsealed census");
   auto good=readyShape();
   require(!shapeJson::Compose(good,*out,receipt) &&
           receipt.error==shapeJson::Error::ReusedOutput &&
           out->FirstError()==shapeJson::Error::IncompleteCensus &&
           !out->Bytes(),"failed JSON recorder reused");
 });

 actualJson("empty-real-vm-shape-json",true,
     shape::Category::PlainReferenceDefinitionCandidate);
 actualJson("plain-reference-real-vm-json",true,
     shape::Category::PlainReferenceDefinitionCandidate,[]{
       publish(&classes[0]);physicalEnumeration={&classes[0]};});
 actualJson("plain-value-real-vm-json",true,
     shape::Category::PlainValueDefinitionCandidate,[]{
       classes[0].byval_arg.type=IL2CPP_TYPE_VALUETYPE;
       publish(&classes[0]);physicalEnumeration={&classes[0]};});
 actualJson("generic-definition-real-vm-json",true,
     shape::Category::GenericDefinition,[]{
       classes[0].is_generic=true;publish(&classes[0]);
       physicalEnumeration={&classes[0]};});
 actualJson("constructed-generic-real-vm-json",true,
     shape::Category::ConstructedGeneric,[]{
       classes[0].generic_class=&classes[1];publish(&classes[0]);
       physicalEnumeration={&classes[0]};});
 actualJson("nested-definition-real-vm-json",true,
     shape::Category::NestedDefinition,[]{
       classes[0].declaringType=&classes[1];publish(&classes[0]);
       physicalEnumeration={&classes[0]};});
 actualJson("ranked-array-real-vm-json",true,
     shape::Category::RankedOrArray,[]{
       classes[0].rank=3;publish(&classes[0]);
       physicalEnumeration={&classes[0]};});
 actualJson("missing-definition-real-vm-json",true,
     shape::Category::MissingMetadataDefinition,[]{
       classes[0].typeMetadataHandle=nullptr;publish(&classes[0]);
       physicalEnumeration={&classes[0]};});
 actualJson("unsupported-shape-real-vm-json",true,
     shape::Category::UnsupportedTypeCode,[]{
       classes[0].byval_arg.type=0x34;publish(&classes[0]);
       physicalEnumeration={&classes[0]};});
 actualJson("mixed-candidate-active-real-json",true,
     shape::Category::PlainReferenceDefinitionCandidate,[]{
       publish(&classes[0]);publish(&classes[1],witness::Source::ActiveShadow);
       classes[1].is_generic=true;physicalEnumeration={&classes[1],&classes[0]};},
     [](const auto&output,const auto&receipt,const auto&source,const auto&){
       const auto json=shapeJsonBytes(output);
       require(receipt.observedRows==2&&receipt.candidateRows==1&&
               receipt.activeShadowRows==1 &&
               source.census.rowsByCategory[shape::Index(shape::Category::GenericDefinition)]==1 &&
               json.find("\"genericDefinition\":1")!=std::string::npos &&
               json.find("\"candidateRows\":1")!=std::string::npos,
               "mixed observed roles/category JSON changed");
     });
 actualJson("unmatched-source-real-vm-json-refused",false,
     shape::Category::MissingMatchedCallbackProof,[]{
       publish(&classes[0]);},[](const auto&,const auto&receipt,const auto&source,const auto&names){
       require(receipt.error==shapeJson::Error::IncompleteCensus &&
               source.error==census::Error::IncompleteCapture &&
               !names.allObservedTypeShapeRowsFromMatchedCallbacks,
               "unmatched source admitted");
     });
 actualJson("revision-after-iterator-json-refused",false,
     shape::Category::MissingMatchedCallbackProof,[]{
       publish(&classes[0]);physicalEnumeration={&classes[0]};
       afterMetadataRelease=[](){s_executionRegistryRevision.Bump();};});
 actualJson("small-budget-atomic-after-real-source",false,
     shape::Category::PlainReferenceDefinitionCandidate,[]{
       publish(&classes[0]);physicalEnumeration={&classes[0]};},
     [](const auto&,const auto&receipt,const auto&source,const auto&){
       require(receipt.error==shapeJson::Error::OutputCapacity&&
               source.census.completeObservedSubset&&source.census.total==1,
               "budget failure suppressed or wrongly invalidated source census");
     },128);
 actualJson("oversized-budget-no-iterator",false,
     shape::Category::PlainReferenceDefinitionCandidate,[]{
       publish(&classes[0]);physicalEnumeration={&classes[0]};},
     [](const auto&,const auto&receipt,const auto&source,const auto&names){
       require(receipt.error==shapeJson::Error::InvalidOutputLimit &&
               source.error==census::Error::InvalidShapeCensus&&
               iteratorCalls==0&&!names.namesCopiedFromMatchedEnumeratorCallbacks,
               "invalid budget traversed class source");
     },shapeJson::Record::kMaximumBytes+1);
 reset();{
    publish(&classes[0]);physicalEnumeration={&classes[0]};
    physical::Scratch scratch;owned::Capture names;owned::Receipt nameReceipt{};
    census::Result source{};auto output=std::make_unique<shapeJson::Record>();
    shapeJson::Receipt receipt{};
    require(AssemblyShadow::TryCaptureObservedTypeShapeJson(scratch,names,nameReceipt,
        source,*output,receipt,shapeJson::Record::kMaximumBytes),"first JSON");
    const size_t previous=iteratorCalls;
    require(!AssemblyShadow::TryCaptureObservedTypeShapeJson(scratch,names,nameReceipt,
        source,*output,receipt,shapeJson::Record::kMaximumBytes) &&
        receipt.error==shapeJson::Error::ReusedOutput&&
        source.error==census::Error::ReusedCensus&&
        iteratorCalls==previous&&!output->Data()&&!output->Bytes()&&
        !nameReceipt.namesCopiedFromMatchedEnumeratorCallbacks,
        "reused output retained stale JSON or source success");
    std::cout<<"PASS LIVE SHAPE JSON reused-complete-json-revokes-source\n";
 }
 reset();{
    publish(&classes[0]);physicalEnumeration={&classes[0]};
    physical::Scratch scratch;owned::Capture names;owned::Receipt nameReceipt{};
    census::Result source{};source.census.total=4;
    auto output=std::make_unique<shapeJson::Record>();shapeJson::Receipt receipt{};
    require(!AssemblyShadow::TryCaptureObservedTypeShapeJson(scratch,names,nameReceipt,
        source,*output,receipt,shapeJson::Record::kMaximumBytes)&&
        receipt.error==shapeJson::Error::IncompleteCensus&&
        source.error==census::Error::ReusedCensus&&
        !source.census.total&&iteratorCalls==0&&!output->Data() &&
        !nameReceipt.namesCopiedFromMatchedEnumeratorCallbacks,
        "stale census allowed class iterator or leaked source evidence");
    std::cout<<"PASS LIVE SHAPE JSON stale-census-blocked-before-iterator\n";
 }
 std::cout<<"P1 GENUINE VM SHAPE JSON HOST PASS\n";
 return 0;
 }catch(const std::exception&err){std::cerr<<"FAILED: "<<err.what()<<"\n";return 1;}
}
