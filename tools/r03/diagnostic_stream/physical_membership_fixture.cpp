// R03 P1: compile the checked-in inactive physical membership source under
// explicitly controlled VM registry, metadata enumerator, and lifetime mocks.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "AssemblyShadowPhysicalMembership.h"
#include "AssemblyShadowExecutionRegistryPublisher.h"
#include "AssemblyShadowExecutionRegistryRevision.h"
#include "AssemblyShadowExecutionRegistrationWitness.h"

struct Il2CppClass { int deliberatelyUnread; };
namespace il2cpp { namespace vm {
struct ActiveSnapshot { uint64_t generation; };
enum class AssemblyShadowState { Disabled, Committed, Failed };
static ActiveSnapshot active{7},secondActive{8};
static std::atomic<const ActiveSnapshot*> s_active{&active};
static std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Committed};
static std::array<Il2CppClass*,2048> s_executionClasses{};
static size_t s_executionClassCount=0;
static assembly_shadow_execution_registry::Revision s_executionRegistryRevision{};
static assembly_shadow_execution_witness::Table s_executionRegistrationWitnesses{};
static bool gateOpen=true;
static size_t gateReaders=0,registryDepth=0,metadataDepth=0;
static int g_MetadataLock=0;
static std::vector<Il2CppClass*> enumerated;
static size_t enumRepeat=0;
static std::function<void()> afterMetadataRelease;
namespace assembly_shadow_metadata_lifetime {
struct Gate {
    struct Reader {
        bool admitted=false;
        explicit Reader(Gate&):admitted(gateOpen){if(admitted)++gateReaders;}
        ~Reader(){if(admitted)--gateReaders;}
        explicit operator bool() const noexcept{return admitted;}
    };
};
static Gate g_MetadataLifetimeGate{};
}
struct ExecutionObservationLock {
    ExecutionObservationLock(){
        if(metadataDepth)throw std::runtime_error("nested registry/metadata lock");
        ++registryDepth;
    }
    ~ExecutionObservationLock(){--registryDepth;}
};
namespace membership=assembly_shadow_physical_membership;
namespace publisher=assembly_shadow_execution_publisher;
namespace witness=assembly_shadow_execution_witness;
void il2cpp_class_for_each(void (*callback)(Il2CppClass*,void*),void* context) {
    if(metadataDepth!=1||registryDepth)throw std::runtime_error("enumerator lock violation");
    for(auto* klass:enumerated)callback(klass,context);
    for(size_t i=0;i<enumRepeat;++i)callback(enumerated.empty()?nullptr:enumerated[0],context);
}
class AssemblyShadow {
public:
    static bool TryAuditObservedPhysicalClassMembership(
        membership::Scratch&,membership::Receipt&);
};
}}
namespace il2cpp { namespace os {
struct FastAutoLock {
    explicit FastAutoLock(int*){
        if(vm::registryDepth)throw std::runtime_error("metadata lock nested with registry");
        ++vm::metadataDepth;
    }
    ~FastAutoLock(){
        --vm::metadataDepth;
        if(!vm::metadataDepth&&vm::afterMetadataRelease)vm::afterMetadataRelease();
    }
};
}}
namespace il2cpp { namespace vm {
#include "AssemblyShadowPhysicalMembershipSource.inl"
}}
using namespace il2cpp::vm;
static Il2CppClass entities[1024]{};
static void require(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}
static void reset() {
    s_active.store(&active);s_state.store(AssemblyShadowState::Committed);
    s_executionClasses.fill(nullptr);s_executionClassCount=0;
    s_executionRegistryRevision=assembly_shadow_execution_registry::Revision{};
    s_executionRegistrationWitnesses=assembly_shadow_execution_witness::Table{};
    gateOpen=true;gateReaders=registryDepth=metadataDepth=0;
    enumerated.clear();enumRepeat=0;afterMetadataRelease=nullptr;
}
static publisher::Result publish(Il2CppClass* klass,witness::Source role=witness::Source::Candidate) {
    ExecutionObservationLock lock;
    return publisher::Insert(s_executionClasses,s_executionClassCount,1024,
                             s_executionRegistrationWitnesses,s_executionRegistryRevision,
                             klass,role);
}
static size_t indexOf(Il2CppClass* klass) {
    return (reinterpret_cast<uintptr_t>(klass)>>3)%s_executionClasses.size();
}
static void test(const char* label,membership::Error expected,
                 std::function<void(membership::Scratch&)> setup={})
{
    reset(); membership::Scratch scratch;membership::Receipt receipt;
    if(setup)setup(scratch);
    const bool success=AssemblyShadow::TryAuditObservedPhysicalClassMembership(scratch,receipt);
    require(success==(expected==membership::Error::None),"wrong method verdict");
    require(receipt.error==expected,"wrong error classification");
    require(!gateReaders&&!registryDepth&&!metadataDepth,"lifetime or VM lock leak");
    require(scratch.Consumed(),"scratch not marked one-shot");
    require(!receipt.physicalClassPointerOriginProven&&
            !receipt.metadataPointerLifetimeProven&&
            !receipt.coherentVmEpochProven&&
            !receipt.executionJsonComplete&&
            !receipt.playerExecution&&!receipt.runtimeAcceptance,
            "false pointer/runtime acceptance");
    if(success){
        require(receipt.enumeratedMembershipComplete,"successful membership result unsealed");
        require(receipt.matchedClasses==receipt.observedClasses,
                "source class not matched");
        require(receipt.registryRevisionBefore==receipt.registryRevisionAfter,
                "registry revision changed");
        if(receipt.observedClasses)require(receipt.enumeratorInvoked,"enumerator not used");
        else require(!receipt.enumeratorInvoked,"empty registry invoked iterator");
    } else require(!receipt.enumeratedMembershipComplete,"failed source promoted");
    require(scratch.Observed()==0&&scratch.Enumerated()==0&&scratch.Matched()==0,
            "opaque addresses were not cleared from scratch");
    std::cout<<"PASS "<<label<<"\n";
}
int main() {
    try {
        test("empty-registry",membership::Error::None);
        test("one-enumerated",membership::Error::None,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated.push_back(&entities[0]);
        });
        test("candidate-active-enumerated",membership::Error::None,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"candidate insert");
            require(publish(&entities[1],witness::Source::ActiveShadow)==publisher::Result::Inserted,
                    "active insert");
            enumerated={&entities[0],&entities[1]};
        });
        test("unrelated-physical-class-ignored",membership::Error::None,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[2],&entities[0],&entities[3]};
        });
        test("duplicate-enumeration-not-double-counted",membership::Error::None,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[0],&entities[0]};
        });
        test("hash-collision-addresses",membership::Error::None,[](auto&){
            auto* a=reinterpret_cast<Il2CppClass*>(uintptr_t{0x1000});
            auto* b=reinterpret_cast<Il2CppClass*>(uintptr_t{0x5000});
            require(indexOf(a)==indexOf(b),"mock lacks deliberate hash collision");
            require(publish(a)==publisher::Result::Inserted,"collision A failed");
            require(publish(b,witness::Source::ActiveShadow)==publisher::Result::Inserted,
                    "collision B failed");
            enumerated={b,a}; // never dereferenced
        });
        test("unmatched-class-refused",membership::Error::UnmatchedObservedIdentity,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[1]};
        });
        test("partially-enumerated-refused",membership::Error::UnmatchedObservedIdentity,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert 0");
            require(publish(&entities[1])==publisher::Result::Inserted,"insert 1");
            enumerated={&entities[0]};
        });
        test("missing-witness-refused",membership::Error::InvalidRegistry,[](auto&){
            s_executionClasses[123]=&entities[0];s_executionClassCount=1;
        });
        test("wrong-observed-count-refused",membership::Error::InvalidRegistry,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            s_executionClassCount=0;
        });
        test("saturated-revision-refused",membership::Error::InvalidRegistry,[](auto&){
            s_executionRegistryRevision=assembly_shadow_execution_registry::Revision{
                std::numeric_limits<uint64_t>::max()};
        });
        test("admission-closed",membership::Error::AdmissionUnavailable,[](auto&){
            gateOpen=false;
        });
        test("duplicate-registry-identity",membership::Error::DuplicateObservedIdentity,[](auto&){
            s_executionClasses[0]=&entities[0];s_executionClasses[1]=&entities[0];
            s_executionClassCount=2;
            require(s_executionRegistrationWitnesses.Register(0,&entities[0],witness::Source::Candidate),
                    "first witness");
            require(s_executionRegistrationWitnesses.Register(1,&entities[0],witness::Source::Candidate),
                    "second witness");
        });
        test("null-enumerator-class",membership::Error::EnumeratorIncomplete,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={nullptr,&entities[0]};
        });
        test("enumeration-over-cap",membership::Error::EnumeratorIncomplete,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[0]};
            enumRepeat=membership::kMaximumEnumerated;
        });
        test("revision-changes-after-iteration",membership::Error::SourceChanged,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[0]};
            afterMetadataRelease=[](){
                require(publish(&entities[1])==publisher::Result::Inserted,"late insert");
            };
        });
        test("state-changes-after-iteration",membership::Error::SourceChanged,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[0]};
            afterMetadataRelease=[](){s_state.store(AssemblyShadowState::Failed);};
        });
        test("active-changes-after-iteration",membership::Error::SourceChanged,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[0]};
            afterMetadataRelease=[](){s_active.store(&secondActive);};
        });
        test("witness-changes-after-iteration",membership::Error::SourceChanged,[](auto&){
            require(publish(&entities[0])==publisher::Result::Inserted,"insert failed");
            enumerated={&entities[0]};
            afterMetadataRelease=[](){
                s_executionRegistrationWitnesses=assembly_shadow_execution_witness::Table{};
            };
        });
        test("consumed-scratch-refused",membership::Error::ReusedScratch,[](auto& scratch){
            require(scratch.Consume(),"fixture scratch pre-consume failed");
        });
        // Keep positive scratch one-shot behavior distinct from failed admission.
        reset();
        {
            membership::Scratch scratch;membership::Receipt receipt;
            require(AssemblyShadow::TryAuditObservedPhysicalClassMembership(scratch,receipt),
                    "empty initial membership failed");
            require(!AssemblyShadow::TryAuditObservedPhysicalClassMembership(scratch,receipt),
                    "consumed membership scratch reused");
            require(receipt.error==membership::Error::ReusedScratch,
                    "wrong reused-scratch error");
            std::cout<<"PASS "<<"successful-scratch-reuse-refused"<<"\n";
        }
        std::cout<<"P1 PHYSICAL ENUMERATION MEMBERSHIP SOURCE HOST PASS\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAILED: "<<error.what()<<"\n";return 1;
    }
}
