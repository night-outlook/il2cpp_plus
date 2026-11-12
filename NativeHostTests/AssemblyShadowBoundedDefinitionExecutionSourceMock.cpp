// Compiles the ACTUAL inactive VM source .inl under a pinned synthetic VM ABI.
// This is a source-execution mock, NOT Unity/IL2CPP or live metadata authority.
// Never install this file or its fake VM definitions into libil2cpp/vm.
#include "vm/AssemblyShadowBoundedDefinitionExecutionLive.h"
#include "vm/AssemblyShadowBoundedExecutionEmptyLive.h"
#include "vm/AssemblyShadowExecutionRegistryRevision.h"
#include "vm/AssemblyShadowFirstFailureV2.h"
#include "vm/AssemblyShadowRegisteredAotImageScan.h"
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
#include "AssemblyShadowFoldedNameMock.h"

static constexpr int kTypeIndexInvalid=-1;
static constexpr int kGenericContainerIndexInvalid=-1;
using Il2CppTypeEnum=int;
static constexpr int IL2CPP_TYPE_VOID=1;
static constexpr int IL2CPP_TYPE_BOOLEAN=2;
static constexpr int IL2CPP_TYPE_CHAR=3;
static constexpr int IL2CPP_TYPE_I1=4;
static constexpr int IL2CPP_TYPE_U1=5;
static constexpr int IL2CPP_TYPE_I2=6;
static constexpr int IL2CPP_TYPE_U2=7;
static constexpr int IL2CPP_TYPE_I4=8;
static constexpr int IL2CPP_TYPE_U4=9;
static constexpr int IL2CPP_TYPE_I8=10;
static constexpr int IL2CPP_TYPE_U8=11;
static constexpr int IL2CPP_TYPE_R4=12;
static constexpr int IL2CPP_TYPE_R8=13;
static constexpr int IL2CPP_TYPE_STRING=14;
static constexpr int IL2CPP_TYPE_TYPEDBYREF=22;
static constexpr int IL2CPP_TYPE_I=24;
static constexpr int IL2CPP_TYPE_U=25;
static constexpr int IL2CPP_TYPE_OBJECT=28;
static constexpr int IL2CPP_TYPE_IL2CPP_TYPE_INDEX=100;
static constexpr int IL2CPP_TYPE_CLASS=18;
static constexpr int IL2CPP_TYPE_VALUETYPE=17;
static constexpr int IL2CPP_TYPE_GENERICINST=21;
static constexpr int IL2CPP_TYPE_PTR=15;
static constexpr int IL2CPP_TYPE_ARRAY=20;
static constexpr int IL2CPP_TYPE_SZARRAY=29;

struct Il2CppImage;
struct Il2CppAssembly { struct {const char* name=nullptr;} aname; int token=1; Il2CppImage* image=nullptr; bool interpreter=false; };
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
struct Il2CppGenericClass;
struct Il2CppArrayType;
struct Il2CppType {
    int type=IL2CPP_TYPE_CLASS;
    union Data {
        void* typeHandle;
        const Il2CppType* type;
        Il2CppGenericClass* generic_class;
        const Il2CppArrayType* array;
        Data() noexcept:typeHandle(nullptr){}
    } data;
    int attrs=0,num_mods=0,byref=0,pinned=0;
};
struct Il2CppArrayType {
    const Il2CppType* etype=nullptr;
    uint8_t rank=0,numsizes=0,numlobounds=0;
    int* sizes=nullptr;
    int* lobounds=nullptr;
};
struct Il2CppGenericInst {
    uint32_t type_argc=0;
    const Il2CppType** type_argv=nullptr;
};
struct Il2CppGenericContext {
    const Il2CppGenericInst* class_inst=nullptr;
    const Il2CppGenericInst* method_inst=nullptr;
};
struct Il2CppGenericClass {
    const Il2CppType* type=nullptr;
    Il2CppGenericContext context{};
};
struct Il2CppClass {
    Il2CppImage* image=nullptr;
    void* typeMetadataHandle=nullptr;
    const char* namespaze=nullptr;
    const char* name=nullptr;
    int rank=0;
    Il2CppGenericClass* generic_class=nullptr;
    void* genericContainerHandle=nullptr;
    void* declaringType=nullptr;
    Il2CppType byval_arg{};
    int32_t cctor_started=0,cctor_finished_or_no_cctor=0;
    int32_t initializationExceptionGCHandle=0;
    void* static_fields=nullptr;
};

