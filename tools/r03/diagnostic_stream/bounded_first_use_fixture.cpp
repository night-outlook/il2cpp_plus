// Host-only exact production first-use method compiled against explicit
// CandidateRegistry/UsageLock mocks. Il2CppClass tokens are NEVER dereferenced.
// Other fields in the full general-diagnostic JSON check are mock data.
// No Unity native SDK, Player or semantic acceptance is implied.
#include "AssemblyShadowBoundedFirstUseCapture.h"
#include "AssemblyShadowFrozenGeneralCapture.h"
#include "AssemblyShadowDiagnostics.h"
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

extern bool watch;
extern size_t newCalls;

namespace il2cpp { namespace vm {
namespace {
struct UseRecord
{
    bool present=false;
    BaselineUseKind kind=BaselineUseKind::AssemblyReflection;
    char detail[160]{};
    const void* klass=nullptr;
    uint64_t thread=0,timestamp=0,sequence=0;
};
struct Candidate
{
    std::string name;
    UseRecord firstUse;
};
struct CandidateRegistry
{
    std::vector<std::unique_ptr<Candidate>> entries;
};
std::atomic<CandidateRegistry*> s_candidates{nullptr};
uint64_t s_usageGeneration=0;
std::atomic_flag s_usageLock=ATOMIC_FLAG_INIT;
unsigned usageLockEntries=0;
int mutationMode=0;
CandidateRegistry registry,otherRegistry;
void LockUsage()
{
    while(s_usageLock.test_and_set(std::memory_order_acquire)) {}
    ++usageLockEntries;
    if(usageLockEntries==2)
    {
        if(mutationMode==1) ++s_usageGeneration;
        if(mutationMode==2) s_candidates.store(&otherRegistry,std::memory_order_release);
    }
}
void UnlockUsage(){ s_usageLock.clear(std::memory_order_release); }
struct UsageLock
{
    UsageLock(){LockUsage();}
    ~UsageLock(){UnlockUsage();}
};
}
class AssemblyShadow
{
public:
    static bool TryCaptureBoundedFirstUses(
        assembly_shadow_bounded_first_use::Capture&);
};
// __EXACT_PRODUCTION_FIRST_USE_METHOD__

namespace u=assembly_shadow_bounded_first_use;
namespace d=assembly_shadow_diagnostic;
namespace g=assembly_shadow_frozen_general;
using Cap=u::Capture;
static std::unique_ptr<Candidate> Make(const std::string& name,bool present,
    BaselineUseKind kind,const char* detail,const void* physical,
    uint64_t thread,uint64_t timestamp,uint64_t sequence)
{
    auto x=std::make_unique<Candidate>();
    x->name=name;
    x->firstUse.present=present;
    x->firstUse.kind=kind;
    if(detail)
    {
        const auto n=std::strlen(detail);
        assert(n<sizeof(x->firstUse.detail));
        std::memcpy(x->firstUse.detail,detail,n+1);
    }
    x->firstUse.klass=physical;
    x->firstUse.thread=thread;
    x->firstUse.timestamp=timestamp;
    x->firstUse.sequence=sequence;
    return x;
}
static void Reset()
{
    watch=false;
    mutationMode=0;usageLockEntries=0;s_usageGeneration=19;
    registry.entries.clear();otherRegistry.entries.clear();
    registry.entries.push_back(Make("Assembly.A",true,BaselineUseKind::MethodExecution,
        "first",nullptr,7,11,1));
    registry.entries.push_back(Make("Unused",false,BaselineUseKind::AssemblyReflection,
        "",nullptr,0,0,0));
    registry.entries.push_back(Make(std::string("B\0identity",10),true,
        BaselineUseKind::MonoScript,"second \n",nullptr,UINT64_MAX,0,UINT64_MAX));
    registry.entries.push_back(Make("Third",true,BaselineUseKind::ClassInit,
        "",nullptr,99,123,0));
    s_candidates.store(&registry,std::memory_order_release);
}
static std::string ToString(d::QuotedText text)
{
    assert(text.data);
    return {text.data,text.bytes};
}
static std::unique_ptr<Cap> Collect()
{
    auto owned=std::make_unique<Cap>();
    newCalls=0;watch=true;
    const bool ok=AssemblyShadow::TryCaptureBoundedFirstUses(*owned);
    const size_t allocations=newCalls;
    watch=false;
    assert(ok && owned->Complete() && owned->FirstError()==u::Error::None);
    assert(allocations==0);
    return owned;
}
static void AssertUnchanged(const Cap& capture)
{
    assert(capture.Complete() && capture.SourceCandidateCount()==4);
    assert(capture.PresentCount()==3 && capture.CapturedUsageGeneration()==19);
    u::Row row;
    assert(capture.Get(0,row));
    assert(ToString(row.name)=="Assembly.A");
    assert(ToString(row.detail)=="first FirstUseSequence=1");
    assert(row.kind==BaselineUseKind::MethodExecution && row.thread==7 && row.timestamp==11);
    assert(ToString(row.type)=="");
    assert(!capture.Get(1,row));
    assert(capture.Get(2,row));
    assert(ToString(row.name)==std::string("B\0identity",10));
    assert(ToString(row.detail)=="second \n FirstUseSequence=18446744073709551615");
    assert(row.thread==UINT64_MAX && row.timestamp==0 && row.sequence==UINT64_MAX);
    assert(capture.Get(3,row));
    assert(ToString(row.detail)==" FirstUseSequence=0");
    assert(row.kind==BaselineUseKind::ClassInit);
    assert(!capture.Get(4,row));
}
static void NoClassSourceAndOwnership()
{
    Reset();
    auto capture=Collect();
    AssertUnchanged(*capture);
    registry.entries[0]->name="overwritten";
    std::memset(registry.entries[0]->firstUse.detail,'X',
                sizeof(registry.entries[0]->firstUse.detail));
    registry.entries[2]->name="changed";
    AssertUnchanged(*capture);
    auto poisoned=std::make_unique<Cap>();
    assert(poisoned->Begin(0) && poisoned->EndRaw(0) && poisoned->EndNames());
    assert(poisoned->Seal(0,true));
    assert(!poisoned->Seal(0,true) && poisoned->FirstError()==u::Error::WrongPhase);
}
static void FailClosed()
{
    Reset();mutationMode=1;
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::SourceChanged && !c->Complete());
    }
    Reset();mutationMode=2;
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::SourceChanged && !c->Complete());
    }
    Reset();int physical=42;
    registry.entries[0]->firstUse.klass=&physical;
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::UnverifiedClassMetadata && !c->Complete());
        u::Row row;
        assert(!c->Get(0,row)); // No class-name omission disguised as complete.
    }
    Reset();registry.entries[0]->firstUse.detail[159]='!';
    std::memset(registry.entries[0]->firstUse.detail,'a',160);
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::InvalidDetail);
    }
    Reset();registry.entries[1].reset();
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::InvalidInput);
    }
    Reset();registry.entries[0]->name.assign(Cap::kTextCapacity+1,'n');
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::TextCapacity);
    }
    Reset();
    for(size_t i=registry.entries.size();i<Cap::kMaximumCandidates+1;++i)
        registry.entries.push_back(Make("x",false,BaselineUseKind::AssemblyReflection,
            "",nullptr,0,0,0));
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::TooManyCandidates);
    }
}
static void ExplicitFutureMetadataGate()
{
    int token=1,other=2;
    u::Raw raw;
    raw.present=true;raw.kind=BaselineUseKind::ObjectAllocation;
    std::memcpy(raw.detail,"allocator",10);
    raw.opaqueClass=&token;raw.sequence=23;raw.thread=2;raw.timestamp=3;
    {
        auto cap=std::make_unique<Cap>();
        assert(cap->Begin(1) && cap->AddRaw(raw) && cap->EndRaw(2));
        assert(cap->AddCandidateName(0,d::Quoted("A")) && cap->EndNames());
        const void* classId=nullptr;
        assert(cap->PendingOpaqueClass(0,classId) && classId==&token);
        // A mismatched class identity MUST reject the entire capture.
        assert(!cap->ResolvePhysicalClass(0,&other,d::Quoted("N"),d::Quoted("C")));
        assert(cap->FirstError()==u::Error::ClassTokenMismatch && !cap->Complete());
    }
    {
        auto cap=std::make_unique<Cap>();
        assert(cap->Begin(1) && cap->AddRaw(raw) && cap->EndRaw(2));
        assert(cap->AddCandidateName(0,d::Quoted("A")) && cap->EndNames());
        // ONLY a fixture-supplied type: a real production resolver still needs
        // independent class owner/lifetime proof, never implied here.
        assert(cap->ResolvePhysicalClass(0,&token,d::Quoted(""),d::Quoted("Class")));
        assert(cap->Seal(2,true));
        u::Row row;
        assert(cap->Get(0,row) && ToString(row.type)==".Class");
        assert(ToString(row.detail)=="allocator FirstUseSequence=23");
    }
}
static void ComposeCompleteGeneral()
{
    Reset();
    auto first=Collect();
    // The original serializer is NOT modified; the remaining VM components
    // below are explicit synthetic fixtures, not actual class enumerations.
    ShadowDiagnosticSnapshot v1;
    v1.enabled=true;v1.state=AssemblyShadowState::FailedAfterCommit;
    v1.generation=5;
    v1.startupCandidateSchemaVersion=1;
    v1.startupObservationMode="EarlyTracking";
    v1.startupCandidateNames={"A"};
    v1.enumerationGeneration=6;
    v1.classEnumerationGeneration=7;
    for(size_t index=0;index<first->SourceCandidateCount();++index)
    {
        u::Row row;
        if(!first->Get(index,row))continue;
        ShadowUseDiagnostic item;
        item.name=ToString(row.name);
        item.kind=row.kind;
        item.detail=ToString(row.detail);
        item.type=ToString(row.type);
        item.thread=row.thread;item.timestamp=row.timestamp;
        v1.baselineUses.push_back(item);
    }
    const std::string expected=AssemblyShadowDiagnostics::Serialize(v1);
    auto general=std::make_unique<g::Capture>();
    assert(general->Begin({v1.enabled,v1.startupCandidateSchemaVersion,
                           d::Quoted(v1.startupObservationMode)}));
    for(auto& x:v1.startupCandidateNames)
        assert(general->AddStartupCandidate(d::Quoted(x)));
    assert(general->EndStartupCandidates({v1.state,v1.lastError,d::Quoted(v1.detail),
        d::Quoted(v1.baselineBuildId),d::Quoted(v1.patchId),v1.generation,
        v1.retainedBytes,v1.expected,v1.staged}));
    assert(general->EndClosure() && general->EndStableAot() &&
           general->EndCommitOrder() && general->EndAssemblies() &&
           general->EndEvents());
    for(size_t index=0;index<first->SourceCandidateCount();++index)
    {
        u::Row row;
        if(!first->Get(index,row))continue;
        assert(general->AddBaselineUse({row.name,row.detail,row.type,
                                         row.kind,row.thread,row.timestamp}));
    }
    assert(general->EndBaselineUses(v1.enumerationGeneration) &&
           general->EndOrdinaryAssemblies(v1.classEnumerationGeneration) &&
           general->Freeze());
    assert(general->Bytes()==expected.size() &&
           std::memcmp(general->Data(),expected.data(),expected.size())==0);
    std::cout<<"P1 real first-use raw subset + MOCK other VM general fields, V1 bytes="
             <<expected.size()<<" exact\n";
}
}} // il2cpp::vm

int main()
{
    il2cpp::vm::NoClassSourceAndOwnership();
    il2cpp::vm::FailClosed();
    il2cpp::vm::ExplicitFutureMetadataGate();
    il2cpp::vm::ComposeCompleteGeneral();
    std::cout<<"P1 first-use actual source method: bounded under UsageLock,"
             <<" class pointers unobserved, negative coverage PASSED\n";
}
