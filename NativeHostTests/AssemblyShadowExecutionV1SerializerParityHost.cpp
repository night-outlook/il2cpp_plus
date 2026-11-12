// R03 B2, source-accurate field15 serialization differential.
// Uses ACTUAL AssemblyShadowExecutionSerializer.h,
// AssemblyShadowR02Diagnostics.h and AssemblyShadowFrozenExecutionRecord.h.
// This proves exact legacy std::ostream-vs-owned-record JSON only when the
// immutable input class rows + complete R02 counter values are identical.
// It does NOT capture live Unity Il2CppClass metadata, terminal source,
// all-VM epoch or native V2. Entire host remains outside libil2cpp runtime.
#include "vm/AssemblyShadowR02Diagnostics.h"
#include "vm/AssemblyShadowFrozenExecutionRecord.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace ex = il2cpp::vm::assembly_shadow_execution_diagnostic;
namespace frozen = il2cpp::vm::assembly_shadow_frozen_execution_record;
namespace r02 = il2cpp::vm::assembly_shadow_r02;
namespace diagnostic = il2cpp::vm::assembly_shadow_diagnostic;
static unsigned cases=0,checks=0,guardedAllocations=0,matchedRows=0;
static bool allocationGuard=false;

void* operator new(std::size_t n) {
    if(allocationGuard) {++guardedAllocations;std::abort();}
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}

static void Check(bool truth,int line) {
    ++checks;
    if(!truth) {
        std::fprintf(stderr,
                     "R03 B2 ORIGINAL V1 serialization parity case=%u check=%u line=%d\n",
                     cases,checks,line);
        std::abort();
    }
}
#define CHECK(x) Check(!!(x),__LINE__)

