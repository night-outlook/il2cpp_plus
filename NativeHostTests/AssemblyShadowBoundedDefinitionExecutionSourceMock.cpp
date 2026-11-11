// Compiles the ACTUAL inactive VM source .inl under a pinned synthetic VM ABI.
// This is a source-execution mock, NOT Unity/IL2CPP or live metadata authority.
// Never install this file or its fake VM definitions into libil2cpp/vm.
#include "vm/AssemblyShadowBoundedDefinitionExecutionLive.h"
#include "vm/AssemblyShadowBoundedExecutionEmptyLive.h"
#include "vm/AssemblyShadowExecutionRegistryRevision.h"
#include "vm/AssemblyShadowFirstFailureV2.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

static constexpr int kTypeIndexInvalid=-1;
static constexpr int IL2CPP_TYPE_CLASS=18;
static constexpr int IL2CPP_TYPE_VALUETYPE=17;

struct Il2CppAssembly { struct {const char* name=nullptr;} aname; bool interpreter=false; };
struct Il2CppTypeDefinition {
    int declaringTypeIndex=kTypeIndexInvalid;
    int genericContainerIndex=-1;
    const char* ns=nullptr;
    const char* name=nullptr;
};
struct Il2CppGenericContainer {uint32_t type_argc=0;};
struct Il2CppImage {
    Il2CppAssembly* assembly=nullptr;
    int token=0;
    uint32_t typeCount=0;
    void* handles[8]{};
};
struct Il2CppType {
    int type=IL2CPP_TYPE_CLASS;
    struct {void* typeHandle=nullptr;} data;
    int attrs=0,num_mods=0,byref=0,pinned=0;
};
struct Il2CppClass {
    Il2CppImage* image=nullptr;
    void* typeMetadataHandle=nullptr;
    const char* namespaze=nullptr;
    const char* name=nullptr;
    int rank=0;
    void* generic_class=nullptr;
    void* declaringType=nullptr;
    Il2CppType byval_arg{};
    int32_t cctor_started=0,cctor_finished_or_no_cctor=0;
    int32_t initializationExceptionGCHandle=0;
    void* static_fields=nullptr;
};

