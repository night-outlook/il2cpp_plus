// Actual production snapshot/capture/formatters, plus byte-exact original
// formatter headers retained at native 5667e371. VM class acquisition is NOT
// simulated as acceptance: class rows below are explicitly owned host inputs.
#include "AssemblyShadowFrozenExecutionRecord.h"
#include "AssemblyShadowR02Diagnostics.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace diag=il2cpp::vm::assembly_shadow_diagnostic;
namespace r02=il2cpp::vm::assembly_shadow_r02;
namespace exec=il2cpp::vm::assembly_shadow_execution_diagnostic;
namespace frozen=il2cpp::vm::assembly_shadow_frozen_execution_record;
extern bool watchNew;
extern size_t watchedNew;
#define REQUIRE(condition) do { if(!(condition))throw std::runtime_error(#condition); } while(false)

// Only the namespace is renamed; the original header bytes remain unchanged.
#define assembly_shadow_execution_diagnostic execution_v1_reference
#include FROZEN_EXECUTION_REFERENCE_HEADER
#undef assembly_shadow_execution_diagnostic
namespace original=il2cpp::vm::execution_v1_reference;
namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {
struct ReferenceCounters
{
    inline static const Snapshot* sample=nullptr;
    inline static int Level=ObservationCounters::Level;
    static constexpr size_t kThreadCapacity=ObservationCounters::kThreadCapacity;
    static uint64_t Read(Metric metric){return sample ? sample->Read(metric) : ObservationCounters::Read(metric);}
    static uint64_t DroppedThreads(){return sample ? sample->DroppedThreads() : ObservationCounters::DroppedThreads();}
    static bool Saturated(){return sample ? sample->Saturated() : ObservationCounters::Saturated();}
    static size_t RetainedBytes(){return sample ? sample->RetainedBytes() : ObservationCounters::RetainedBytes();}
    static const char* Coverage(){return sample ? sample->Coverage() : ObservationCounters::Coverage();}
};
struct ReferenceMemo
{
    static size_t TlsBytesPerThread()
    { return ReferenceCounters::sample ? ReferenceCounters::sample->MemoTlsBytesPerThread() : ObservationMemo::TlsBytesPerThread(); }
};
}}}
// The original R02 body is unedited. These three symbol substitutions route
// it to an immutable test value (or actual counters), not the new formatter.
#define ObservationCounters ReferenceCounters
#define ObservationMemo ReferenceMemo
#define AppendDiagnostics AppendV1ReferenceDiagnostics
#include FROZEN_R02_REFERENCE_HEADER
#undef AppendDiagnostics
#undef ObservationMemo
#undef ObservationCounters