static ex::Header Header(const r02::Snapshot& snap,int stateCode,
                         const char* state,uint64_t generation) {
    ex::Header h;
    h.stateCode=stateCode;
    h.state=state;
    h.generation=generation;
    h.methodChecks=snap.Read(r02::Metric::MethodChecks);
    h.shadowMethodChecks=snap.Read(r02::Metric::ShadowMethodChecks);
    h.rejectedBaselineMethods=snap.Read(r02::Metric::RejectedBaselineMethods);
    h.baselineClassCctorStarted=snap.Read(r02::Metric::BaselineCctors);
    h.shadowClassCctorStarted=snap.Read(r02::Metric::ShadowCctors);
    h.interpreterTransformations=snap.Read(r02::Metric::Transformations);
    h.shadowInterpreterTransformations=snap.Read(r02::Metric::ShadowTransformations);
    h.droppedClassObservations=snap.Read(r02::Metric::DroppedClasses);
    return h;
}
static ex::ClassRow Row(const char* assembly,const char* key,
                        bool shadow,bool physical,bool active,
                        bool started,bool finished,bool exception,
                        const char* pointer,bool pointerDetails,
                        bool storageAvailable) {
    ex::ClassRow row;
    row.logicalAssembly=diagnostic::Quoted(assembly);
    row.typeKey=diagnostic::Quoted(key);
    row.interpreterShadow=shadow;
    row.physicalInterpreter=physical;
    row.isActive=active;
    row.cctorStarted=started;
    row.cctorFinished=finished;
    row.hasInitializationException=exception;
    row.staticStoragePointer=diagnostic::Quoted(pointer);
    row.pointerDetailsAvailable=pointerDetails;
    row.staticStorageAvailable=storageAvailable;
    return row;
}
static std::string Legacy(const ex::Header& header,
                          const ex::ClassRow* rows,size_t count) {
    // Same ACTUAL legacy source formatting functions and final R02 suffix
    // as original GetExecutionDiagnosticsJson; no cloned JSON generator.
    std::ostringstream output;
    ex::Begin(output,header);
    for(size_t i=0;i<count;++i)
        ex::AppendClass(output,rows[i],i!=0);
    ex::EndClasses(output);
    r02::AppendDiagnostics(output);
    output << "}";
    return output.str();
}
static void CompareCase(const ex::ClassRow* rows,size_t count,
                        int stateCode,const char* state,uint64_t generation) {
    ++cases;
    r02::Snapshot snapshot;
    r02::SnapshotReceipt receipt;
    CHECK(r02::Snapshot::TryCaptureCurrent(snapshot,receipt));
    CHECK(snapshot.Complete()&&receipt.observedReadFenceStable);
    CHECK(receipt.samples==2);
    CHECK(snapshot.DiagnosticsLevel()==2);
    const ex::Header h=Header(snapshot,stateCode,state,generation);
    const std::string legacy=Legacy(h,rows,count);
    CHECK(!legacy.empty());
    auto* record=new frozen::Record();
    frozen::Identity identity;
    identity.stateCode=stateCode;
    identity.state=diagnostic::Quoted(state);
    identity.generation=generation;
    // Warm up locale and stdlib before guarding native frozen formatter.
    CHECK(std::locale()==std::locale::classic());
    allocationGuard=true;
    const bool started=record->Begin(identity,snapshot,count);
    bool appended=started;
    for(size_t i=0;started && i<count;++i)
        if(!record->AddClass(rows[i])) {appended=false;break;}
    const bool finished=started&&appended&&record->Freeze();
    allocationGuard=false;
    CHECK(started && appended && finished && record->Complete());
    CHECK(record->Bytes()==legacy.size());
    CHECK(std::memcmp(record->Data(),legacy.data(),legacy.size())==0);
    CHECK(record->RowsWritten()==count);
    CHECK(record->FirstError()==frozen::RecordError::None);
    CHECK(legacy.find("\"r02\":{\"schemaVersion\":1")!=std::string::npos);
    CHECK(legacy.find("\"schemaVersion\":1,\"enabled\":true")!=std::string::npos);
    CHECK(legacy.find("\"counterCoverage\":\"BoundedComplete\"")!=std::string::npos);
    matchedRows+=static_cast<unsigned>(count);
    delete record;
}
static void Positive() {
    CompareCase(nullptr,0,6,"Committed",3); // zero classes
    const ex::ClassRow one[]={
        Row("Methods","type(7:methods/4:Demo/4:Hero@0)",
            false,false,true,true,false,false,"",false,false)
    };
    r02::ObservationCounters::Add(r02::Metric::MethodChecks,23);
    r02::ObservationCounters::Add(r02::Metric::ShadowMethodChecks,7);
    r02::ObservationCounters::Add(r02::Metric::RejectedBaselineMethods,2);
    r02::ObservationCounters::Add(r02::Metric::BaselineCctors,1);
    CompareCase(one,1,6,"Committed",42);
    const ex::ClassRow two[]={
        Row("C:\\Pkg\\M\xC3\x89THODS.DLL, Version=2",
            "generic(type(7:methods/4:Demo/4:Hero@2),"
            "36:type(7:support/6:System/5:Int32@0),"
            "36:type(7:support/6:System/5:Int32@0))",
            true,false,false,true,true,false,
            "0xabcdef",true,true),
        Row("HOT\"Assembly\\Line\n", "ptr(type(3:hot/4:Demo/3:Obj@0))",
            true,true,true,false,false,true,"",true,false),
    };
    r02::ObservationCounters::Add(r02::Metric::Transformations,100);
    r02::ObservationCounters::Add(r02::Metric::ShadowTransformations,60);
    r02::ObservationCounters::Add(r02::Metric::DefinitionSearches,12345);
    r02::ObservationCounters::Add(r02::Metric::ObservationMemoHits,17);
    CompareCase(two,2,8,"Failed",UINT64_MAX-10);
    const ex::ClassRow edge[]={
        Row("Assembly-CSharp",
            "array(2,34:type(3:aot/4:Game/4:Node@0),sizes=1,2,bounds=-1,0,)",
            true,false,true,true,true,true,"0x0",false,true),
        Row("Assembly-CSharp","qualified(7,2,1,0,szarray(type(3:aot/4:Game/4:Node@0)))",
            false,false,false,false,true,false,"",false,false),
        Row("O'Brien/Module","type(2:ob/0:/1:X@0)",
            true,true,true,false,false,false,"",false,false),
    };
    r02::ObservationCounters::Add(r02::Metric::CounterpartHits,4294967297ULL);
    r02::ObservationCounters::Add(r02::Metric::CounterpartMisses,789);
    CompareCase(edge,3,9,"FailedAfterCommit",18446744073709551610ULL);
}
static void Negative() {
    r02::Snapshot snapshot; r02::SnapshotReceipt receipt;
    CHECK(r02::Snapshot::TryCaptureCurrent(snapshot,receipt));
    CHECK(snapshot.Complete());
    frozen::Identity identity;
    identity.stateCode=6;identity.state=diagnostic::Quoted("Committed");
    identity.generation=4;
    ++cases;
    auto* tooFew=new frozen::Record();
    CHECK(tooFew->Begin(identity,snapshot,1));
    CHECK(!tooFew->Freeze()); // Exact row count, no fabricated classes.
    CHECK(!tooFew->Complete()&&tooFew->Data()==nullptr);
    delete tooFew;
    ++cases;
    auto* tooMany=new frozen::Record();
    CHECK(tooMany->Begin(identity,snapshot,0));
    const ex::ClassRow row=Row("a","type(1:a/0:/1:X@0)",false,false,true,
                                false,false,false,"",false,false);
    CHECK(!tooMany->AddClass(row));
    CHECK(!tooMany->Complete()&&tooMany->Data()==nullptr);
    delete tooMany;
    ++cases;
    auto* invalid=new frozen::Record();
    CHECK(invalid->Begin(identity,snapshot,1));
    ex::ClassRow bad=row;
    const char invalidUtf8[]={'B',char(0xc0),char(0xaf),0};
    bad.logicalAssembly={invalidUtf8,3};
    CHECK(!invalid->AddClass(bad));
    CHECK(!invalid->Complete()&&invalid->Data()==nullptr);
    CHECK(invalid->FirstError()==frozen::RecordError::InvalidUtf8);
    delete invalid;
    ++cases;
    auto* aliased=new frozen::Record();
    CHECK(aliased->Begin(identity,snapshot,1));
    ex::ClassRow alias=row;
    alias.typeKey={reinterpret_cast<const char*>(aliased),3};
    CHECK(!aliased->AddClass(alias));
    CHECK(!aliased->Complete()&&aliased->Data()==nullptr);
    CHECK(aliased->FirstError()==frozen::RecordError::AliasedInput);
    delete aliased;
}
int main() {
    static_assert(!std::is_copy_constructible<frozen::Record>::value,
                  "Frozen diagnostics must retain a unique owner");
    CHECK(std::locale()==std::locale::classic());
    Positive();Negative();
    CHECK(guardedAllocations==0);
    CHECK(cases==8 && matchedRows==6);
    std::printf("{\"kind\":\"R03B2OriginalV1ExecutionSerializerParityHost\","
                "\"result\":\"PassedActualLegacyAndFrozenSerializerBytes\","
                "\"cases\":%u,\"assertions\":%u,\"rows\":%u,"
                "\"guardedAllocations\":%u,"
                "\"actualLegacyR02Formatter\":true,"
                "\"actualFrozenR02Formatter\":true,"
                "\"exactByteEqualityWithImmutableRows\":true,"
                "\"actualUnityClassMetadata\":false,"
                "\"wholeVmEpochProven\":false,"
                "\"completeField15InPlayer\":false,"
                "\"terminalSafeField15\":false,"
                "\"nativeV2TransportImplemented\":false,"
                "\"runtimeAcceptance\":false}\n",
                cases,checks,matchedRows,guardedAllocations);
}
