// Host-only: compile the exact current production
// AssemblyShadow::TryCaptureBoundedTransactionInventory body under mock source
// objects with the SAME referenced members/atomic field types. This does not
// emulate the entire Unity VM, callback scheduler, or metadata lifetime.
#include "AssemblyShadowBoundedTransactionInventory.h"
#include "AssemblyShadowFrozenGeneralCapture.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <limits>

extern bool watch;
extern size_t newCalls;

namespace il2cpp { namespace vm {
namespace {
struct Candidate { std::string name; };
struct StagedAssembly
{
    std::string mvid;
    bool skeletonBuilt=false, runtimeMetadataInitialized=false, published=false;
    std::atomic<bool> moduleInitializerAttempted{false},moduleInitializerRan{false};
};
struct ClosureMember { Candidate* candidate=nullptr; StagedAssembly* staged=nullptr; };
struct Event
{
    uint64_t sequence=0; std::string kind,name;
    uint64_t generation=0; size_t stagedCount=0;
};
struct Transaction
{
    std::mutex mutex;
    AssemblyShadowError lastError=AssemblyShadowError::Success;
    std::string detail,baselineBuildId,patchId;
    std::vector<ClosureMember> closure;
    std::vector<std::string> commitOrder;
    std::vector<Event> events;
    uint64_t retainedBytes=0;
};
struct StableAotConfiguration { std::vector<std::string> stableNames; };
struct TypeFailure { AssemblyShadowError error; std::string detail; };
struct ActiveSnapshot { uint64_t generation=1; };

Transaction source;
Candidate a{"A"},b{"B"},c{"C"};
StagedAssembly stageA,stageC;
StableAotConfiguration configuration, replacementConfiguration;
TypeFailure failure{AssemblyShadowError::ReferenceResolutionFailed,"durable first failure"};
ActiveSnapshot active{31};
std::atomic<const TypeFailure*> s_typeFailure{nullptr};
std::atomic<const ActiveSnapshot*> s_active{nullptr};
std::atomic<const StableAotConfiguration*> s_configuration{nullptr};
std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Committed};
std::atomic<bool> s_unexpectedFailure{false},s_lateBaselineUse{false};
int mutationMode=0,generationCalls=0;
Transaction& Current() { return source; }
size_t StagedCount(const Transaction& transaction)
{
    size_t count=0;
    for(const auto& member:transaction.closure) if(member.staged) ++count;
    return count;
}
} // matches the native production anonymous-namespace names

class AssemblyShadow
{
public:
    static bool TryCaptureBoundedTransactionInventory(
        assembly_shadow_bounded_inventory::Capture&);
    static uint64_t ActiveGeneration()
    {
        ++generationCalls;
        if (generationCalls==2)
        {
            switch(mutationMode)
            {
                case 1: s_typeFailure.store(&failure,std::memory_order_release);break;
                case 2: s_state.store(AssemblyShadowState::FailedAfterCommit,std::memory_order_release);break;
                case 3: s_active.store(&active,std::memory_order_release);break;
                case 4: s_lateBaselineUse.store(true,std::memory_order_release);break;
                case 5: s_configuration.store(&replacementConfiguration,std::memory_order_release);break;
                case 6: s_unexpectedFailure.store(true,std::memory_order_release);break;
                case 7: ++active.generation;break;
                default:break;
            }
        }
        const auto* snapshot=s_active.load(std::memory_order_acquire);
        return snapshot?snapshot->generation:0;
    }
};
// __EXACT_PRODUCTION_METHOD_INSERTED_HERE__

namespace inv=assembly_shadow_bounded_inventory;
namespace stat=assembly_shadow_bounded_transaction;
namespace fg=assembly_shadow_frozen_general;
namespace dg=assembly_shadow_diagnostic;
using Capture=inv::Capture;
static std::string S(dg::QuotedText v)
{
    assert(v.data);
    return std::string(v.data,v.bytes);
}
static void Reset()
{
    watch=false;
    mutationMode=0;generationCalls=0;
    s_state.store(AssemblyShadowState::Committed);
    s_typeFailure.store(nullptr);
    s_active.store(nullptr);
    s_configuration.store(&configuration);
    s_unexpectedFailure.store(false);
    s_lateBaselineUse.store(false);
    active.generation=31;
    a.name="A";b.name="B\n";c.name="C\"\\";
    configuration.stableNames={"System","mscorlib"};
    replacementConfiguration.stableNames={"Different"};
    stageA.mvid="mvid-A";stageA.skeletonBuilt=true;
    stageA.runtimeMetadataInitialized=true;
    stageA.published=true;
    stageA.moduleInitializerAttempted.store(true);
    stageA.moduleInitializerRan.store(true);
    stageC.mvid="mvid-C";
    stageC.skeletonBuilt=true;stageC.runtimeMetadataInitialized=false;
    stageC.published=false;
    stageC.moduleInitializerAttempted.store(true);
    stageC.moduleInitializerRan.store(false);
    source.lastError=AssemblyShadowError::MetadataCapacityExceeded;
    source.detail=std::string("state\0with newline\n\xE4\xB8\xAD",20);
    source.baselineBuildId="baseline-identity";
    source.patchId="patch-identity";
    source.closure={{&a,&stageA},{&b,nullptr},{&c,&stageC}};
    source.commitOrder={"A","C"};
    source.events={{1,"begin","A",0,0},
                   {2,"staged\n","B\"\\",5,2},
                   {3,"finish","",31,3}};
    source.retainedBytes=UINT64_MAX;
    failure.error=AssemblyShadowError::ReferenceResolutionFailed;
    failure.detail="durable first failure";
}
static std::unique_ptr<Capture> Collect()
{
    auto owned=std::make_unique<Capture>(); // preallocate while healthy
    newCalls=0;watch=true;
    const bool result=AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned);
    const size_t during=newCalls;
    watch=false;
    assert(result && owned->Complete());
    assert(during==0); // operator-new only; not a full VM RSS/GC bound.
    return owned;
}
static void Inspect(const Capture& out,bool expectActive=false)
{
    stat::View status;
    assert(out.GetStatus(status));
    assert(status.state==AssemblyShadowState::Committed ||
           status.state==AssemblyShadowState::Failed ||
           status.state==AssemblyShadowState::FailedAfterCommit);
    assert(status.expected==3 && status.staged==2 && status.retainedBytes==UINT64_MAX);
    assert(S(status.baselineBuildId)=="baseline-identity");
    assert(S(status.patchId)=="patch-identity");
    assert(status.generation==(expectActive?31u:0u));
    assert(out.ObservedStableNames()==2 &&
           out.ObservedClosure()==3 && out.ObservedCommitNames()==2 &&
           out.ObservedEvents()==3);
    dg::QuotedText text;
    assert(out.GetStable(0,text) && S(text)=="System");
    assert(out.GetStable(1,text) && S(text)=="mscorlib");
    inv::AssemblyView member;
    assert(out.GetClosure(0,member));
    assert(S(member.name)=="A" && S(member.mvid)=="mvid-A");
    assert(member.skeletonBuilt && member.runtimeMetadataInitialized && member.published);
    assert(member.moduleInitializerAttempted && member.moduleInitializerRan);
    assert(out.GetClosure(1,member));
    assert(S(member.name)=="B\n" && S(member.mvid)=="");
    assert(!member.skeletonBuilt && !member.moduleInitializerAttempted);
    assert(out.GetClosure(2,member));
    assert(S(member.name)=="C\"\\" && S(member.mvid)=="mvid-C");
    assert(member.skeletonBuilt && !member.published && member.moduleInitializerAttempted && !member.moduleInitializerRan);
    assert(out.GetCommit(0,text) && S(text)=="A");
    assert(out.GetCommit(1,text) && S(text)=="C");
    inv::EventView event;
    assert(out.GetEvent(1,event) && event.sequence==2 && event.generation==5);
    assert(event.stagedCount==2 && S(event.kind)=="staged\n" && S(event.name)=="B\"\\");
    assert(!out.GetClosure(3,member) && !out.GetEvent(3,event) && !out.GetStable(2,text));
}
static void BaselineAndSourceDestruction()
{
    Reset();
    const auto owned=Collect();
    stat::View status;
    assert(owned->GetStatus(status));
    assert(status.state==AssemblyShadowState::Committed);
    assert(status.lastError==AssemblyShadowError::MetadataCapacityExceeded);
    const auto detail=S(status.detail);
    assert(detail==source.detail);
    Inspect(*owned);
    source.detail="overwritten";
    source.baselineBuildId="overwritten";
    source.patchId="overwritten";
    a.name="changed";b.name="changed";c.name="changed";
    stageA.mvid="changed";stageC.mvid="changed";
    configuration.stableNames={"changed"};
    source.commitOrder={"changed"};
    source.events={{99,"changed","changed",123,1}};
    Inspect(*owned);
    assert(S(status.detail)==detail);
}
static void Precedence()
{
    Reset();s_lateBaselineUse.store(true);
    {
        const auto owned=Collect();stat::View v;assert(owned->GetStatus(v));
        assert(v.lastError==AssemblyShadowError::BaselineAlreadyUsed);
        assert(S(v.detail)=="Physical baseline use after publication; restart required.");
    }
    Reset();s_unexpectedFailure.store(true);s_lateBaselineUse.store(true);
    {
        const auto owned=Collect();stat::View v;assert(owned->GetStatus(v));
        assert(v.lastError==AssemblyShadowError::InternalError);
        assert(S(v.detail)=="Unexpected native failure sealed the transaction; restart required.");
    }
    Reset();s_typeFailure.store(&failure);
    {
        const auto owned=Collect();stat::View v;assert(owned->GetStatus(v));
        assert(v.state==AssemblyShadowState::Failed);
        assert(v.lastError==AssemblyShadowError::ReferenceResolutionFailed);
        assert(S(v.detail)=="durable first failure");
        Inspect(*owned);
    }
    Reset();s_typeFailure.store(&failure);s_active.store(&active);
    {
        const auto owned=Collect();stat::View v;assert(owned->GetStatus(v));
        assert(v.state==AssemblyShadowState::FailedAfterCommit);
        assert(v.generation==31);
        Inspect(*owned,true);
    }
}
static void FlipAndLimit()
{
    for(int mode=1;mode<=6;++mode)
    {
        Reset();mutationMode=mode;
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::SourceChanged && !owned->Complete());
        stat::View status;
        inv::AssemblyView row;
        assert(!owned->GetStatus(status) && !owned->GetClosure(0,row));
    }
    Reset();s_active.store(&active);mutationMode=7;
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::SourceChanged);
    }
    Reset();configuration.stableNames.resize(Capture::kMaximumStableNames+1,"x");
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::StableLimit);
    }
    Reset();source.closure.resize(Capture::kMaximumClosure+1,{&a,&stageA});
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::ClosureLimit);
    }
    Reset();source.commitOrder.resize(Capture::kMaximumCommitNames+1,"a");
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::CommitLimit);
    }
    Reset();source.events.resize(Capture::kMaximumEvents+1,{1,"k","n",1,1});
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::EventLimit);
    }
    Reset();configuration.stableNames={std::string(Capture::kTextCapacity+1,'z')};
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::TextCapacity);
    }
    Reset();source.closure[0].candidate=nullptr;
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::MissingInput);
    }
    Reset();source.detail=std::string(stat::Capture::kMaxDetail+1,'z');
    {
        auto owned=std::make_unique<Capture>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*owned));
        assert(owned->FirstError()==inv::Error::StatusFailure);
        assert(owned->StatusFirstError()==stat::Error::DetailLimit);
    }
}
static void SourceAliasAndPhase()
{
    Reset();
    auto cptr=std::make_unique<Capture>();
    auto& owned=*cptr;
    assert(!owned.EndStable() && owned.FirstError()==inv::Error::WrongStage);
    stat::View bad;
    assert(!owned.GetStatus(bad));
    auto normal=std::make_unique<Capture>();
    stat::Input input;input.detail=dg::Quoted("d");input.baselineBuildId=dg::Quoted("b");
    input.patchId=dg::Quoted("p");
    assert(normal->Begin(input));
    assert(!normal->AddClosure({dg::Quoted("a"),dg::Quoted("b")}));
    assert(normal->FirstError()==inv::Error::WrongStage);
    auto alias=std::make_unique<Capture>();
    assert(alias->Begin(input));
    auto* own=reinterpret_cast<const char*>(alias.get());
    assert(!alias->AddStable({own,16}));
    assert(alias->FirstError()==inv::Error::Overlap);
    struct Adjacent { Capture capture; char input[64]; };
    auto adjacent=std::make_unique<Adjacent>();
    const char* text="adjacent-storage";
    std::memcpy(adjacent->input,text,16);
    assert(adjacent->capture.Begin(input));
    assert(adjacent->capture.AddStable({adjacent->input,16}));
    assert(adjacent->capture.EndStable() && adjacent->capture.EndClosure()
           && adjacent->capture.EndCommit() && adjacent->capture.EndLocked());
    assert(adjacent->capture.Seal(true,false,AssemblyShadowError::Success,dg::Quoted(""),false));
    dg::QuotedText name;assert(adjacent->capture.GetStable(0,name));
    assert(S(name)=="adjacent-storage");
}
static void GeneralV1Composition()
{
    Reset();
    const auto owned=Collect();
    stat::View status;assert(owned->GetStatus(status));
    // Source-backed transaction subset + independently prepared synthetic
    // startup, usage and class inventory: no claim of one live VM epoch.
    ShadowDiagnosticSnapshot original;
    original.enabled=true;
    original.startupCandidateSchemaVersion=2;
    original.startupObservationMode="EarlyTracking";
    original.startupCandidateNames={"A","B"};
    original.state=status.state;
    original.lastError=status.lastError;
    original.detail=S(status.detail);
    original.baselineBuildId=S(status.baselineBuildId);
    original.patchId=S(status.patchId);
    original.generation=status.generation;
    original.expected=status.expected;
    original.staged=status.staged;
    original.retainedBytes=status.retainedBytes;
    dg::QuotedText text;
    for(size_t i=0;i<owned->ObservedStableNames();++i)
    {assert(owned->GetStable(i,text));original.stableAotNames.push_back(S(text));}
    for(size_t i=0;i<owned->ObservedClosure();++i)
    {
        inv::AssemblyView row;assert(owned->GetClosure(i,row));
        original.closureLoadOrder.push_back(S(row.name));
        ShadowAssemblyDiagnostic v;
        v.name=S(row.name);v.mvid=S(row.mvid);
        v.skeletonBuilt=row.skeletonBuilt;v.runtimeMetadataInitialized=row.runtimeMetadataInitialized;
        v.published=row.published;v.moduleInitializerAttempted=row.moduleInitializerAttempted;
        v.moduleInitializerRan=row.moduleInitializerRan;
        original.assemblies.push_back(std::move(v));
    }
    for(size_t i=0;i<owned->ObservedCommitNames();++i)
    {assert(owned->GetCommit(i,text));original.commitOrder.push_back(S(text));}
    for(size_t i=0;i<owned->ObservedEvents();++i)
    {
        inv::EventView row;assert(owned->GetEvent(i,row));
        original.events.push_back({row.sequence,S(row.kind),S(row.name),row.generation,row.stagedCount});
    }
    original.enumerationGeneration=17;
    original.ordinaryAssemblies.push_back({"Physical",false});
    original.classEnumerationGeneration=18;
    original.ordinaryClasses.push_back({"Physical","Obj",false,false,false});
    original.baselineUses.push_back({"A",BaselineUseKind::MethodExecution,"reason","Type",7,8});

    const std::string legacy=AssemblyShadowDiagnostics::Serialize(original);
    auto staged=std::make_unique<fg::Capture>();
    assert(staged->Begin({true,original.startupCandidateSchemaVersion,
                          dg::Quoted(original.startupObservationMode)}));
    for(const auto& name:original.startupCandidateNames)
        assert(staged->AddStartupCandidate(dg::Quoted(name)));
    assert(staged->EndStartupCandidates({status.state,status.lastError,status.detail,
        status.baselineBuildId,status.patchId,status.generation,status.retainedBytes,
        status.expected,status.staged}));
    for(const auto& name:original.closureLoadOrder)assert(staged->AddClosureName(dg::Quoted(name)));
    assert(staged->EndClosure());
    for(const auto& name:original.stableAotNames)assert(staged->AddStableAotName(dg::Quoted(name)));
    assert(staged->EndStableAot());
    for(const auto& name:original.commitOrder)assert(staged->AddCommitOrderName(dg::Quoted(name)));
    assert(staged->EndCommitOrder());
    for(const auto& row:original.assemblies)
        assert(staged->AddAssembly({dg::Quoted(row.name),dg::Quoted(row.mvid),
            row.skeletonBuilt,row.runtimeMetadataInitialized,row.published,
            row.moduleInitializerAttempted,row.moduleInitializerRan}));
    assert(staged->EndAssemblies());
    for(const auto& row:original.events)
        assert(staged->AddEvent({row.sequence,row.generation,row.stagedCount,
            dg::Quoted(row.kind),dg::Quoted(row.name)}));
    assert(staged->EndEvents());
    for(const auto& row:original.baselineUses)
        assert(staged->AddBaselineUse({dg::Quoted(row.name),dg::Quoted(row.detail),
            dg::Quoted(row.type),row.kind,row.thread,row.timestamp}));
    assert(staged->EndBaselineUses(original.enumerationGeneration));
    for(const auto& row:original.ordinaryAssemblies)
        assert(staged->AddOrdinaryAssembly({dg::Quoted(row.name),row.isInterpreter}));
    assert(staged->EndOrdinaryAssemblies(original.classEnumerationGeneration));
    for(const auto& row:original.ordinaryClasses)
        assert(staged->AddOrdinaryClass({dg::Quoted(row.assemblyName),dg::Quoted(row.typeName),
            row.isInterpreter,row.isConstructedGeneric,row.usesStagedMetadata}));
    assert(staged->Freeze());
    assert(staged->Bytes()==legacy.size());
    assert(std::memcmp(staged->Data(),legacy.data(),legacy.size())==0);
    std::cout << "P1 transaction inventory to staged general exact V1 bytes="
              <<legacy.size()<<" (other VM domains are MOCKS)\n";
}
}} // il2cpp::vm

int main()
{
    il2cpp::vm::BaselineAndSourceDestruction();
    il2cpp::vm::Precedence();
    il2cpp::vm::FlipAndLimit();
    il2cpp::vm::SourceAliasAndPhase();
    il2cpp::vm::GeneralV1Composition();
    std::cout<<"P1 actual transaction inventory method: full rows/status/ownership/"
             <<"overflow/source races and V1 staged composition passed\n";
}
