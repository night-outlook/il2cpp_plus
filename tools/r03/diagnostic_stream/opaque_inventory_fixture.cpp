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
#include "AssemblyShadowExecutionRegistryRevision.h"
#include "AssemblyShadowExecutionRegistrationWitness.h"
#include "AssemblyShadowExecutionRegistryPublisher.h"
struct Il2CppClass { int deliberatelyUnread; };
namespace il2cpp { namespace vm {
struct ActiveSnapshot {uint64_t generation;};
enum class AssemblyShadowState {Disabled,Committed,Failed};
static ActiveSnapshot act{7},alt{8};
static std::atomic<const ActiveSnapshot*> s_active{&act};
static std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Committed};
static std::array<Il2CppClass*,2048> s_executionClasses{};
static size_t s_executionClassCount=0;
static assembly_shadow_execution_registry::Revision s_executionRegistryRevision{};
static assembly_shadow_execution_witness::Table s_executionRegistrationWitnesses{};
static bool g_autoseedWitnesses=true;
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
    s_executionRegistryRevision=assembly_shadow_execution_registry::Revision{};
    s_executionRegistrationWitnesses=assembly_shadow_execution_witness::Table{};
    g_autoseedWitnesses=true;
    g_admission=true;g_readers=0;g_lockDepth=0;g_sampleCount=0;g_onSample=nullptr;
}
static void test(const char* name,opaque::Error expected,
                 std::function<void(opaque::Scratch&)> arrange={}){
    reset();opaque::Scratch scratch;opaque::Receipt receipt;
    if(arrange)arrange(scratch);
    // Seed the fixture from the same write-once admission API as production.
    // Adversarial cases can disable this or pre-seed an incorrect witness.
    if(g_autoseedWitnesses)
        for(size_t i=0;i<s_executionClasses.size();++i)
            if(s_executionClasses[i])
                require(s_executionRegistrationWitnesses.Register(
                    i,s_executionClasses[i],
                    assembly_shadow_execution_witness::Source::Candidate),
                    "fixture witness admission failed");
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
        require(receipt.registrationWitnessesObserved,
                "registration witness was not observed");
        require(receipt.candidateWitnessesBefore+receipt.activeWitnessesBefore==
                receipt.countBefore &&
                receipt.candidateWitnessesAfter+receipt.activeWitnessesAfter==
                receipt.countAfter,"invalid witnessed count");
        require(receipt.registryRevisionFenceStable&&
                receipt.registryRevisionBefore==receipt.registryRevisionAfter&&
                receipt.registryRevisionAfter==s_executionRegistryRevision.Current(),
                "registry mutation fence not stable");
        require(receipt.countBefore==receipt.countAfter&&
                receipt.countAfter==s_executionClassCount,"incorrect count");
    }else require(!receipt.opaqueInventoryObserved&&
                  !receipt.registrationWitnessesObserved&&
                  !receipt.registryRevisionFenceStable,"partial success");
    require(!scratch.Before().Populated()&&!scratch.After().Populated(),
            "opaque scratch was not invalidated");
    std::cout<<"PASS "<<name<<"\n";
}
// Exercise the EXACT native publisher used by ObserveExecutionClass, under
// controlled single-threaded fixture ownership. No class is dereferenced.
namespace publisher=il2cpp::vm::assembly_shadow_execution_publisher;
namespace witness=il2cpp::vm::assembly_shadow_execution_witness;
static publisher::Result publish(Il2CppClass* klass,witness::Source source,
                                 size_t maximum=1024)
{
    ExecutionObservationLock lock;
    return publisher::Insert(s_executionClasses,s_executionClassCount,maximum,
                             s_executionRegistrationWitnesses,
                             s_executionRegistryRevision,klass,source);
}
static size_t slot_for(Il2CppClass* klass)
{
    return (reinterpret_cast<uintptr_t>(klass)>>3)%s_executionClasses.size();
}
static void writer_test(const char* name,std::function<void()> check)
{
    reset();
    check();
    require(g_readers==0&&g_lockDepth==0,"publisher lock leak");
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
                s_executionClasses[77]=&entities[0];s_executionClassCount=1;
                require(s_executionRegistrationWitnesses.Register(
                    77,&entities[0],assembly_shadow_execution_witness::Source::Candidate),
                    "added class witness failure");
                s_executionRegistryRevision.Advance();
            }};});
        test("same-count-replacement",opaque::Error::SourceChanged,[](auto&){
            s_executionClasses[40]=&entities[0];s_executionClassCount=1;
            g_onSample=[](){if(g_sampleCount==2){
                s_executionClasses[40]=&entities[1];
                s_executionRegistrationWitnesses=assembly_shadow_execution_witness::Table{};
                require(s_executionRegistrationWitnesses.Register(
                    40,&entities[1],assembly_shadow_execution_witness::Source::Candidate),
                    "replacement fixture witness failure");
                s_executionRegistryRevision.Advance();
            }};});
        test("same-count-relocation",opaque::Error::SourceChanged,[](auto&){
            s_executionClasses[40]=&entities[0];s_executionClassCount=1;
            g_onSample=[](){if(g_sampleCount==2){
                s_executionClasses[40]=nullptr;s_executionClasses[41]=&entities[0];
                s_executionRegistrationWitnesses=assembly_shadow_execution_witness::Table{};
                require(s_executionRegistrationWitnesses.Register(
                    41,&entities[0],assembly_shadow_execution_witness::Source::Candidate),
                    "relocation fixture witness failure");
                s_executionRegistryRevision.Advance();
            }};});
        test("state-flip",opaque::Error::SourceChanged,[](auto&){
            g_onSample=[](){if(g_sampleCount==2)s_state.store(AssemblyShadowState::Failed);};});
        test("active-identity-flip",opaque::Error::SourceChanged,[](auto&){
            g_onSample=[](){if(g_sampleCount==2)s_active.store(&alt);};});
        test("one-shot-failure",opaque::Error::ReusedScratch,[](auto& scratch){
            require(scratch.Consume(),"initial consumption failed");});
        test("second-sample-count-corruption",opaque::Error::InvalidRegistry,[](auto&){
            g_onSample=[](){if(g_sampleCount==2)s_executionClassCount=1;};});
        test("same-slots-new-registry-revision",opaque::Error::SourceChanged,[](auto&){
            s_executionClasses[40]=&entities[0];s_executionClassCount=1;
            s_executionRegistryRevision=assembly_shadow_execution_registry::Revision{8};
            g_onSample=[](){if(g_sampleCount==2)
                require(s_executionRegistryRevision.Advance(),"revision advance failed");};
        });
        test("stable-nonzero-registry-revision",opaque::Error::None,[](auto&){
            s_executionClasses[40]=&entities[0];s_executionClassCount=1;
            s_executionRegistryRevision=assembly_shadow_execution_registry::Revision{9};
        });
        test("saturated-registry-revision-refused",opaque::Error::InvalidRegistry,[](auto&){
            s_executionRegistryRevision=assembly_shadow_execution_registry::Revision{
                std::numeric_limits<uint64_t>::max()};
            require(!s_executionRegistryRevision.Advance(),"revision overflow accepted");
        });
        test("unwitnessed-pointer-refused",opaque::Error::InvalidRegistry,[](auto&){
            s_executionClasses[10]=&entities[0];s_executionClassCount=1;
            g_autoseedWitnesses=false;
        });
        test("mismatched-witness-pointer-refused",opaque::Error::InvalidRegistry,[](auto&){
            s_executionClasses[10]=&entities[0];s_executionClassCount=1;
            g_autoseedWitnesses=false;
            require(s_executionRegistrationWitnesses.Register(
                10,&entities[1],assembly_shadow_execution_witness::Source::Candidate),
                "negative fixture cannot seed mismatched pointer");
        });
        test("phantom-witness-empty-slot-refused",opaque::Error::InvalidRegistry,[](auto&){
            g_autoseedWitnesses=false;
            require(s_executionRegistrationWitnesses.Register(
                10,&entities[1],assembly_shadow_execution_witness::Source::Candidate),
                "negative fixture cannot seed ghost source");
        });
        test("active-shadow-witness-admitted",opaque::Error::None,[](auto&){
            s_executionClasses[10]=&entities[0];s_executionClassCount=1;
            g_autoseedWitnesses=false;
            require(s_executionRegistrationWitnesses.Register(
                10,&entities[0],assembly_shadow_execution_witness::Source::ActiveShadow),
                "active shadow fixture witness failed");
        });
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
        writer_test("publisher-candidate",[](){
            require(publish(&entities[0],witness::Source::Candidate)==publisher::Result::Inserted,
                    "candidate not published");
            witness::Source observed{};
            require(s_executionRegistrationWitnesses.Matches(slot_for(&entities[0]),&entities[0],observed)
                    &&observed==witness::Source::Candidate,"candidate witness unavailable");
            opaque::Scratch scratch;opaque::Receipt receipt;
            require(AssemblyShadow::TryCaptureOpaqueExecutionInventory(scratch,receipt) &&
                    receipt.candidateWitnessesBefore==1&&receipt.activeWitnessesBefore==0,
                    "registered candidate not visible in collector");
            require(s_executionRegistryRevision.Current()==1,"candidate revision");
        });
        writer_test("publisher-active-shadow",[](){
            require(publish(&entities[1],witness::Source::ActiveShadow)==publisher::Result::Inserted,
                    "active-shadow not published");
            opaque::Scratch scratch;opaque::Receipt receipt;
            require(AssemblyShadow::TryCaptureOpaqueExecutionInventory(scratch,receipt) &&
                    receipt.activeWitnessesBefore==1&&receipt.candidateWitnessesBefore==0,
                    "active-shadow source witness not captured");
        });
        writer_test("publisher-duplicate",[](){
            require(publish(&entities[0],witness::Source::Candidate)==publisher::Result::Inserted,
                    "initial registration rejected");
            require(publish(&entities[0],witness::Source::ActiveShadow)==publisher::Result::Existing,
                    "duplicate should remain first registered source");
            require(s_executionClassCount==1&&s_executionRegistryRevision.Current()==1,
                    "duplicate changed registry count/revision");
        });
        writer_test("publisher-capacity",[](){
            require(publish(&entities[0],witness::Source::Candidate,1)==publisher::Result::Inserted,
                    "first capacity slot failed");
            require(publish(&entities[1],witness::Source::Candidate,1)==publisher::Result::Full,
                    "capacity should fail closed");
            require(s_executionClassCount==1&&s_executionRegistryRevision.Current()==1,
                    "full registry was mutated");
        });
        writer_test("publisher-existing-at-capacity",[](){
            require(publish(&entities[0],witness::Source::Candidate,1)==publisher::Result::Inserted,
                    "first registration failed");
            require(publish(&entities[0],witness::Source::Candidate,1)==publisher::Result::Existing,
                    "registered duplicate must survive capacity limit");
        });
        writer_test("publisher-conflicting-witness",[](){
            const auto slot=slot_for(&entities[0]);
            require(s_executionRegistrationWitnesses.Register(slot,&entities[1],witness::Source::Candidate),
                    "fixture could not install conflicting witness");
            require(publish(&entities[0],witness::Source::Candidate)==publisher::Result::WitnessRejected,
                    "conflicting witness accepted");
            require(s_executionClassCount==0&&s_executionRegistryRevision.Current()==0&&
                    !s_executionClasses[slot],"failed publication changed pointer/count/revision");
        });
        writer_test("publisher-saturated-revision",[](){
            s_executionRegistryRevision=assembly_shadow_execution_registry::Revision{
                std::numeric_limits<uint64_t>::max()};
            require(publish(&entities[0],witness::Source::Candidate)==publisher::Result::RevisionExhausted,
                    "saturated revision allowed publication");
            witness::Source observed{};
            require(s_executionRegistrationWitnesses.Matches(slot_for(&entities[0]),nullptr,observed)&&
                    s_executionClassCount==0,"saturated revision left a phantom witness");
        });
        writer_test("publisher-invalid-input",[](){
            require(publish(nullptr,witness::Source::Candidate)==publisher::Result::InvalidInput,
                    "null pointer accepted");
            require(publish(&entities[0],witness::Source::None)==publisher::Result::InvalidInput,
                    "invalid source accepted");
            require(s_executionClassCount==0&&s_executionRegistryRevision.Current()==0,
                    "invalid insertion had side effects");
        });
        writer_test("publisher-collision-probe",[](){
            // These values are never dereferenced: only the pointer hash is tested.
            auto* a=reinterpret_cast<Il2CppClass*>(static_cast<uintptr_t>(0x1000));
            auto* b=reinterpret_cast<Il2CppClass*>(static_cast<uintptr_t>(0x5000));
            require(slot_for(a)==slot_for(b),"fixture expected hash collision");
            require(publish(a,witness::Source::Candidate)==publisher::Result::Inserted,
                    "first collision publication failed");
            require(publish(b,witness::Source::ActiveShadow)==publisher::Result::Inserted,
                    "second collision publication failed");
            opaque::Scratch scratch;opaque::Receipt receipt;
            require(AssemblyShadow::TryCaptureOpaqueExecutionInventory(scratch,receipt)&&
                    receipt.candidateWitnessesBefore==1&&receipt.activeWitnessesBefore==1,
                    "collision lost a witnessed identity");
            require(s_executionClassCount==2&&s_executionRegistryRevision.Current()==2,
                    "collision count/revision changed unexpectedly");
        });
        std::cout<<"P1 OPAQUE EXECUTION INVENTORY SOURCE HOST PASS\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAILED: "<<error.what()<<"\n";return 1;
    }
}