namespace hybridclr {namespace metadata {
inline bool IsInterpreterIndex(int token){return token<0;}
}}
namespace il2cpp {namespace os {
struct FastAutoLock {
    explicit FastAutoLock(int*) noexcept {}
};
struct Atomic {
    static int32_t LoadRelaxed(const int32_t* ptr) noexcept {return *ptr;}
};
}}
namespace il2cpp {namespace vm {
using assembly_shadow_execution_registry::Revision;
using assembly_shadow_execution_witness::Table;
enum class AssemblyShadowState:int32_t {
    Disabled=0, CandidatesRegistered=1, Staging=2, Staged=3,
    Validated=4, Committing=5, Committed=6, Aborted=7,
    Failed=8, FailedAfterCommit=9
};
struct ActiveSnapshot {
    uint64_t generation=1;
    std::set<const Il2CppAssembly*> byAssembly;
    std::set<const Il2CppAssembly*> shadowToBaseline;
};
class AssemblyShadowDiagnostics {
public:
    static const char* StateName(AssemblyShadowState state) noexcept {
        return state==AssemblyShadowState::Committed ? "Committed" : "CandidatesRegistered";
    }
};

static std::array<Il2CppClass*,2048> s_executionClasses{};
static size_t s_executionClassCount=0;
static Revision s_executionRegistryRevision{};
static Table s_executionRegistrationWitnesses{};
static std::mutex s_observationLock;
struct ExecutionObservationLock {
    std::lock_guard<std::mutex> held;
    ExecutionObservationLock():held(s_observationLock){}
};
static std::atomic<const ActiveSnapshot*> s_active{nullptr};
static std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::CandidatesRegistered};
static assembly_shadow_first_failure_v2::FirstErrorV2 s_firstFailureV2;
namespace assembly_shadow_metadata_lifetime {
    Gate g_MetadataLifetimeGate;
}
static int g_MetadataLock=0;
static bool g_emitPhysical=true, g_duplicate=false;
static bool g_changeRevision=false, g_changeCounter=false;
static bool g_publishB1DuringEnumeration=false, g_midB1Published=false;
static Il2CppClass* g_physical=nullptr;
static Il2CppGenericContainer g_container{};
static Il2CppType* g_declaringType=nullptr;

class MetadataCache {
public:
    static void* GetAssemblyTypeHandle(const Il2CppImage* image,uint32_t index){
        return image && index<image->typeCount && index<8 ? image->handles[index]:nullptr;
    }
};
class GlobalMetadata {
public:
    static std::pair<const char*,const char*> GetTypeNamespaceAndName(void* handle){
        auto* def=static_cast<Il2CppTypeDefinition*>(handle);
        return {def->ns,def->name};
    }
    static Il2CppGenericContainer* GetGenericContainerFromIndex(int index){
        return index<0 ? nullptr : &g_container;
    }
    static uint32_t GetGenericContainerCount(Il2CppGenericContainer* p){
        return p ? p->type_argc : 0;
    }
    static const Il2CppType* GetIl2CppTypeFromIndex(int index){
        if(index<0)std::abort(); // source MUST reject staged ancestry first
        return index==3 ? g_declaringType : nullptr;
    }
};
static bool Interpreter(const Il2CppAssembly* owner){return owner->interpreter;}
static void il2cpp_class_for_each(void(*callback)(Il2CppClass*,void*),void* context){
    if(g_emitPhysical)callback(g_physical,context);
    if(g_duplicate && g_emitPhysical)callback(g_physical,context);
    if(g_publishB1DuringEnumeration) {
        static const char firstReason[]="mid-capture native B1 first error";
        g_midB1Published=s_firstFailureV2.TryFreeze(
            19,assembly_shadow_first_failure_v2::Origin::InterpreterInitializer,
            firstReason,sizeof firstReason-1);
    }
    if(g_changeRevision)s_executionRegistryRevision.Advance();
    if(g_changeCounter)
        assembly_shadow_r02::ObservationCounters::Add(
            assembly_shadow_r02::Metric::MethodChecks,1);
}
class AssemblyShadow {
public:
    static bool TryCaptureBoundedDefinitionExecutionLive(
        assembly_shadow_physical_membership::Scratch&,
        ::r03_b2_owned_execution_rows::Rows&,
        assembly_shadow_frozen_execution_record::Record&,
        assembly_shadow_bounded_definition_execution::Receipt&);
};
#include "vm/AssemblyShadowBoundedDefinitionExecutionLiveSource.inl"

static void Register(Il2CppClass* klass) {
    // Same power-of-two, open-addressed slot policy used by the production
    // ExecutionRegistryPublisher. No physical field read occurs at this step.
    size_t slot=(reinterpret_cast<uintptr_t>(klass)>>3)%s_executionClasses.size();
    while(s_executionClasses[slot])slot=(slot+1)%s_executionClasses.size();
    s_executionClasses[slot]=klass;
    const bool registered=s_executionRegistrationWitnesses.Register(
        slot,klass,assembly_shadow_execution_witness::Source::Candidate);
    if(!registered || !s_executionRegistryRevision.Advance())std::abort();
    ++s_executionClassCount;
}

}} // il2cpp::vm

static int assertions=0,cases=0;
static void Check(bool value){++assertions;if(!value)std::abort();}
using namespace il2cpp::vm;
namespace src=il2cpp::vm::assembly_shadow_bounded_definition_execution;
namespace rows=::r03_b2_owned_execution_rows;