struct FakeSource
{
    int level=2;
    size_t capacity=128,storage=40960,memo=512;
    uint64_t dropped=0;
    bool saturated=false;
    std::array<uint64_t,r02::Snapshot::kMetricCount> values{};
    int changedMetric=-1,changeMetadata=0;
    mutable size_t pass=0,current=0,reads=0;
    mutable std::array<r02::Metric,2*r02::Snapshot::kMetricCount> order{};
    mutable std::array<std::memory_order,2*r02::Snapshot::kMetricCount> memoryOrder{};
    explicit FakeSource(int requestedLevel=2):level(requestedLevel)
    {
        for(size_t i=0;i<values.size();++i)values[i]=i+1;
        values[static_cast<size_t>(r02::Metric::MethodChecks)]=10000;
        values[static_cast<size_t>(r02::Metric::Transformations)]=10000;
        if(level<2)memo=0;
        if(level==0){values.fill(0);storage=0;}
    }
    int DiagnosticsLevel() const noexcept { current=pass++;return level+(current && changeMetadata==1 ? 1 : 0); }
    size_t ThreadCapacity() const noexcept { return capacity-(current && changeMetadata==2 ? 1 : 0); }
    size_t MemoTlsBytesPerThread() const noexcept { return memo+(current && changeMetadata==3 ? 1 : 0); }
    size_t RetainedBytes() const noexcept { return storage+(current && changeMetadata==4 ? 1 : 0); }
    uint64_t DroppedThreads() const noexcept { return dropped+(current && changeMetadata==5 ? 1 : 0); }
    bool Saturated() const noexcept { return saturated || (current && changeMetadata==6); }
    uint64_t Read(r02::Metric metric,std::memory_order memory=std::memory_order_relaxed) const noexcept
    {
        if(reads<order.size()){order[reads]=metric;memoryOrder[reads]=memory;}
        ++reads;
        return values[static_cast<size_t>(metric)]+(current && changedMetric==static_cast<int>(metric) ? 1 : 0);
    }
};
static r02::Snapshot Take(const FakeSource& source=FakeSource{})
{
    r02::Snapshot result;r02::SnapshotReceipt receipt;
    REQUIRE(r02::Snapshot::TryCaptureFrom(source,result,receipt));
    REQUIRE(result.Complete() && receipt.observedReadFenceStable && receipt.samples==2);
    REQUIRE(receipt.metricReads==2*r02::Snapshot::kMetricCount);
    REQUIRE(!receipt.coherentVmEpochProven && !receipt.runtimeAcceptance);
    return result;
}
static frozen::Identity Identity(){return {6,diag::Quoted("Committed"),17};}
static exec::ClassRow Row()
{
    exec::ClassRow row;
    row.logicalAssembly=diag::Quoted("Module.Contracts");row.typeKey=diag::Quoted("Example.Item");
    row.staticStoragePointer=diag::Quoted("");row.isActive=true;
    return row;
}
static std::string Expected(const frozen::Identity& identity,const r02::Snapshot& snapshot,
                            const std::vector<exec::ClassRow>& rows)
{
    std::string state(identity.state.data,identity.state.bytes);
    original::Header header;
    header.stateCode=identity.stateCode;header.state=state.c_str();header.generation=identity.generation;
    header.methodChecks=snapshot.Read(r02::Metric::MethodChecks);
    header.shadowMethodChecks=snapshot.Read(r02::Metric::ShadowMethodChecks);
    header.rejectedBaselineMethods=snapshot.Read(r02::Metric::RejectedBaselineMethods);
    header.baselineClassCctorStarted=snapshot.Read(r02::Metric::BaselineCctors);
    header.shadowClassCctorStarted=snapshot.Read(r02::Metric::ShadowCctors);
    header.interpreterTransformations=snapshot.Read(r02::Metric::Transformations);
    header.shadowInterpreterTransformations=snapshot.Read(r02::Metric::ShadowTransformations);
    header.droppedClassObservations=snapshot.Read(r02::Metric::DroppedClasses);
    std::ostringstream out;original::Begin(out,header);
    for(size_t i=0;i<rows.size();++i)
    {
        const auto& source=rows[i];original::ClassRow row;
        row.logicalAssembly=source.logicalAssembly;row.typeKey=source.typeKey;
        row.interpreterShadow=source.interpreterShadow;row.physicalInterpreter=source.physicalInterpreter;
        row.isActive=source.isActive;row.cctorStarted=source.cctorStarted;row.cctorFinished=source.cctorFinished;
        row.hasInitializationException=source.hasInitializationException;row.staticStoragePointer=source.staticStoragePointer;
        row.pointerDetailsAvailable=source.pointerDetailsAvailable;row.staticStorageAvailable=source.staticStorageAvailable;
        original::AppendClass(out,row,i!=0);
    }
    original::EndClasses(out);
    r02::ReferenceCounters::sample=&snapshot;r02::ReferenceCounters::Level=snapshot.DiagnosticsLevel();
    r02::AppendV1ReferenceDiagnostics(out);r02::ReferenceCounters::sample=nullptr;
    out<<'}';return out.str();
}
static std::unique_ptr<frozen::Record> Build(const r02::Snapshot& snapshot,const std::vector<exec::ClassRow>& rows={},
                                             frozen::Identity identity=Identity())
{
    auto capture=std::make_unique<frozen::Record>();
    REQUIRE(capture->Begin(identity,snapshot,rows.size()));
    for(const auto& row:rows)REQUIRE(capture->AddClass(row));
    REQUIRE(capture->Freeze() && capture->Complete());
    REQUIRE(capture->Bytes()==Expected(identity,snapshot,rows).size());
    REQUIRE(std::memcmp(capture->Data(),Expected(identity,snapshot,rows).data(),capture->Bytes())==0);
    return capture;
}
struct Output
{
    std::string bytes;
    size_t calls=0,maximum=0,failAt=0;
    const frozen::Record* reenter=nullptr;
    frozen::Record* revoke=nullptr;
    diag::WriteResult nested{};
    static bool Write(void* context,const char* data,size_t count) noexcept
    {
        auto& self=*static_cast<Output*>(context);++self.calls;
        if(count>self.maximum)self.maximum=count;
        if(self.reenter)self.nested=self.reenter->EmitTo({&self,Write},frozen::Record::kMaxBytes);
        if(self.revoke)self.revoke->RejectInvalidSource();
        if(self.failAt && self.calls==self.failAt)return false;
        try{self.bytes.append(data,count);}catch(...){return false;}
        return true;
    }
};