struct Il2CppDefaults {
    Il2CppClass* object_class=nullptr;
    Il2CppClass* void_class=nullptr;
    Il2CppClass* boolean_class=nullptr;
    Il2CppClass* char_class=nullptr;
    Il2CppClass* sbyte_class=nullptr;
    Il2CppClass* byte_class=nullptr;
    Il2CppClass* int16_class=nullptr;
    Il2CppClass* uint16_class=nullptr;
    Il2CppClass* int32_class=nullptr;
    Il2CppClass* uint32_class=nullptr;
    Il2CppClass* int_class=nullptr;
    Il2CppClass* uint_class=nullptr;
    Il2CppClass* int64_class=nullptr;
    Il2CppClass* uint64_class=nullptr;
    Il2CppClass* single_class=nullptr;
    Il2CppClass* double_class=nullptr;
    Il2CppClass* string_class=nullptr;
    Il2CppClass* typed_reference_class=nullptr;
    Il2CppClass* systemtype_class=nullptr;
};
// Fake default data only; compile the ACTUAL no-assert production Peek.
static Il2CppDefaults il2cpp_defaults{};
#include "vm/AssemblyShadowExistingPrimitiveDefaults.h"

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
static bool g_dropSecondPass=false;
static unsigned g_physicalPasses=0;
static bool g_changeRevision=false, g_changeCounter=false;
static bool g_publishB1DuringEnumeration=false, g_midB1Published=false;
static Il2CppClass* g_physical=nullptr;
static Il2CppClass* g_otherPhysical=nullptr;
static bool g_reversePhysicalCallbacks=false;
static Il2CppGenericContainer g_container{};
static Il2CppType* g_declaringType=nullptr;
// Mock of the real Assembly.cpp unfiltered physical registry. The visitor
// itself uses the ACTUAL pure Scan header shared with production Assembly.cpp.
static std::array<const Il2CppAssembly*,1100> g_rawAssemblies{};
static size_t g_rawCount=0,g_rawVisits=0;
static bool g_dropRegisteredOnSecond=false;
static std::array<Il2CppAssembly,1025> g_extraAssemblies{};
static std::array<Il2CppImage,1025> g_extraImages{};
class Assembly {
public:
    static bool TryVisitRegisteredAotImages(
        bool(*visit)(const Il2CppImage*,void*),void* context) noexcept {
        if(!visit)return false;
        ++g_rawVisits;
        const size_t count=(g_dropRegisteredOnSecond&&g_rawVisits==2&&g_rawCount>1)
            ? g_rawCount-1:g_rawCount;
        return assembly_shadow_registered_aot_scan::Scan(
            g_rawAssemblies.begin(),g_rawAssemblies.begin()+count,
            [](const Il2CppImage* image) noexcept -> bool {
                return image->token<0;
            },
            [=](const Il2CppImage* image) noexcept -> bool {
                return visit(image,context);
            });
    }
};

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
        if(index<kGenericContainerIndexInvalid)std::abort(); // Guard must run first.
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
    ++g_physicalPasses;
    if(g_emitPhysical && !(g_dropSecondPass && g_physicalPasses==2)) {
        if(g_reversePhysicalCallbacks && g_otherPhysical)
            callback(g_otherPhysical,context);
        callback(g_physical,context);
        if(!g_reversePhysicalCallbacks && g_otherPhysical)
            callback(g_otherPhysical,context);
    }
    if(g_duplicate && g_emitPhysical)callback(g_physical,context);
    if(g_publishB1DuringEnumeration && !g_midB1Published) {
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

static int assertions=0,cases=0,currentMode=-1;
static void CheckAt(bool value,int line,const char* expression){
    ++assertions;
    if(!value){
        std::fprintf(stderr,"R03 B2 source-mock assertion failed: mode=%d check=%d line=%d expr=%s\\n",
                     currentMode,assertions,line,expression);
        std::abort();
    }
}
#define Check(expression) CheckAt(bool(expression),__LINE__,#expression)
using namespace il2cpp::vm;
namespace src=il2cpp::vm::assembly_shadow_bounded_definition_execution;
namespace rows=::r03_b2_owned_execution_rows;

struct Fixture {
    Il2CppAssembly assembly{};
    Il2CppAssembly supportAssembly{};
    Il2CppTypeDefinition def{};
    Il2CppTypeDefinition outer{};
    Il2CppTypeDefinition secondary{};
    Il2CppTypeDefinition primitiveDefinition{};
    Il2CppType declaring{};
    Il2CppType genericBase{},genericArg1{},genericArg2{};
    Il2CppType nestedBase{},compositeLeaf{},chain[11]{};
    const Il2CppType* nestedArgv[2]{};
    Il2CppGenericInst nestedInstance{};
    Il2CppGenericClass nestedClass{};
    Il2CppArrayType arrayType{};
    int arraySizes[2]{4,5},arrayBounds[2]{-1,0};
    const Il2CppType* genericArgv[2]{};
    Il2CppGenericInst genericInstance{};
    Il2CppGenericClass genericClass{};
    Il2CppImage image{};
    Il2CppImage supportImage{};
    Il2CppClass klass{};
    Il2CppClass other{};
    Il2CppClass primitiveClass{};
    ActiveSnapshot active{};
    Fixture() {
        assembly.aname.name="Methods";
        supportAssembly.aname.name="Support";
        def.ns="Demo";def.name="Hero";
        outer.ns="Demo";outer.name="Outer";
        secondary.ns="Demo";secondary.name="Friend";
        primitiveDefinition.ns="System";primitiveDefinition.name="Int32";
        declaring.data.typeHandle=&outer;
        genericBase.type=IL2CPP_TYPE_CLASS;
        genericBase.data.typeHandle=&def;
        genericArg1.data.typeHandle=&secondary;
        genericArg2.data.typeHandle=&secondary;
        genericArgv[0]=&genericArg1;
        genericArgv[1]=&genericArg2;
        genericInstance.type_argc=2;
        genericInstance.type_argv=genericArgv;
        genericClass.type=&genericBase;
        genericClass.context.class_inst=&genericInstance;
        nestedBase.type=IL2CPP_TYPE_CLASS;
        nestedBase.data.typeHandle=&secondary;
        nestedArgv[0]=nestedArgv[1]=&genericArg2;
        nestedInstance.type_argc=2;
        nestedInstance.type_argv=nestedArgv;
        nestedClass.type=&nestedBase;
        nestedClass.context.class_inst=&nestedInstance;
        arrayType.etype=&genericArg2;
        arrayType.rank=2;
        arrayType.numsizes=2;
        arrayType.numlobounds=2;
        arrayType.sizes=arraySizes;
        arrayType.lobounds=arrayBounds;
        image.assembly=&assembly;assembly.image=&image;
        image.token=0;image.typeCount=1;
        supportImage.assembly=&supportAssembly;
        supportAssembly.image=&supportImage;supportImage.token=0;
        supportImage.typeCount=1;supportImage.handles[0]=&secondary;
        primitiveClass.image=&supportImage;
        primitiveClass.typeMetadataHandle=&primitiveDefinition;
        primitiveClass.name=primitiveDefinition.name;
        primitiveClass.namespaze=primitiveDefinition.ns;
        image.handles[0]=&def;
        klass.image=&image;klass.typeMetadataHandle=&def;
        klass.namespaze=def.ns;klass.name=def.name;
        klass.byval_arg.type=IL2CPP_TYPE_CLASS;
        klass.byval_arg.data.typeHandle=&def;
        klass.cctor_started=1;klass.static_fields=&def;
        other.image=&image;other.typeMetadataHandle=&secondary;
        other.namespaze=secondary.ns;other.name=secondary.name;
        other.byval_arg.type=IL2CPP_TYPE_CLASS;
        other.byval_arg.data.typeHandle=&secondary;
        other.cctor_started=1;other.static_fields=&secondary;
        active.generation=7;active.byAssembly.insert(&assembly);
    }
};
enum class Mode {
    Good, GenericDefinition, StagedGenericContainer,
    GenericSameImage, GenericNestedArgument, GenericArgumentMissingOwner,
    CrossImageGeneric, CrossImageGenericUnobserved,
    CrossImageGenericNotRegistered, CrossImageGenericAmbiguous,
    CrossImageInterpreter, RegisteredAotChanged,
    RegisteredAotOverflow, RegisteredAotInvalidOwner,
    RegisteredEmptyAotAssembly,
    CrossImageNestedArgument,
    GenericWrongArity, GenericTooManyArguments,
    GenericQualifiedArgument, GenericConstructedArgument,
    PrimitiveDefault, PrimitiveQualified, PrimitiveMissingDefault,
    PrimitiveUnregistered, PrimitiveWrongHandle, PrimitiveWrongImage,
    PrimitiveUnsupportedTag, PrimitiveAmbiguousOwner, PrimitiveGenericArity,
    PrimitiveClassNameMismatch, PrimitiveClassNested,
    PrimitiveClassGenericContainer,
    GenericPointerArgument, GenericSzArrayArgument,
    GenericArrayArgument, GenericQualifiedPointerArgument,
    GenericArrayBadSizes, GenericArrayMissingElement,
    GenericArrayTooManyDimensions, GenericCompositeTooDeep,
    GenericMissingArguments, GenericMissingContext,
    GenericBadMethodContext, GenericWrongBase,
    TwoClassesReversedPhysicalCallback,
    UnicodeAssembly, UnicodePathVersionAssembly, UnicodeSupplementaryAssembly,
    InvalidUnicodeOverlong, InvalidUnicodeSurrogate,
    BadImageOwner, ConstructedGeneric,
    NestedType, WrongNestedOwner, CyclicNested, InvalidDeclaringShape,
    StagedNestedIndex,
    UnsupportedImage, SourceChanged, CounterChanged,
    MissingSecondPass,
    TerminalDuringCapture,
    MissingEnumeration, DuplicateEnumeration, BadAssembly
};

static void Exercise(Mode mode,src::Error expectedError) {
    ++cases;
    currentMode=static_cast<int>(mode);
    Fixture fixture;
    s_executionClasses.fill(nullptr);
    s_executionClassCount=0;
    s_executionRegistryRevision=Revision{};
    s_executionRegistrationWitnesses=Table{};
    g_emitPhysical=true;g_duplicate=false;
    g_physicalPasses=0;g_dropSecondPass=false;
    g_rawAssemblies.fill(nullptr);g_rawCount=0;g_rawVisits=0;
    g_dropRegisteredOnSecond=false;
    g_rawAssemblies[g_rawCount++]=&fixture.assembly;
    il2cpp_defaults=Il2CppDefaults{};
    g_changeRevision=false;g_changeCounter=false;
    g_publishB1DuringEnumeration=false;g_midB1Published=false;
    g_declaringType=&fixture.declaring;
    g_otherPhysical=nullptr;g_reversePhysicalCallbacks=false;
    s_active.store(&fixture.active);
    s_state.store(AssemblyShadowState::Committed);
    g_physical=&fixture.klass;
    if(mode==Mode::GenericDefinition)fixture.def.genericContainerIndex=0;
    if(mode==Mode::GenericSameImage || mode==Mode::GenericNestedArgument ||
       mode==Mode::GenericArgumentMissingOwner ||
       mode==Mode::GenericWrongArity || mode==Mode::GenericTooManyArguments ||
       mode==Mode::PrimitiveDefault ||
       mode==Mode::PrimitiveQualified ||
       mode==Mode::PrimitiveMissingDefault ||
       mode==Mode::PrimitiveUnregistered ||
       mode==Mode::PrimitiveWrongHandle ||
       mode==Mode::PrimitiveWrongImage ||
       mode==Mode::PrimitiveUnsupportedTag ||
       mode==Mode::PrimitiveAmbiguousOwner ||
       mode==Mode::PrimitiveGenericArity ||
       mode==Mode::PrimitiveClassNameMismatch ||
       mode==Mode::PrimitiveClassNested ||
       mode==Mode::PrimitiveClassGenericContainer ||
       mode==Mode::GenericQualifiedArgument ||
       mode==Mode::GenericConstructedArgument ||
       mode==Mode::GenericPointerArgument ||
       mode==Mode::GenericSzArrayArgument ||
       mode==Mode::GenericArrayArgument ||
       mode==Mode::GenericQualifiedPointerArgument ||
       mode==Mode::GenericArrayBadSizes ||
       mode==Mode::GenericArrayMissingElement ||
       mode==Mode::GenericArrayTooManyDimensions ||
       mode==Mode::GenericCompositeTooDeep || mode==Mode::GenericMissingArguments ||
       mode==Mode::GenericMissingContext ||
       mode==Mode::GenericBadMethodContext || mode==Mode::GenericWrongBase) {
        fixture.def.genericContainerIndex=0;
        fixture.image.typeCount=2;
        fixture.image.handles[1]=&fixture.secondary;
        fixture.klass.byval_arg.type=IL2CPP_TYPE_GENERICINST;
        fixture.klass.byval_arg.data.generic_class=&fixture.genericClass;
        fixture.klass.generic_class=&fixture.genericClass;
        if(mode==Mode::GenericArgumentMissingOwner)
            fixture.image.handles[1]=nullptr;
        if(mode==Mode::GenericNestedArgument) {
            fixture.secondary.declaringTypeIndex=3;
            fixture.image.typeCount=3;
            fixture.image.handles[2]=&fixture.outer;
        }
        if(mode==Mode::GenericTooManyArguments)
            fixture.genericInstance.type_argc=
                static_cast<uint32_t>(
                    r03_b2_owned_constructed_type_key::Capture::kMaximumArguments+1);
        if(mode==Mode::GenericWrongArity)
            fixture.genericInstance.type_argc=1;
        if(mode==Mode::GenericQualifiedArgument)
            fixture.genericArg1.byref=1;
        if(mode==Mode::GenericConstructedArgument) {
            fixture.secondary.genericContainerIndex=0;
            fixture.genericArg1.type=IL2CPP_TYPE_GENERICINST;
            fixture.genericArg1.data.generic_class=&fixture.nestedClass;
        }
        if(mode==Mode::GenericPointerArgument ||
           mode==Mode::GenericQualifiedPointerArgument) {
            fixture.genericArg1.type=IL2CPP_TYPE_PTR;
            fixture.genericArg1.data.type=&fixture.genericArg2;
            if(mode==Mode::GenericQualifiedPointerArgument) {
                fixture.genericArg1.attrs=7;
                fixture.genericArg1.num_mods=2;
                fixture.genericArg1.byref=1;
            }
        }
        if(mode==Mode::GenericSzArrayArgument) {
            fixture.genericArg1.type=IL2CPP_TYPE_SZARRAY;
            fixture.genericArg1.data.type=&fixture.genericArg2;
        }
        if(mode==Mode::GenericArrayArgument ||
           mode==Mode::GenericArrayBadSizes ||
           mode==Mode::GenericArrayMissingElement ||
           mode==Mode::GenericArrayTooManyDimensions) {
            fixture.genericArg1.type=IL2CPP_TYPE_ARRAY;
            fixture.genericArg1.data.array=&fixture.arrayType;
            if(mode==Mode::GenericArrayBadSizes)
                fixture.arrayType.sizes=nullptr;
            if(mode==Mode::GenericArrayMissingElement)
                fixture.arrayType.etype=nullptr;
            if(mode==Mode::GenericArrayTooManyDimensions)
                fixture.arrayType.numsizes=33;
        }
        if(mode==Mode::GenericCompositeTooDeep) {
            fixture.genericArg1.type=IL2CPP_TYPE_PTR;
            fixture.genericArg1.data.type=&fixture.chain[0];
            for(size_t i=0;i<10;++i) {
                fixture.chain[i].type=IL2CPP_TYPE_PTR;
                fixture.chain[i].data.type=&fixture.chain[i+1];
            }
            fixture.chain[10].type=IL2CPP_TYPE_CLASS;
            fixture.chain[10].data.typeHandle=&fixture.secondary;
        }
        if(mode==Mode::GenericMissingArguments)
            fixture.genericInstance.type_argv=nullptr;
        if(mode==Mode::GenericMissingContext)
            fixture.genericClass.context.class_inst=nullptr;
        if(mode==Mode::GenericBadMethodContext)
            fixture.genericClass.context.method_inst=&fixture.genericInstance;
        if(mode==Mode::GenericWrongBase)
            fixture.genericBase.data.typeHandle=&fixture.secondary;
    }
    if(mode==Mode::CrossImageGeneric ||
       mode==Mode::CrossImageGenericUnobserved ||
       mode==Mode::CrossImageGenericNotRegistered ||
       mode==Mode::CrossImageGenericAmbiguous ||
       mode==Mode::RegisteredAotChanged ||
       mode==Mode::CrossImageInterpreter ||
       mode==Mode::CrossImageNestedArgument) {
        fixture.def.genericContainerIndex=0;
        fixture.klass.byval_arg.type=IL2CPP_TYPE_GENERICINST;
        fixture.klass.byval_arg.data.generic_class=&fixture.genericClass;
        fixture.klass.generic_class=&fixture.genericClass;
        fixture.other.image=&fixture.supportImage;
        g_otherPhysical=&fixture.other;
        if(mode!=Mode::CrossImageGenericNotRegistered)
            g_rawAssemblies[g_rawCount++]=&fixture.supportAssembly;
        if(mode==Mode::RegisteredAotChanged)
            g_dropRegisteredOnSecond=true;
        g_reversePhysicalCallbacks=true;
        if(mode==Mode::CrossImageGenericUnobserved)
            g_otherPhysical=nullptr; // No actual matched callback owner
        if(mode==Mode::CrossImageGenericAmbiguous) {
            fixture.image.typeCount=2;
            fixture.image.handles[1]=&fixture.secondary; // Same TypeDef in two images
        }
        if(mode==Mode::CrossImageInterpreter)
            fixture.supportImage.token=-2;
        if(mode==Mode::CrossImageNestedArgument) {
            fixture.secondary.declaringTypeIndex=3;
            fixture.supportImage.typeCount=2;
            fixture.supportImage.handles[1]=&fixture.outer;
        }
    }

    const bool primitiveMode=
        mode==Mode::PrimitiveDefault || mode==Mode::PrimitiveQualified ||
        mode==Mode::PrimitiveMissingDefault || mode==Mode::PrimitiveUnregistered ||
        mode==Mode::PrimitiveWrongHandle || mode==Mode::PrimitiveWrongImage ||
        mode==Mode::PrimitiveUnsupportedTag || mode==Mode::PrimitiveAmbiguousOwner ||
        mode==Mode::PrimitiveGenericArity ||
        mode==Mode::PrimitiveClassNameMismatch ||
        mode==Mode::PrimitiveClassNested ||
        mode==Mode::PrimitiveClassGenericContainer;
    if(primitiveMode) {
        fixture.genericArg1.type=IL2CPP_TYPE_I4;
        fixture.genericArg1.data.typeHandle=nullptr;
        fixture.supportImage.typeCount=2;
        // The second concrete generic argument is a Methods TypeDef;
        // it must not also be spuriously duplicated in Support.
        fixture.supportImage.handles[0]=nullptr;
        fixture.supportImage.handles[1]=&fixture.primitiveDefinition;
        g_rawAssemblies[g_rawCount++]=&fixture.supportAssembly;
        il2cpp_defaults.int32_class=&fixture.primitiveClass;
        if(mode==Mode::PrimitiveQualified)fixture.genericArg1.byref=1;
        if(mode==Mode::PrimitiveMissingDefault)il2cpp_defaults.int32_class=nullptr;
        if(mode==Mode::PrimitiveUnregistered)--g_rawCount;
        if(mode==Mode::PrimitiveWrongHandle)
            fixture.primitiveClass.typeMetadataHandle=&fixture.outer;
        if(mode==Mode::PrimitiveWrongImage)
            fixture.primitiveClass.image=&fixture.image;
        if(mode==Mode::PrimitiveUnsupportedTag)fixture.genericArg1.type=42;
        if(mode==Mode::PrimitiveAmbiguousOwner) {
            fixture.image.typeCount=2;
            fixture.image.handles[1]=&fixture.primitiveDefinition;
        }
        if(mode==Mode::PrimitiveGenericArity)
            fixture.primitiveDefinition.genericContainerIndex=0;
        if(mode==Mode::PrimitiveClassNameMismatch)
            fixture.primitiveClass.name="Other";
        if(mode==Mode::PrimitiveClassNested)
            fixture.primitiveClass.declaringType=&fixture.outer;
        if(mode==Mode::PrimitiveClassGenericContainer)
            fixture.primitiveClass.genericContainerHandle=&fixture.def;
    }
    if(mode==Mode::StagedGenericContainer)fixture.def.genericContainerIndex=-2;
    if(mode==Mode::TwoClassesReversedPhysicalCallback) {
        fixture.image.typeCount=2;
        fixture.image.handles[1]=&fixture.secondary;
        g_otherPhysical=&fixture.other;
        g_reversePhysicalCallbacks=true;
    }
    if(mode==Mode::BadImageOwner)fixture.image.handles[0]=nullptr;
    if(mode==Mode::RegisteredAotInvalidOwner)
        fixture.image.assembly=nullptr;
    if(mode==Mode::RegisteredEmptyAotAssembly) {
        fixture.supportImage.typeCount=0;
        g_rawAssemblies[g_rawCount++]=&fixture.supportAssembly;
    }
    if(mode==Mode::RegisteredAotOverflow) {
        for(size_t i=0;i<1025;++i) {
            g_extraAssemblies[i].aname.name="Extra";
            g_extraAssemblies[i].token=1;
            g_extraAssemblies[i].image=&g_extraImages[i];
            g_extraImages[i].assembly=&g_extraAssemblies[i];
            g_extraImages[i].typeCount=1;
            g_rawAssemblies[g_rawCount++]=&g_extraAssemblies[i];
        }
    }
    if(mode==Mode::ConstructedGeneric)
        fixture.klass.generic_class=&fixture.genericClass;
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
    if(mode==Mode::MissingSecondPass)g_dropSecondPass=true;
    if(mode==Mode::DuplicateEnumeration)g_duplicate=true;
    if(mode==Mode::UnicodeAssembly)
        fixture.assembly.aname.name="M\xC3\x89THODS";
    if(mode==Mode::UnicodePathVersionAssembly)
        fixture.assembly.aname.name="C:\\Pkg\\M\xC3\x89THODS.DLL, Version=2";
    if(mode==Mode::UnicodeSupplementaryAssembly)
        fixture.assembly.aname.name="X\xF0\x9F\x98\x80";
    if(mode==Mode::InvalidUnicodeOverlong || mode==Mode::BadAssembly)
        fixture.assembly.aname.name="B\xC0\xAF";
    if(mode==Mode::InvalidUnicodeSurrogate)
        fixture.assembly.aname.name="B\xED\xA0\x80";
    Register(&fixture.klass);
    if(mode==Mode::TwoClassesReversedPhysicalCallback ||
       mode==Mode::CrossImageGeneric ||
       mode==Mode::CrossImageGenericAmbiguous ||
       mode==Mode::CrossImageInterpreter ||
       mode==Mode::CrossImageNestedArgument)
        Register(&fixture.other);
    const bool twoRows=
        mode==Mode::TwoClassesReversedPhysicalCallback ||
        mode==Mode::CrossImageGeneric ||
        mode==Mode::CrossImageGenericAmbiguous ||
        mode==Mode::RegisteredAotChanged ||
        mode==Mode::CrossImageInterpreter ||
        mode==Mode::CrossImageNestedArgument;
    const size_t expectedRows=twoRows?2u:1u;

    assembly_shadow_physical_membership::Scratch membership{};
    auto* owned=new rows::Rows();
    auto* record=new assembly_shadow_frozen_execution_record::Record();
    src::Receipt receipt{};
    const bool success=AssemblyShadow::TryCaptureBoundedDefinitionExecutionLive(
        membership,*owned,*record,receipt);
    if(success!=(expectedError==src::Error::None) || receipt.error!=expectedError)
        std::fprintf(stderr,"R03 B2 mode=%d actualSuccess=%d actualError=%u expectedError=%u\\n",
            static_cast<int>(mode),static_cast<int>(success),
            static_cast<unsigned>(receipt.error),static_cast<unsigned>(expectedError));
    if(expectedError==src::Error::None) {
        Check(success && receipt.error==src::Error::None);
        Check(receipt.registered==expectedRows &&
              receipt.physicallyMatched==expectedRows);
        Check(receipt.serializedRows==expectedRows && receipt.ownedBytes>0);
        Check(receipt.callbackPhysicalMembershipProven);
        Check(receipt.restrictedTypeDefSubsetProven);
        Check(receipt.observedRegistryAndCounterFenceStable);
        Check(receipt.admittedMetadataEpoch==1);
        Check(receipt.registeredImageSetStable);
        Check(receipt.registeredPhysicalAotImages>=receipt.admittedPhysicalImages);
        const bool admittedPrimitive=
            mode==Mode::PrimitiveDefault||mode==Mode::PrimitiveQualified;
        Check(receipt.usedExistingPrimitiveDefault==admittedPrimitive);
        Check(receipt.admittedPrimitiveArguments==(admittedPrimitive?1u:0u));
        if(admittedPrimitive) {
            Check(receipt.registeredPhysicalAotImages==2);
            Check(receipt.admittedPhysicalImages==1);
        }
        const bool unicode=(mode==Mode::UnicodeAssembly ||
            mode==Mode::UnicodePathVersionAssembly ||
            mode==Mode::UnicodeSupplementaryAssembly);
        Check(receipt.usedUnicodeAssemblyFold==unicode);
        const bool nestedGeneric=(mode==Mode::GenericConstructedArgument);
        const bool pointerArgument=(mode==Mode::GenericPointerArgument ||
                                    mode==Mode::GenericQualifiedPointerArgument);
        const bool szArrayArgument=(mode==Mode::GenericSzArrayArgument);
        const bool mdArrayArgument=(mode==Mode::GenericArrayArgument);
        const bool qualifiedArgument=(mode==Mode::PrimitiveQualified ||
                                      mode==Mode::GenericQualifiedArgument ||
                                      mode==Mode::GenericQualifiedPointerArgument);
        Check(receipt.usedNestedGenericArgument==nestedGeneric);
        Check(receipt.usedPointerArgument==pointerArgument);
        Check(receipt.usedSzArrayArgument==szArrayArgument);
        Check(receipt.usedMdArrayArgument==mdArrayArgument);
        Check(receipt.usedQualifiedArgument==qualifiedArgument);
        if(nestedGeneric||pointerArgument||szArrayArgument||mdArrayArgument)
            Check(receipt.maximumCompositeDepth>0);
        if(receipt.compositeNodesVisited)
            Check(receipt.compositeNodesVisited>=2);

        const bool crossImage=(mode==Mode::CrossImageGeneric ||
                               mode==Mode::CrossImageNestedArgument ||
                               mode==Mode::CrossImageGenericUnobserved ||
                               admittedPrimitive);
        Check(receipt.usedCrossImageArgument==crossImage);
        Check(receipt.usedUnobservedAotArgumentImage==
              (mode==Mode::CrossImageGenericUnobserved||admittedPrimitive));
        Check(receipt.admittedPhysicalImages==(twoRows?2u:1u) ||
              mode==Mode::TwoClassesReversedPhysicalCallback);
        Check(owned->Complete() && owned->Count()==expectedRows &&
              record->Complete());
        Check(!receipt.completeArbitraryTypeKeys);
        Check(!receipt.completeExecutionDiagnostics);
        Check(!receipt.coherentVmEpochProven && !receipt.runtimeAcceptance);
        const std::string body(record->Data(),record->Bytes());
        const char* key=(mode==Mode::PrimitiveDefault ||
                         mode==Mode::PrimitiveQualified ||
                         mode==Mode::GenericDefinition ||
                         mode==Mode::GenericSameImage ||
                         mode==Mode::GenericNestedArgument ||
                         mode==Mode::GenericQualifiedArgument ||
                         mode==Mode::GenericConstructedArgument ||
                         mode==Mode::GenericPointerArgument ||
                         mode==Mode::GenericSzArrayArgument ||
                         mode==Mode::GenericArrayArgument ||
                         mode==Mode::GenericQualifiedPointerArgument ||
                         mode==Mode::CrossImageGeneric ||
                         mode==Mode::CrossImageNestedArgument ||
                         mode==Mode::CrossImageGenericUnobserved)
            ? "type(7:methods/4:Demo/4:Hero@2)"
            : mode==Mode::UnicodeAssembly ||
              mode==Mode::UnicodePathVersionAssembly
                ? "type(8:m\xC3\xA9thods/4:Demo/4:Hero@0)"
                : mode==Mode::UnicodeSupplementaryAssembly
                    ? "type(5:x\xF0\x9F\x98\x80/4:Demo/4:Hero@0)"
            : mode==Mode::NestedType
                ? "type(7:methods/4:Demo/5:Outer@0/4:Demo/4:Hero@0)"
                : "type(7:methods/4:Demo/4:Hero@0)";
        Check(body.find(key)!=std::string::npos);
        if(mode==Mode::CrossImageGeneric ||
           mode==Mode::CrossImageNestedArgument ||
           mode==Mode::CrossImageGenericUnobserved) {
            const std::string arg=mode==Mode::CrossImageNestedArgument ?
                "type(7:support/4:Demo/5:Outer@0/4:Demo/6:Friend@0)" :
                "type(7:support/4:Demo/6:Friend@0)";
            const std::string canonical="generic(type(7:methods/4:Demo/4:Hero@2),"+
                std::to_string(arg.size())+":"+arg+","+
                std::to_string(arg.size())+":"+arg+")";
            Check(body.find(canonical)!=std::string::npos);
            if(mode!=Mode::CrossImageGenericUnobserved)
                Check(body.find("\"logicalAssembly\":\"Support\"")!=std::string::npos);
            Check(receipt.admittedPhysicalImages==
                  (mode==Mode::CrossImageGenericUnobserved?1u:2u));
            Check(receipt.registeredPhysicalAotImages==2);
        }
        if(mode==Mode::GenericSameImage || mode==Mode::GenericNestedArgument) {
            const std::string arg=mode==Mode::GenericNestedArgument ?
                "type(7:methods/4:Demo/5:Outer@0/4:Demo/6:Friend@0)" :
                "type(7:methods/4:Demo/6:Friend@0)";
            const std::string generic="generic("+
                std::string("type(7:methods/4:Demo/4:Hero@2),")+
                std::to_string(arg.size())+":"+arg+","+
                std::to_string(arg.size())+":"+arg+")";
            Check(body.find(generic)!=std::string::npos);
            Check(body.find("\"typeKey\":\"generic(")!=std::string::npos);
        }
        if(admittedPrimitive) {
            const std::string primitive="type(7:support/6:System/5:Int32@0)";
            Check(body.find(primitive)!=std::string::npos);
            if(mode==Mode::PrimitiveQualified)
                Check(body.find("qualified(0,0,1,0,"+primitive+")")!=
                      std::string::npos);
        }
        if(mode==Mode::GenericQualifiedArgument)
            Check(body.find("qualified(0,0,1,0,type(")!=std::string::npos);
        if(mode==Mode::GenericPointerArgument)
            Check(body.find("ptr(type(")!=std::string::npos);
        if(mode==Mode::GenericSzArrayArgument)
            Check(body.find("szarray(type(")!=std::string::npos);
        if(mode==Mode::GenericArrayArgument)
            Check(body.find("array(2,")!=std::string::npos &&
                  body.find("sizes=4,5,bounds=-1,0,")!=std::string::npos);
        if(mode==Mode::GenericQualifiedPointerArgument)
            Check(body.find("qualified(7,2,1,0,ptr(type(")!=std::string::npos);
        if(mode==Mode::GenericConstructedArgument)
            Check(body.find("generic(type(7:methods/4:Demo/6:Friend@2),")!=
                  std::string::npos);
        if(!unicode)
        Check(body.find("\"logicalAssembly\":\"Methods\"")!=std::string::npos);
        else {
            rows::RowView unicodeRow{};
            Check(owned->Get(0,unicodeRow));
            Check(unicodeRow.logicalAssembly.size==
                  std::strlen(fixture.assembly.aname.name));
        }
        if(mode==Mode::TwoClassesReversedPhysicalCallback) {
            Check(body.find("type(7:methods/4:Demo/6:Friend@0)")!=
                  std::string::npos);
            rows::RowView row0{},row1{};
            Check(owned->Get(0,row0) && owned->Get(1,row1));
            Check(row0.registrySlot < row1.registrySlot);
        }
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
        Check(!receipt.usedUnicodeAssemblyFold);
        Check(!receipt.usedExistingPrimitiveDefault);
        Check(receipt.admittedPrimitiveArguments==0);
        Check(!receipt.usedNestedGenericArgument &&
              !receipt.usedPointerArgument && !receipt.usedSzArrayArgument &&
              !receipt.usedMdArrayArgument && !receipt.usedQualifiedArgument);

        Check(!receipt.completeExecutionDiagnostics && !receipt.runtimeAcceptance);
    }
    delete record;
    delete owned;
    s_active.store(nullptr);
    il2cpp_defaults=Il2CppDefaults{};
}
static void TestPhysicalImageInventoryBounds() {
    namespace physical_images=il2cpp::vm::assembly_shadow_observed_images;
    char imageSymbols[65]{};
    physical_images::Inventory<64> inventory;
    Check(inventory.Healthy());
    Check(!inventory.AddFromMatchedCallback(nullptr));
    Check(inventory.FirstError()==physical_images::Error::MissingImage);
    Check(!inventory.Healthy() && inventory.Count()==0);
    physical_images::Inventory<64> bounded;
    for(size_t i=0;i<64;++i)
        Check(bounded.AddFromMatchedCallback(&imageSymbols[i]));
    Check(bounded.Count()==64);
    Check(bounded.AddFromMatchedCallback(&imageSymbols[0]));
    Check(bounded.Count()==64);
    Check(!bounded.AddFromMatchedCallback(&imageSymbols[64]));
    Check(bounded.FirstError()==physical_images::Error::Capacity);
    Check(!bounded.Healthy() && bounded.Count()==0);
    Check(bounded.Get<Il2CppImage>(0)==nullptr);
}

// The original Class.cpp uses ASSERT for absent il2cpp_defaults pointers.
// Our actual production Peek must return null instead of aborting.
static unsigned primitivePeekCases=0;
static void TestAssertFreePrimitivePeekMapping(){
    namespace defaults=il2cpp::vm::assembly_shadow_existing_primitive_defaults;
    struct TestCase {Il2CppTypeEnum code;Il2CppClass* Il2CppDefaults::*slot;};
    const TestCase table[]={
        {IL2CPP_TYPE_OBJECT,&Il2CppDefaults::object_class},
        {IL2CPP_TYPE_VOID,&Il2CppDefaults::void_class},
        {IL2CPP_TYPE_BOOLEAN,&Il2CppDefaults::boolean_class},
        {IL2CPP_TYPE_CHAR,&Il2CppDefaults::char_class},
        {IL2CPP_TYPE_I1,&Il2CppDefaults::sbyte_class},
        {IL2CPP_TYPE_U1,&Il2CppDefaults::byte_class},
        {IL2CPP_TYPE_I2,&Il2CppDefaults::int16_class},
        {IL2CPP_TYPE_U2,&Il2CppDefaults::uint16_class},
        {IL2CPP_TYPE_I4,&Il2CppDefaults::int32_class},
        {IL2CPP_TYPE_U4,&Il2CppDefaults::uint32_class},
        {IL2CPP_TYPE_I,&Il2CppDefaults::int_class},
        {IL2CPP_TYPE_U,&Il2CppDefaults::uint_class},
        {IL2CPP_TYPE_I8,&Il2CppDefaults::int64_class},
        {IL2CPP_TYPE_U8,&Il2CppDefaults::uint64_class},
        {IL2CPP_TYPE_R4,&Il2CppDefaults::single_class},
        {IL2CPP_TYPE_R8,&Il2CppDefaults::double_class},
        {IL2CPP_TYPE_STRING,&Il2CppDefaults::string_class},
        {IL2CPP_TYPE_TYPEDBYREF,&Il2CppDefaults::typed_reference_class},
        {IL2CPP_TYPE_IL2CPP_TYPE_INDEX,&Il2CppDefaults::systemtype_class},
    };
    Il2CppClass sentinel{};
    for(const TestCase& test:table){
        il2cpp_defaults=Il2CppDefaults{};
        Check(defaults::Peek(test.code)==nullptr);
        il2cpp_defaults.*(test.slot)=&sentinel;
        Check(defaults::Peek(test.code)==&sentinel);
        ++primitivePeekCases;
    }
    il2cpp_defaults=Il2CppDefaults{};
    Check(defaults::Peek(static_cast<Il2CppTypeEnum>(42))==nullptr);
    Check(defaults::Peek(static_cast<Il2CppTypeEnum>(IL2CPP_TYPE_GENERICINST))==nullptr);
    Check(defaults::Peek(static_cast<Il2CppTypeEnum>(IL2CPP_TYPE_CLASS))==nullptr);
    ++primitivePeekCases;
}
int main(){
    TestPhysicalImageInventoryBounds();
    TestAssertFreePrimitivePeekMapping();
    g_container.type_argc=2;
    Check(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit());
    Exercise(Mode::Good,src::Error::None);
    Exercise(Mode::UnicodeAssembly,src::Error::None);
    Exercise(Mode::UnicodePathVersionAssembly,src::Error::None);
    Exercise(Mode::UnicodeSupplementaryAssembly,src::Error::None);
    Exercise(Mode::InvalidUnicodeOverlong,src::Error::TypeKeyFailed);
    Exercise(Mode::InvalidUnicodeSurrogate,src::Error::TypeKeyFailed);
    Exercise(Mode::GenericDefinition,src::Error::None);
    Exercise(Mode::PrimitiveDefault,src::Error::None);
    Exercise(Mode::PrimitiveQualified,src::Error::None);
    Exercise(Mode::PrimitiveMissingDefault,src::Error::UnsupportedTypeShape);
    Exercise(Mode::PrimitiveUnregistered,src::Error::UnadmittedMetadataOwner);
    Exercise(Mode::PrimitiveWrongHandle,src::Error::UnadmittedMetadataOwner);
    Exercise(Mode::PrimitiveWrongImage,src::Error::InvalidTypeIdentity);
    Exercise(Mode::PrimitiveUnsupportedTag,src::Error::UnsupportedTypeShape);
    Exercise(Mode::PrimitiveAmbiguousOwner,src::Error::AmbiguousMetadataOwner);
    Exercise(Mode::PrimitiveGenericArity,src::Error::UnsupportedTypeShape);
    Exercise(Mode::PrimitiveClassNameMismatch,src::Error::UnsupportedTypeShape);
    Exercise(Mode::PrimitiveClassNested,src::Error::UnsupportedTypeShape);
    Exercise(Mode::PrimitiveClassGenericContainer,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericSameImage,src::Error::None);
    Exercise(Mode::GenericNestedArgument,src::Error::None);
    Exercise(Mode::CrossImageGeneric,src::Error::None);
    Exercise(Mode::CrossImageGenericUnobserved,src::Error::None);
    Exercise(Mode::CrossImageGenericNotRegistered,src::Error::UnadmittedMetadataOwner);
    Exercise(Mode::RegisteredAotChanged,src::Error::SourceChanged);
    Exercise(Mode::RegisteredAotOverflow,src::Error::PhysicalImageLimit);
    Exercise(Mode::RegisteredAotInvalidOwner,src::Error::UnadmittedMetadataOwner);
    Exercise(Mode::RegisteredEmptyAotAssembly,src::Error::None);
    Exercise(Mode::CrossImageGenericAmbiguous,src::Error::AmbiguousMetadataOwner);
    Exercise(Mode::CrossImageInterpreter,src::Error::UnsupportedImage);
    Exercise(Mode::CrossImageNestedArgument,src::Error::None);
    Exercise(Mode::GenericArgumentMissingOwner,src::Error::UnadmittedMetadataOwner);
    Exercise(Mode::GenericWrongArity,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericTooManyArguments,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericQualifiedArgument,src::Error::None);
    Exercise(Mode::GenericConstructedArgument,src::Error::None);
    Exercise(Mode::GenericPointerArgument,src::Error::None);
    Exercise(Mode::GenericSzArrayArgument,src::Error::None);
    Exercise(Mode::GenericArrayArgument,src::Error::None);
    Exercise(Mode::GenericQualifiedPointerArgument,src::Error::None);
    Exercise(Mode::GenericArrayBadSizes,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericArrayMissingElement,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericArrayTooManyDimensions,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericCompositeTooDeep,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericMissingArguments,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericMissingContext,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericBadMethodContext,src::Error::UnsupportedTypeShape);
    Exercise(Mode::GenericWrongBase,src::Error::UnsupportedTypeShape);
    Exercise(Mode::StagedGenericContainer,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::TwoClassesReversedPhysicalCallback,src::Error::None);
    Exercise(Mode::BadImageOwner,src::Error::UnadmittedMetadataOwner);
    Exercise(Mode::ConstructedGeneric,src::Error::UnsupportedTypeShape);
    Exercise(Mode::NestedType,src::Error::None);
    Exercise(Mode::WrongNestedOwner,src::Error::UnadmittedMetadataOwner);
    Exercise(Mode::CyclicNested,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::InvalidDeclaringShape,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::StagedNestedIndex,src::Error::UnsupportedMetadataAncestry);
    Exercise(Mode::UnsupportedImage,src::Error::UnsupportedImage);
    Exercise(Mode::SourceChanged,src::Error::SourceChanged);
    Exercise(Mode::CounterChanged,src::Error::CounterChanged);
    Exercise(Mode::MissingEnumeration,src::Error::UnmatchedClass);
    Exercise(Mode::MissingSecondPass,src::Error::UnmatchedClass);
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
#if IL2CPP_DEBUG
    const char* flavor="debug";
#else
    const char* flavor="release";
#endif
    std::printf("{\"kind\":\"R03B2ActualNativeSourceMock\","
        "\"result\":\"PassedActualInlWithMockedVmOnly\","
        "\"buildFlavor\":\"%s\",\"cases\":%d,\"assertions\":%d,\"primitivePeekCases\":%u,\"realUnity\":false,"
        "\"physicalClassProvenInRealPlayer\":false,"
        "\"fullCanonicalTypeKeys\":false,\"nativeV2TransportImplemented\":false,"
        "\"runtimeAcceptance\":false,\"R03Accepted\":false}\n",flavor,cases,assertions,primitivePeekCases);
}