struct Fixture {
    Il2CppAssembly assembly{};
    Il2CppTypeDefinition def{};
    Il2CppTypeDefinition outer{};
    Il2CppType declaring{};
    Il2CppImage image{};
    Il2CppClass klass{};
    ActiveSnapshot active{};
    Fixture() {
        assembly.aname.name="Methods";
        def.ns="Demo";def.name="Hero";
        outer.ns="Demo";outer.name="Outer";
        declaring.data.typeHandle=&outer;
        image.assembly=&assembly;image.token=0;image.typeCount=1;
        image.handles[0]=&def;
        klass.image=&image;klass.typeMetadataHandle=&def;
        klass.namespaze=def.ns;klass.name=def.name;
        klass.byval_arg.type=IL2CPP_TYPE_CLASS;
        klass.byval_arg.data.typeHandle=&def;
        klass.cctor_started=1;klass.static_fields=&def;
        active.generation=7;active.byAssembly.insert(&assembly);
    }
};
enum class Mode {
    Good, GenericDefinition, BadImageOwner, ConstructedGeneric,
    NestedType, WrongNestedOwner, CyclicNested, InvalidDeclaringShape,
    StagedNestedIndex,
    UnsupportedImage, SourceChanged, CounterChanged,
    TerminalDuringCapture,
    MissingEnumeration, DuplicateEnumeration, BadAssembly
};

