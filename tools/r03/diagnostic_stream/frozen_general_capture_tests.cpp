// Exact actual general V1 serializer versus P1 staged owned bounded capture.
// This is HOST SOURCE with prepared synthetic snapshot; NOT VM acquisition.
#include "AssemblyShadowFrozenGeneralCapture.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <limits>

extern bool watch;
extern size_t newCalls;
using namespace il2cpp::vm;
namespace fg=assembly_shadow_frozen_general;
namespace dg=assembly_shadow_diagnostic;
namespace il2cpp { namespace vm { class OriginalDiagnostics { public: static std::string Serialize(const ShadowDiagnosticSnapshot&); }; }}

static ShadowDiagnosticSnapshot MakeSnapshot(size_t count,bool enabled=true)
{
    ShadowDiagnosticSnapshot s;
    s.enabled=enabled;
    if (!enabled) return s;
    s.startupCandidateSchemaVersion=7;
    s.startupObservationMode="EarlyTracking";
    s.startupCandidateNames={"A","B\n","é 中文","quote\"\\","embedded\0nul"};
    s.state=AssemblyShadowState::FailedAfterCommit;
    s.lastError=AssemblyShadowError::ReferenceResolutionFailed;
    s.detail=std::string("detail\0",7)+"\n\t\\\"é中文";
    s.baselineBuildId="build\rX";
    s.patchId="patch";
    s.generation=std::numeric_limits<uint64_t>::max();
    s.expected=200;s.staged=199;s.retainedBytes=std::numeric_limits<uint64_t>::max();
    s.closureLoadOrder={"A","B","C"};
    s.stableAotNames={"System","mscorlib"};
    s.commitOrder={"A","B"};
    s.assemblies.push_back({"A","mvid",true,true,true,true,false});
    s.assemblies.push_back({"B","other",false,true,false,false,true});
    s.events.push_back({0,"Begin","A",2,1});
    s.events.push_back({UINT64_MAX,"Failed","B",6,15});
    s.baselineUses.push_back({"A",BaselineUseKind::MethodExecution,"first\0use","Type",
                              UINT64_MAX,UINT64_MAX});
    s.baselineUses.push_back({"B",BaselineUseKind::MonoScript,"note\n","T2",0,99});
    s.enumerationGeneration=21;
    s.ordinaryAssemblies.push_back({"A",false});
    s.ordinaryAssemblies.push_back({"B",true});
    s.classEnumerationGeneration=22;
    s.ordinaryClasses.reserve(count);
    for(size_t i=0;i<count;++i)
    {
        ShadowDiagnosticSnapshot::OrdinaryClass cl;
        cl.assemblyName="Namespace.Module_"+std::to_string(i);
        cl.typeName="Long.Prefix.Namespace.SyntheticClass_"+std::to_string(i)+
            "_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        cl.isInterpreter=(i%2)==1;
        cl.isConstructedGeneric=(i%3)==0;
        cl.usesStagedMetadata=(i%5)==0;
        s.ordinaryClasses.push_back(std::move(cl));
    }
    return s;
}
static dg::QuotedText Q(const std::string& s) {return dg::Quoted(s);}
static bool Build(fg::Capture& c,const ShadowDiagnosticSnapshot& s)
{
    if(!c.Begin({s.enabled,s.startupCandidateSchemaVersion,Q(s.startupObservationMode)})) return false;
    for(const auto& x:s.startupCandidateNames) if(!c.AddStartupCandidate(Q(x))) return false;
    const fg::Transaction transaction{s.state,s.lastError,Q(s.detail),Q(s.baselineBuildId),
        Q(s.patchId),s.generation,s.retainedBytes,s.expected,s.staged};
    if(!c.EndStartupCandidates(transaction)) return false;
    for(const auto& x:s.closureLoadOrder) if(!c.AddClosureName(Q(x))) return false;
    if(!c.EndClosure()) return false;
    for(const auto& x:s.stableAotNames) if(!c.AddStableAotName(Q(x))) return false;
    if(!c.EndStableAot()) return false;
    for(const auto& x:s.commitOrder) if(!c.AddCommitOrderName(Q(x))) return false;
    if(!c.EndCommitOrder()) return false;
    for(const auto& x:s.assemblies)
    {
        if(!c.AddAssembly({Q(x.name),Q(x.mvid),x.skeletonBuilt,x.runtimeMetadataInitialized,
                         x.published,x.moduleInitializerAttempted,x.moduleInitializerRan})) return false;
    }
    if(!c.EndAssemblies()) return false;
    for(const auto& x:s.events)
        if(!c.AddEvent({x.sequence,x.generation,x.stagedCount,Q(x.kind),Q(x.name)})) return false;
    if(!c.EndEvents()) return false;
    for(const auto& x:s.baselineUses)
        if(!c.AddBaselineUse({Q(x.name),Q(x.detail),Q(x.type),x.kind,x.thread,x.timestamp})) return false;
    if(!c.EndBaselineUses(s.enumerationGeneration)) return false;
    for(const auto& x:s.ordinaryAssemblies)
        if(!c.AddOrdinaryAssembly({Q(x.name),x.isInterpreter})) return false;
    if(!c.EndOrdinaryAssemblies(s.classEnumerationGeneration)) return false;
    for(const auto& x:s.ordinaryClasses)
        if(!c.AddOrdinaryClass({Q(x.assemblyName),Q(x.typeName),x.isInterpreter,
            x.isConstructedGeneric,x.usesStagedMetadata})) return false;
    return c.Freeze();
}
struct Buffer
{
    std::vector<char> data;
    size_t used=0,calls=0,failAt=0;
    explicit Buffer(size_t size):data(size,'\0'){}
    static bool Write(void* p,const char* data,size_t length) noexcept
    {
        Buffer& b=*static_cast<Buffer*>(p);
        if(++b.calls==b.failAt || length>b.data.size()-b.used) return false;
        if(length) std::memcpy(b.data.data()+b.used,data,length);
        b.used+=length;
        return true;
    }
    dg::Sink Sink() noexcept {return {this,Write};}
    std::string Str()const{return {data.data(),used};}
};
static void RoundTrip(size_t count,bool enabled)
{
    ShadowDiagnosticSnapshot snapshot=MakeSnapshot(count,enabled);
    const std::string frozenOriginal=OriginalDiagnostics::Serialize(snapshot);
    const std::string golden=AssemblyShadowDiagnostics::Serialize(snapshot);
    assert(frozenOriginal==golden); // Independently frozen V1 source.
    auto capture=std::make_unique<fg::Capture>();
    Buffer buffer(golden.size()+1);
    newCalls=0;watch=true;
    const bool built=Build(*capture,snapshot);
    const size_t allocatedDuringBuild=newCalls;
    const auto receipt=capture->EmitTo(buffer.Sink(),golden.size());
    const size_t allocatedDuringBoth=newCalls;
    watch=false;
    assert(built && capture->Complete());
    assert(allocatedDuringBuild==0 && allocatedDuringBoth==0);
    assert(receipt.complete && receipt.bytesAccepted==golden.size());
    assert(buffer.Str()==golden);
    assert(capture->Bytes()==golden.size());
    assert(std::memcmp(capture->Data(),golden.data(),golden.size())==0);
    assert(AssemblyShadowDiagnostics::SerializeTo(snapshot,{nullptr,nullptr},golden.size()).error==dg::WriteError::InvalidInput);
    // Complete owned bytes must survive destruction/mutation of every source
    // snapshot text field without VM pointers.
    snapshot=ShadowDiagnosticSnapshot{};
    Buffer second(golden.size());
    auto repeated=capture->EmitTo(second.Sink(),golden.size());
    assert(repeated.complete && second.Str()==golden);
    Buffer shortSink(golden.size());
    const auto shortReceipt=capture->EmitTo(shortSink.Sink(),golden.size()-1);
    assert(!shortReceipt.complete && shortReceipt.error==dg::WriteError::ByteLimit && shortSink.used==0);
    Buffer failed(golden.size());
    failed.failAt=1;
    const auto failure=capture->EmitTo(failed.Sink(),golden.size());
    assert(!failure.complete && failure.error==dg::WriteError::SinkFailure);
    assert(failed.used==0);
    if(count>=4096) assert(golden.size()>262144);
    std::cout << "general classes=" << count << " bytes=" << golden.size()
              << " exact owned byte parity and sink controls passed\n";
}
static void Negative()
{
    const auto source=MakeSnapshot(1);
    const auto startup=fg::Header{source.enabled,source.startupCandidateSchemaVersion,Q(source.startupObservationMode)};
    {
        auto c=std::make_unique<fg::Capture>();
        assert(!c->Freeze() && c->FirstError()==fg::CaptureError::WrongStage && !c->Complete());
        assert(!Build(*c,source) && c->Data()==nullptr);
    }
    {
        auto c=std::make_unique<fg::Capture>();
        assert(c->Begin(startup));
        assert(!c->EndClosure() && c->FirstError()==fg::CaptureError::WrongStage);
        assert(!c->AddStartupCandidate(Q(source.startupCandidateNames[0])));
    }
    {
        auto c=std::make_unique<fg::Capture>();
        const dg::QuotedText missing{nullptr,0};
        assert(!c->Begin({true,1,missing}) && c->FirstError()==fg::CaptureError::InvalidInput);
        assert(!c->Complete());
    }
    {
        auto c=std::make_unique<fg::Capture>();
        auto invalid=source;
        invalid.detail="\xc0\xaf";
        assert(!Build(*c,invalid));
        assert(c->FirstError()==fg::CaptureError::InvalidUtf8);
        assert(!c->Complete() && c->Data()==nullptr && c->Bytes()==0);
    }
    {
        auto c=std::make_unique<fg::Capture>();
        auto oversized=source;
        oversized.detail.assign(fg::Capture::kMaxBytes+1,'X');
        assert(!Build(*c,oversized));
        assert(c->FirstError()==fg::CaptureError::Capacity);
        assert(!c->Complete() && c->Data()==nullptr && c->Bytes()==0);
    }
    {
        auto c=std::make_unique<fg::Capture>();
        struct Grouped :std::numpunct<char> {
            char do_thousands_sep() const override {return '_';}
            std::string do_grouping() const override {return "\3";}
        };
        std::locale original=std::locale();
        std::locale::global(std::locale(std::locale::classic(),new Grouped));
        assert(!c->Begin(startup) && c->FirstError()==fg::CaptureError::UnsupportedLocale);
        std::locale::global(original);
    }
    std::cout<<"frozen general malformed/order/utf8/budget/locale negatives passed\n";
}
int main()
{
    std::locale::global(std::locale::classic());
    RoundTrip(0,false);
    RoundTrip(1,true);
    RoundTrip(4096,true);
    Negative();
    std::cout<<"P1 staged general builder complete under host-only owned-snapshot contract\n";
}
