// R03 P1: the exact production source method is compiled below under
// explicit VM mocks. A host PASS does not establish any live class provenance.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "AssemblyShadowOpaqueExecutionInventory.h"
struct Il2CppClass { int deliberatelyUnread; };
namespace il2cpp { namespace vm {
struct ActiveSnapshot {uint64_t generation;};
enum class AssemblyShadowState {Disabled,Committed,Failed};
static ActiveSnapshot act{7},alt{8};
static std::atomic<const ActiveSnapshot*> s_active{&act};
static std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Committed};
static std::array<Il2CppClass*,2048> s_executionClasses{};
static size_t s_executionClassCount=0;
static bool g_admission=true;
static size_t g_readers=0,g_lockDepth=0,g_sampleCount=0;
static std::function<void()> g_onSample;
struct ExecutionObservationLock {
    ExecutionObservationLock(){++g_lockDepth;++g_sampleCount;if(g_onSample)g_onSample();}
    ~ExecutionObservationLock(){--g_lockDepth;}
};
namespace assembly_shadow_metadata_lifetime {
struct Gate {
    struct Reader {
        bool active;
        explicit Reader(Gate&):active(g_admission){if(active)++g_readers;}
        ~Reader(){if(active)--g_readers;}
        explicit operator bool()const{return active;}
    };
};
inline Gate g_MetadataLifetimeGate{};
}
class AssemblyShadow {
public:
    static bool TryCaptureOpaqueExecutionInventory(
        assembly_shadow_opaque_execution::Scratch&,
        assembly_shadow_opaque_execution::Receipt&);
};
#include "AssemblyShadowOpaqueExecutionInventorySource.inl"
}}
using namespace il2cpp::vm;
namespace opaque=il2cpp::vm::assembly_shadow_opaque_execution;
static Il2CppClass entities[1024]{};
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void reset(){
    s_active.store(&act);s_state.store(AssemblyShadowState::Committed);
    s_executionClasses.fill(nullptr);s_executionClassCount=0;
    g_admission=true;g_readers=0;g_lockDepth=0;g_sampleCount=0;g_onSample=nullptr;
}
static void test(const char* name,opaque::Error expected,
                 std::function<void(opaque::Scratch&)> arrange={}){
    reset();opaque::Scratch scratch;opaque::Receipt receipt;
    if(arrange)arrange(scratch);
    const bool ok=AssemblyShadow::TryCaptureOpaqueExecutionInventory(scratch,receipt);
    require(ok==(expected==opaque::Error::None),"wrong return");
    require(receipt.error==expected,"wrong error");
    require(g_readers==0&&g_lockDepth==0,"reader/observation lock leak");
    require(scratch.Consumed(),"scratch reused or not admitted");
    require(!receipt.physicalClassPointerOriginProven&&
            !receipt.coherentVmEpochProven&&!receipt.executionJsonComplete&&
            !receipt.playerExecution&&!receipt.runtimeAcceptance,"false acceptance");
    if(ok){
        require(receipt.opaqueInventoryObserved&&receipt.exactOpaqueSlotSamplesEqual,
                "missing complete bounded observation");
        require(receipt.completedSourceSamples==2&&g_sampleCount==2,"wrong sampling");
        require(receipt.countBefore==receipt.countAfter&&
                receipt.countAfter==s_executionClassCount,"incorrect count");
    }else require(!receipt.opaqueInventoryObserved,"partial success");
    require(!scratch.Before().Populated()&&!scratch.After().Populated(),
            "opaque scratch was not invalidated");
    std::cout<<"PASS "<<name<<"\n";
}
int main(){
    try{
        test("empty-inventory",opaque::Error::None);
        test("one-nonempty-opaque",opaque::Error::None,[](auto&){
            s_executionClasses[103]=&entities[0];s_executionClassCount=1;});
        test("three-scattered-opaque",opaque::Error::None,[](auto&){
            s_executionClasses[9]=&entities[0];s_executionClasses[300]=&entities[1];
            s_executionClasses[2047]=&entities[2];s_executionClassCount=3;});
        test("full-capacity-opaque",opaque::Error::None,[](auto&){
            for(size_t i=0;i<1024;++i){s_executionClasses[i*2]=&entities[i];}
            s_executionClassCount=1024;});
        test("closed-metadata-ticket",opaque::Error::AdmissionUnavailable,[](auto&){
            g_admission=false;});
        test("declared-count-too-large",opaque::Error::InvalidRegistry,[](auto&){
            s_executionClassCount=1025;});
        test("over-capacity-actual-entries",opaque::Error::InvalidRegistry,[](auto&){
            for(size_t i=0;i<1025;++i){s_executionClasses[i]=&entities[i%1024];}
            s_executionClassCount=1025;});
        test("missing-identity-for-count",opaque::Error::InvalidRegistry,[](auto&){
            s_executionClassCount=1;});
        test("unexpected-identity-with-count-zero",opaque::Error::InvalidRegistry,[](auto&){
            s_executionClasses[0]=&entities[0];});
        test("duplicate-pointer",opaque::Error::DuplicateIdentity,[](auto&){
            s_executionClasses[0]=&entities[0];s_executionClasses[1]=&entities[0];
            s_executionClassCount=2;});
        test("class-added-before-second-sample",opaque::Error::SourceChanged,[](auto&){
            g_onSample=[](){if(g_sampleCount==2){
                s_executionClasses[77]=&entities[0];s_executionClassCount=1;}};});
        test("same-count-replacement",opaque::Error::SourceChanged,[](auto&){
            s_executionClasses[40]=&entities[0];s_executionClassCount=1;
            g_onSample=[](){if(g_sampleCount==2)s_executionClasses[40]=&entities[1];};});
        test("same-count-relocation",opaque::Error::SourceChanged,[](auto&){
            s_executionClasses[40]=&entities[0];s_executionClassCount=1;
            g_onSample=[](){if(g_sampleCount==2){
                s_executionClasses[40]=nullptr;s_executionClasses[41]=&entities[0];}};});
        test("state-flip",opaque::Error::SourceChanged,[](auto&){
            g_onSample=[](){if(g_sampleCount==2)s_state.store(AssemblyShadowState::Failed);};});
        test("active-identity-flip",opaque::Error::SourceChanged,[](auto&){
            g_onSample=[](){if(g_sampleCount==2)s_active.store(&alt);};});
        test("one-shot-failure",opaque::Error::ReusedScratch,[](auto& scratch){
            require(scratch.Consume(),"initial consumption failed");});
        test("second-sample-count-corruption",opaque::Error::InvalidRegistry,[](auto&){
            g_onSample=[](){if(g_sampleCount==2)s_executionClassCount=1;};});
        reset();
        {
            opaque::Scratch scratch;opaque::Receipt receipt;
            require(AssemblyShadow::TryCaptureOpaqueExecutionInventory(scratch,receipt),
                    "initial scratch capture failed");
            require(!AssemblyShadow::TryCaptureOpaqueExecutionInventory(scratch,receipt),
                    "scratch reuse accepted");
            require(receipt.error==opaque::Error::ReusedScratch&&
                    !receipt.opaqueInventoryObserved,"reuse receipt incorrect");
            std::cout<<"PASS successful-scratch-reuse-refused\n";
        }
        std::cout<<"P1 OPAQUE EXECUTION INVENTORY SOURCE HOST PASS\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAILED: "<<error.what()<<"\n";return 1;
    }
}