static void SnapshotAllFields()
{
    FakeSource source;const auto snapshot=Take(source);
    for(size_t i=0;i<source.values.size();++i)REQUIRE(snapshot.Read(static_cast<r02::Metric>(i))==source.values[i]);
    REQUIRE(source.reads==2*source.values.size());
    for(size_t p=0;p<2;++p)
    {
        const size_t base=p*source.values.size();
        REQUIRE(source.order[base+source.values.size()-2]==r02::Metric::MethodChecks);
        REQUIRE(source.order[base+source.values.size()-1]==r02::Metric::Transformations);
        for(size_t i=base;i<base+source.values.size();++i)
        {
            const auto metric=source.order[i];
            const bool subset=metric==r02::Metric::ShadowMethodChecks || metric==r02::Metric::RejectedBaselineMethods || metric==r02::Metric::ShadowTransformations;
            REQUIRE(source.memoryOrder[i]==(subset ? std::memory_order_acquire : std::memory_order_relaxed));
        }
    }
}
static void EveryMetricChange()
{
    for(size_t index=0;index<r02::Snapshot::kMetricCount;++index)
    {
        FakeSource source;source.changedMetric=static_cast<int>(index);
        auto snapshot=Take();r02::SnapshotReceipt receipt;
        REQUIRE(!r02::Snapshot::TryCaptureFrom(source,snapshot,receipt));
        REQUIRE(!snapshot.Complete() && receipt.error==r02::SnapshotError::ObservedCounterChange);
        REQUIRE(!receipt.observedReadFenceStable && receipt.samples==2 && source.reads==2*r02::Snapshot::kMetricCount);
    }
}
static void EveryMetadataChange()
{
    for(int change=1;change<=6;++change)
    {
        FakeSource source;source.changeMetadata=change;r02::Snapshot snapshot;r02::SnapshotReceipt receipt;
        REQUIRE(!r02::Snapshot::TryCaptureFrom(source,snapshot,receipt));
        REQUIRE(receipt.error!=r02::SnapshotError::None && !snapshot.Complete());
    }
}
static void InvalidSubsets()
{
    for(auto metric:{r02::Metric::ShadowMethodChecks,r02::Metric::RejectedBaselineMethods,r02::Metric::ShadowTransformations})
    {
        FakeSource source;source.values[static_cast<size_t>(metric)]=10001;
        r02::Snapshot snapshot;r02::SnapshotReceipt receipt;
        REQUIRE(!r02::Snapshot::TryCaptureFrom(source,snapshot,receipt));
        REQUIRE(receipt.error==r02::SnapshotError::InvalidSample);
    }
}
static void DisabledPolicy()
{
    auto snapshot=Take(FakeSource{0});REQUIRE(std::strcmp(snapshot.Coverage(),"Disabled")==0);
    auto capture=Build(snapshot);REQUIRE(capture->RowsWritten()==0);
    FakeSource invalid{0};invalid.values[0]=1;r02::SnapshotReceipt receipt;
    REQUIRE(!r02::Snapshot::TryCaptureFrom(invalid,snapshot,receipt));
    auto object=std::make_unique<frozen::Record>();REQUIRE(!object->Begin(Identity(),Take(FakeSource{1}),1));
}
static void CoveragePrecedence()
{
    for(int level=0;level<=2;++level)
        for(int dropped=0;dropped<=1;++dropped)
            for(int saturated=0;saturated<=1;++saturated)
                for(int classes=0;classes<=1;++classes)
                {
                    if(level==0 && (dropped || saturated || classes))continue;
                    FakeSource source{level};source.dropped=dropped;source.saturated=saturated;
                    source.values[static_cast<size_t>(r02::Metric::DroppedClasses)]=classes;
                    const auto snapshot=Take(source);auto capture=Build(snapshot);
                    const std::string json(capture->Data(),capture->Bytes());
                    const char* coverage=level==0 ? "Disabled" : dropped ? "Truncated" : saturated ? "Saturated" : "BoundedComplete";
                    const char* classCoverage=level<2 ? "Disabled" : dropped || classes ? "Truncated" : saturated ? "Saturated" : "BoundedComplete";
                    REQUIRE(json.find(std::string("\"counterCoverage\":\"")+coverage+"\"")!=std::string::npos);
                    REQUIRE(json.find(std::string("\"classesCoverage\":\"")+classCoverage+"\"")!=std::string::npos);
                    REQUIRE(json.find(std::string("\"memoryAccountingAvailable\":")+(!level || dropped || saturated ? "false" : "true"))!=std::string::npos);
                }
}
static void EmptyParity(){Build(Take());}
static void BooleanParity()
{
    std::vector<exec::ClassRow> rows;
    for(unsigned mask=0;mask<256;++mask)
    {
        auto row=Row();row.interpreterShadow=(mask&1)!=0;row.physicalInterpreter=(mask&2)!=0;
        row.isActive=(mask&4)!=0;row.cctorStarted=(mask&8)!=0;row.cctorFinished=(mask&16)!=0;
        row.hasInitializationException=(mask&32)!=0;row.pointerDetailsAvailable=(mask&64)!=0;row.staticStorageAvailable=(mask&128)!=0;
        row.staticStoragePointer=diag::Quoted(row.pointerDetailsAvailable && row.staticStorageAvailable ? "0x1234abcdef" : "");rows.push_back(row);
    }
    Build(Take(),rows);
}
static void CountedState()
{
    const char state[]={'R','e','a','d','y'};
    Build(Take(),{},frozen::Identity{3,{state,sizeof(state)},UINT64_MAX});
}
static void UnicodeAndControls()
{
    const std::string text("quote\"slash\\control\n\0\x01",22);
    const std::string unicode="\xe4\xb8\xad\xce\xa9\xf0\x9f\x8c\x8d";
    auto row=Row();row.logicalAssembly=diag::Quoted(text);row.typeKey=diag::Quoted(unicode);Build(Take(),{row});
}
static void OwnedInputs()
{
    auto snapshot=Take();std::string name="owned module",type="owned type",state="Committed";
    frozen::Identity identity{6,diag::Quoted(state),5};auto row=Row();row.logicalAssembly=diag::Quoted(name);row.typeKey=diag::Quoted(type);
    const auto expected=Expected(identity,snapshot,{row});auto capture=std::make_unique<frozen::Record>();
    REQUIRE(capture->Begin(identity,snapshot,1));REQUIRE(capture->AddClass(row));
    snapshot=r02::Snapshot{};name.assign("mutated");type.assign("destroyed");state.assign("other");
    REQUIRE(capture->Freeze());REQUIRE(std::string(capture->Data(),capture->Bytes())==expected);
}
static void InvalidUtf8()
{
    for(const std::string& text:{std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),std::string("\x80",1)})
    {
        auto capture=std::make_unique<frozen::Record>();auto row=Row();row.typeKey=diag::Quoted(text);
        REQUIRE(capture->Begin(Identity(),Take(),1));REQUIRE(!capture->AddClass(row));
        REQUIRE(capture->FirstError()==frozen::RecordError::InvalidUtf8 && !capture->Data());
        REQUIRE(!capture->Freeze() && capture->FirstError()==frozen::RecordError::InvalidUtf8);
    }
}
static void InvalidInputs()
{
    for(unsigned field=0;field<6;++field)
    {
        auto capture=std::make_unique<frozen::Record>();auto identity=Identity();auto row=Row();
        if(field==0)identity.state={nullptr,0};
        if(field==1)row.logicalAssembly={nullptr,0};
        if(field==2)row.typeKey={nullptr,1};
        if(field==3)row.staticStoragePointer={nullptr,0};
        if(field==4)identity.state=diag::Quoted("");
        if(field==5)identity.state={"A\0B",3};
        const bool begun=capture->Begin(identity,Take(),1);
        REQUIRE((field==0 || field>=4) ? !begun : begun && !capture->AddClass(row));
        REQUIRE(!capture->Complete() && capture->Bytes()==0 && !capture->Data());
    }
}
static void MissingAndExtraRows()
{
    auto a=std::make_unique<frozen::Record>();REQUIRE(a->Begin(Identity(),Take(),1));REQUIRE(!a->Freeze());
    auto b=std::make_unique<frozen::Record>();REQUIRE(b->Begin(Identity(),Take(),0));REQUIRE(!b->AddClass(Row()));
    auto c=std::make_unique<frozen::Record>();REQUIRE(!c->Begin(Identity(),Take(),1025));
    REQUIRE(a->FirstError()==frozen::RecordError::RowCount && b->FirstError()==frozen::RecordError::RowCount && c->BytesWrittenLowerBound()==0);
}
static void MaximumRows(){Build(Take(),std::vector<exec::ClassRow>(1024,Row()));}
static void WrongStages()
{
    auto a=std::make_unique<frozen::Record>();REQUIRE(!a->AddClass(Row()));
    auto b=std::make_unique<frozen::Record>();REQUIRE(!b->Freeze());
    auto c=std::make_unique<frozen::Record>();REQUIRE(c->Begin(Identity(),Take(),0));REQUIRE(!c->Begin(Identity(),Take(),0));
    auto d=Build(Take());REQUIRE(!d->Freeze() && !d->Data());
    auto e=Build(Take());REQUIRE(!e->AddClass(Row()) && !e->Data());
}
static void WholeObjectAlias()
{
    for(size_t offset:{size_t(0),sizeof(frozen::Record)/2,sizeof(frozen::Record)-1})
    {
        auto capture=std::make_unique<frozen::Record>();auto row=Row();
        row.typeKey={reinterpret_cast<const char*>(capture.get())+offset,1};
        REQUIRE(capture->Begin(Identity(),Take(),1));REQUIRE(!capture->AddClass(row));
        REQUIRE(capture->FirstError()==frozen::RecordError::AliasedInput);
    }
}
static void InvalidRangeBeforeDereference()
{
    auto capture=std::make_unique<frozen::Record>();auto row=Row();
    row.typeKey={reinterpret_cast<const char*>(UINTPTR_MAX-1),8};
    REQUIRE(capture->Begin(Identity(),Take(),1));REQUIRE(!capture->AddClass(row));
    REQUIRE(capture->FirstError()==frozen::RecordError::InvalidInput);
    auto other=std::make_unique<frozen::Record>();row.typeKey={reinterpret_cast<const char*>(1),frozen::Record::kMaxBytes+1};
    REQUIRE(other->Begin(Identity(),Take(),1));REQUIRE(!other->AddClass(row));REQUIRE(other->FirstError()==frozen::RecordError::Capacity);
}
static std::unique_ptr<frozen::Record> ExactCapacity()
{
    const auto sample=Take();auto row=Row();row.typeKey=diag::Quoted("");
    const size_t fixed=Expected(Identity(),sample,{row}).size();
    std::string text(frozen::Record::kMaxBytes-fixed,'x');row.typeKey=diag::Quoted(text);
    auto capture=Build(sample,{row});REQUIRE(capture->Bytes()==frozen::Record::kMaxBytes);
    return capture;
}
static void ExactByteLimit(){ExactCapacity();}
static void OneByteOverLimit()
{
    const auto sample=Take();auto row=Row();row.typeKey=diag::Quoted("");
    const size_t fixed=Expected(Identity(),sample,{row}).size();
    std::string text(frozen::Record::kMaxBytes-fixed+1,'x');row.typeKey=diag::Quoted(text);
    auto capture=std::make_unique<frozen::Record>();REQUIRE(capture->Begin(Identity(),sample,1));
    REQUIRE(capture->AddClass(row));REQUIRE(!capture->Freeze());
    REQUIRE(capture->FirstError()==frozen::RecordError::Capacity && capture->Bytes()==0 && !capture->Data());
    REQUIRE(capture->BytesWrittenLowerBound()<=frozen::Record::kMaxBytes);
}
static void EscapedByteLimit()
{
    std::string text(frozen::Record::kMaxBytes/6+1,'\1');auto row=Row();row.typeKey=diag::Quoted(text);
    auto capture=std::make_unique<frozen::Record>();REQUIRE(capture->Begin(Identity(),Take(),1));
    REQUIRE(!capture->AddClass(row));REQUIRE(capture->FirstError()==frozen::RecordError::Capacity);
    REQUIRE(!capture->Freeze() && !capture->Data());
}
struct GroupedDigits:std::numpunct<char>{char do_thousands_sep()const override{return ',';}std::string do_grouping()const override{return "\3";}};
static void RejectLocale()
{
    const std::locale previous=std::locale();
    struct Restore{std::locale value;~Restore(){std::locale::global(value);}} restore{previous};
    std::locale::global(std::locale(std::locale::classic(),new GroupedDigits));
    auto capture=std::make_unique<frozen::Record>();REQUIRE(!capture->Begin(Identity(),Take(),0));
    REQUIRE(capture->FirstError()==frozen::RecordError::UnsupportedLocale);
}
static void RejectIncompleteCounters()
{
    auto capture=std::make_unique<frozen::Record>();REQUIRE(!capture->Begin(Identity(),r02::Snapshot{},0));
    REQUIRE(capture->FirstError()==frozen::RecordError::InvalidCounters);
    std::ostringstream out;REQUIRE(!r02::AppendFrozenDiagnostics(out,r02::Snapshot{}));REQUIRE(out.str().empty());
}
static void RevokeComplete()
{
    auto capture=Build(Take());REQUIRE(!capture->RejectInvalidSource());REQUIRE(!capture->Data() && capture->Bytes()==0);
    Output sink;REQUIRE(!capture->EmitTo({&sink,Output::Write},frozen::Record::kMaxBytes).complete && sink.calls==0);
}
static void SinkPreflight()
{
    auto capture=Build(Take());Output sink;
    auto result=capture->EmitTo({&sink,Output::Write},capture->Bytes()-1);REQUIRE(result.error==diag::WriteError::ByteLimit && sink.calls==0);
    result=capture->EmitTo({&sink,nullptr},capture->Bytes());REQUIRE(result.error==diag::WriteError::InvalidInput && sink.calls==0);
}
static void ChunkedEmission()
{
    auto capture=ExactCapacity();Output sink;sink.bytes.reserve(capture->Bytes());
    const auto result=capture->EmitTo({&sink,Output::Write},capture->Bytes());
    REQUIRE(result.complete && result.bytesAccepted==capture->Bytes() && sink.maximum<=65536 && sink.calls==60);
    REQUIRE(sink.bytes==std::string(capture->Data(),capture->Bytes()));
}
static void FailingSink()
{
    auto capture=ExactCapacity();
    for(size_t failAt:{size_t(1),size_t(2)})
    {
        Output sink;sink.bytes.reserve(capture->Bytes());sink.failAt=failAt;
        const auto result=capture->EmitTo({&sink,Output::Write},capture->Bytes());
        REQUIRE(!result.complete && result.error==diag::WriteError::SinkFailure && sink.calls==failAt);
        REQUIRE(result.bytesAccepted==(failAt-1)*65536 && capture->Complete());
    }
}
static void ReentrantSink()
{
    auto capture=Build(Take());Output sink;sink.reenter=capture.get();
    const auto result=capture->EmitTo({&sink,Output::Write},capture->Bytes());
    REQUIRE(!result.complete && result.error==diag::WriteError::Reentered && sink.calls==1);
    REQUIRE(sink.nested.error==diag::WriteError::Reentered && capture->Complete());
}
static void RevokingSink()
{
    auto capture=ExactCapacity();Output sink;sink.revoke=capture.get();
    const auto result=capture->EmitTo({&sink,Output::Write},capture->Bytes());
    REQUIRE(!result.complete && result.error==diag::WriteError::InvalidInput && sink.calls==1 && !capture->Complete());
}
static void WriterFinishContract()
{
    Output sink;diag::Writer writer({&sink,Output::Write},10);writer<<'x'<<'y';
    const auto finished=writer.Finish();REQUIRE(finished.complete && finished.bytesAccepted==2);
    writer<<'z';const auto rejected=writer.Finish();
    REQUIRE(rejected.error==diag::WriteError::Finished && !rejected.complete && sink.bytes=="xy");
}
static void NoWatchedNew()
{
    auto capture=std::make_unique<frozen::Record>();const FakeSource source;
    r02::Snapshot snapshot;r02::SnapshotReceipt receipt;const auto row=Row();Output sink;sink.bytes.reserve(65536);
    r02::Snapshot actual;r02::SnapshotReceipt actualReceipt;
    REQUIRE(r02::Snapshot::TryCaptureCurrent(actual,actualReceipt)); // Healthy prewarm.
    watchedNew=0;watchNew=true;
    const bool live=r02::Snapshot::TryCaptureCurrent(actual,actualReceipt);
    const bool a=r02::Snapshot::TryCaptureFrom(source,snapshot,receipt);
    const bool b=capture->Begin(Identity(),snapshot,1),c=capture->AddClass(row),d=capture->Freeze();
    const auto result=capture->EmitTo({&sink,Output::Write},65536);
    watchNew=false;REQUIRE(live && a && b && c && d && result.complete && watchedNew==0);
    std::printf("OBSERVATION watchedOperatorNew=%zu snapshotBytes=%zu recordBytes=%zu\n",watchedNew,sizeof(r02::Snapshot),sizeof(frozen::Record));
}
static void RealSource()
{
    for(size_t i=0;i<r02::Snapshot::kMetricCount;++i)
    {
        const auto metric=static_cast<r02::Metric>(i);
        r02::ObservationCounters::Add(metric,metric==r02::Metric::MethodChecks || metric==r02::Metric::Transformations ? 10000 : i+1);
    }
    r02::Snapshot sample;r02::SnapshotReceipt receipt;
    REQUIRE(r02::Snapshot::TryCaptureCurrent(sample,receipt));
    for(size_t i=0;i<r02::Snapshot::kMetricCount;++i)REQUIRE(sample.Read(static_cast<r02::Metric>(i))==r02::ObservationCounters::Read(static_cast<r02::Metric>(i)));
    Build(sample);r02::ReferenceCounters::sample=nullptr;r02::ReferenceCounters::Level=r02::ObservationCounters::Level;
    std::ostringstream original,current;r02::AppendV1ReferenceDiagnostics(original);r02::AppendDiagnostics(current);
    REQUIRE(original.str()==current.str());
}
static void ConcurrentCounters()
{
    std::atomic<bool> go{false};std::vector<std::thread> workers;
    struct Join { std::vector<std::thread>& workers;std::atomic<bool>& go;
        ~Join(){go.store(true,std::memory_order_release);for(auto& t:workers)if(t.joinable())t.join();} } join{workers,go};
    for(int i=0;i<4;++i)workers.emplace_back([&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();for(int n=0;n<1000;++n){r02::ObservationCounters::Add(r02::Metric::MethodChecks,1);r02::ObservationCounters::Add(r02::Metric::ShadowMethodChecks,1,std::memory_order_release);}});
    go.store(true,std::memory_order_release);size_t accepted=0,rejected=0;
    for(int n=0;n<128;++n)
    {
        r02::Snapshot sample;r02::SnapshotReceipt receipt;
        if(r02::Snapshot::TryCaptureCurrent(sample,receipt)){++accepted;REQUIRE(sample.Read(r02::Metric::ShadowMethodChecks)<=sample.Read(r02::Metric::MethodChecks));}
        else{++rejected;REQUIRE(!sample.Complete() && !receipt.observedReadFenceStable);}
    }
    for(auto& thread:workers)thread.join();
    r02::Snapshot sample;r02::SnapshotReceipt receipt;REQUIRE(r02::Snapshot::TryCaptureCurrent(sample,receipt));
    std::printf("OBSERVATION concurrentAccepted=%zu concurrentRejected=%zu coherentVmEpochProven=false\n",accepted,rejected);
}
static void ActualSaturation()
{
    r02::ObservationCounters::Add(r02::Metric::MethodChecks,UINT64_MAX);
    r02::Snapshot sample;r02::SnapshotReceipt receipt;REQUIRE(r02::Snapshot::TryCaptureCurrent(sample,receipt));
    REQUIRE(r02::ObservationCounters::Level==0 ? !sample.Saturated() : sample.Saturated());Build(sample);
}
static void ActualThreadCapacity()
{
    for(size_t i=0;i<130;++i){std::thread thread([]{r02::ObservationCounters::Add(r02::Metric::MethodChecks,1);});thread.join();}
    r02::Snapshot sample;r02::SnapshotReceipt receipt;REQUIRE(r02::Snapshot::TryCaptureCurrent(sample,receipt));
    REQUIRE(r02::ObservationCounters::Level==0 ? sample.DroppedThreads()==0 : sample.DroppedThreads()>0);
    Build(sample);std::printf("OBSERVATION droppedCounterThreads=%llu\n",static_cast<unsigned long long>(sample.DroppedThreads()));
}
int main()
{
    static_assert(!std::is_copy_constructible<frozen::Record>::value && !std::is_move_constructible<frozen::Record>::value,"Capture must retain one owner");
    static_assert(std::is_trivially_copyable<r02::Snapshot>::value,"Snapshot owns scalar values only");
    const struct{const char* name;void(*run)();} tests[]={
        {"snapshot-all-fields-and-read-order",SnapshotAllFields},{"every-metric-change-clears-destination",EveryMetricChange},
        {"every-source-metadata-change",EveryMetadataChange},{"invalid-subsets-rejected",InvalidSubsets},
        {"disabled-policy",DisabledPolicy},{"coverage-precedence",CoveragePrecedence},
        {"empty-original-v1-parity",EmptyParity},{"256-boolean-row-original-v1-parity",BooleanParity},
        {"counted-nonterminated-state",CountedState},{"unicode-and-control-byte-parity",UnicodeAndControls},
        {"owned-inputs-survive-mutation",OwnedInputs},{"invalid-utf8-rejected",InvalidUtf8},{"invalid-null-inputs",InvalidInputs},
        {"missing-extra-and-overlimit-rows",MissingAndExtraRows},{"1024-row-original-v1-parity",MaximumRows},
        {"wrong-stages-and-reuse",WrongStages},{"whole-object-input-alias",WholeObjectAlias},
        {"invalid-range-before-dereference",InvalidRangeBeforeDereference},{"exact-field-byte-limit",ExactByteLimit},
        {"one-byte-over-field-limit",OneByteOverLimit},{"escape-expanded-byte-limit",EscapedByteLimit},
        {"nonclassic-locale-rejected",RejectLocale},{"incomplete-counters-rejected",RejectIncompleteCounters},
        {"late-source-revocation",RevokeComplete},{"sink-preflight-no-callback",SinkPreflight},
        {"bounded-chunked-emission",ChunkedEmission},{"sink-failure-no-continuation",FailingSink},
        {"reentrant-sink-rejected",ReentrantSink},{"revoking-sink-rejected",RevokingSink},
        {"writer-finish-contract-unchanged",WriterFinishContract},{"zero-watched-operator-new",NoWatchedNew},
        {"actual-r02-source-and-live-wrapper-parity",RealSource},{"actual-concurrent-counter-source",ConcurrentCounters},
        {"actual-counter-saturation",ActualSaturation},{"actual-counter-thread-capacity",ActualThreadCapacity}
    };
    for(const auto& test:tests)
    {
        try{test.run();std::printf("PASS %s\n",test.name);}
        catch(const std::exception& error){watchNew=false;std::fprintf(stderr,"FAIL %s: %s\n",test.name,error.what());return 1;}
    }
    std::printf("SUMMARY tests=%zu diagnosticsLevel=%d runtimeAcceptance=false\n",sizeof(tests)/sizeof(tests[0]),r02::ObservationCounters::Level);
    return 0;
}