static void Exercise(Mode mode,src::Error expectedError) {
    ++cases;
    Fixture fixture;
    s_executionClasses.fill(nullptr);
    s_executionClassCount=0;
    s_executionRegistryRevision=Revision{};
    s_executionRegistrationWitnesses=Table{};
    g_emitPhysical=true;g_duplicate=false;
    g_changeRevision=false;g_changeCounter=false;
    g_publishB1DuringEnumeration=false;g_midB1Published=false;
    g_declaringType=&fixture.declaring;
    s_active.store(&fixture.active);
    s_state.store(AssemblyShadowState::Committed);
    g_physical=&fixture.klass;
    if(mode==Mode::GenericDefinition)fixture.def.genericContainerIndex=0;
    if(mode==Mode::BadImageOwner)fixture.image.handles[0]=nullptr;
    if(mode==Mode::ConstructedGeneric)fixture.klass.generic_class=&fixture.def;
    if(mode==Mode::NestedType || mode==Mode::WrongNestedOwner ||
       mode==Mode::CyclicNested || mode==Mode::InvalidDeclaringShape ||
       mode==Mode::StagedNestedIndex) {
        fixture.def.declaringTypeIndex=3;
        fixture.klass.declaringType=&fixture.outer;
        if(mode==Mode::NestedType) {
            fixture.image.typeCount=2;
            fixture.image.handles[1]=&fixture.outer;
        }
        if(mode==Mode::CyclicNested)
            fixture.declaring.data.typeHandle=&fixture.def;
        if(mode==Mode::InvalidDeclaringShape)
            fixture.declaring.type=42;
        if(mode==Mode::StagedNestedIndex)
            fixture.def.declaringTypeIndex=-2;
    }
    if(mode==Mode::UnsupportedImage)fixture.image.typeCount=8193;
    if(mode==Mode::SourceChanged)g_changeRevision=true;
    if(mode==Mode::CounterChanged)g_changeCounter=true;
    if(mode==Mode::TerminalDuringCapture)g_publishB1DuringEnumeration=true;
    if(mode==Mode::MissingEnumeration)g_emitPhysical=false;
    if(mode==Mode::DuplicateEnumeration)g_duplicate=true;
    if(mode==Mode::BadAssembly)fixture.assembly.aname.name="B\xC3\xA9";
    Register(&fixture.klass);

    assembly_shadow_physical_membership::Scratch membership{};
    auto* owned=new rows::Rows();
    auto* record=new assembly_shadow_frozen_execution_record::Record();
    src::Receipt receipt{};
    const bool success=AssemblyShadow::TryCaptureBoundedDefinitionExecutionLive(
        membership,*owned,*record,receipt);
    if(expectedError==src::Error::None) {
        Check(success && receipt.error==src::Error::None);
        Check(receipt.registered==1 && receipt.physicallyMatched==1);
        Check(receipt.serializedRows==1 && receipt.ownedBytes>0);
        Check(receipt.callbackPhysicalMembershipProven);
        Check(receipt.restrictedTypeDefSubsetProven);
        Check(receipt.observedRegistryAndCounterFenceStable);
        Check(receipt.admittedMetadataEpoch==1);
        Check(owned->Complete() && record->Complete());
        Check(!receipt.completeArbitraryTypeKeys);
        Check(!receipt.completeExecutionDiagnostics);
        Check(!receipt.coherentVmEpochProven && !receipt.runtimeAcceptance);
        const std::string body(record->Data(),record->Bytes());
        const char* key=(mode==Mode::GenericDefinition)
            ? "type(7:methods/4:Demo/4:Hero@2)"
            : mode==Mode::NestedType
                ? "type(7:methods/4:Demo/5:Outer@0/4:Demo/4:Hero@0)"
                : "type(7:methods/4:Demo/4:Hero@0)";
        Check(body.find(key)!=std::string::npos);
        Check(body.find("\"logicalAssembly\":\"Methods\"")!=std::string::npos);
        Check(body.find("\"r02\":{\"schemaVersion\":1")!=std::string::npos);
        Check(body.find("\"cctorStarted\":true")!=std::string::npos);
        Check(body.find("\"staticStorageAvailable\":true")!=std::string::npos);
#if IL2CPP_DEBUG
        Check(body.find("\"pointerDetailsAvailable\":true")!=std::string::npos);
        Check(body.find("\"staticStoragePointer\":\"0x")!=std::string::npos);
#else
        Check(body.find("\"pointerDetailsAvailable\":false")!=std::string::npos);
        Check(body.find("\"staticStoragePointer\":\"\"")!=std::string::npos);
#endif
    } else {
        Check(!success && receipt.error==expectedError);
        Check(!record->Complete() && record->Data()==nullptr);
        Check(!owned->Complete() && owned->Count()==0);
        Check(!receipt.callbackPhysicalMembershipProven);
        Check(!receipt.completeExecutionDiagnostics && !receipt.runtimeAcceptance);
    }
    delete record;
    delete owned;
    s_active.store(nullptr);
}
int main(){
    g_container.type_argc=2;
    Check(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit());
    Exercise(Mode::Good,src::Error::None);
    Exercise(Mode::GenericDefinition,src::Error::None);
    Exercise(Mode::BadImageOwner,src::Error::InvalidTypeIdentity);
    Exercise(Mode::ConstructedGeneric,src::Error::UnsupportedTypeShape);
    Exercise(Mode::NestedType,src::Error::None);
    Exercise(Mode::WrongNestedOwner,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::CyclicNested,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::InvalidDeclaringShape,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::StagedNestedIndex,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::UnsupportedImage,src::Error::UnsupportedImage);
    Exercise(Mode::SourceChanged,src::Error::SourceChanged);
    Exercise(Mode::CounterChanged,src::Error::CounterChanged);
    Exercise(Mode::MissingEnumeration,src::Error::UnmatchedClass);
    Exercise(Mode::DuplicateEnumeration,src::Error::BadPhysicalEnumeration);
    Exercise(Mode::BadAssembly,src::Error::TypeKeyFailed);
    // Original B1 producer can publish a first terminal error independently
    // while the metadata callback is executing. Reject the full result even
    // if the execution-registry and R02 revisions did not change.
    Exercise(Mode::TerminalDuringCapture,src::Error::SourceChanged);
    Check(g_midB1Published);
    // A previously published native first failure stays terminal forever;
    // do not reconstruct a source field after it was frozen.
    Exercise(Mode::Good,src::Error::UnsupportedTypeShape);
    assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();
    // A stale registry from an old initial runtime epoch cannot be re-admitted
    // merely because Runtime::Init reopened the metadata gate.
    Check(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit());
    Exercise(Mode::Good,src::Error::NoMetadataAdmission);
    assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();
    std::printf("{\"kind\":\"R03B2ActualNativeSourceMock\","
        "\"result\":\"PassedActualInlWithMockedVmOnly\","
        "\"cases\":%d,\"assertions\":%d,\"realUnity\":false,"
        "\"physicalClassProvenInRealPlayer\":false,"
        "\"fullCanonicalTypeKeys\":false,\"nativeV2TransportImplemented\":false,"
        "\"runtimeAcceptance\":false,\"R03Accepted\":false}\n",cases,assertions);
}
