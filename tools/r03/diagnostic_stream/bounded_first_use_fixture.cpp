// Host-only exact production first-use method compiled against explicit
// CandidateRegistry/UsageLock/metadata mocks with instrumented raw class names.
// Other fields in the full general-diagnostic JSON check are mock data.
// No Unity native SDK, Player or semantic acceptance is implied.
#include "AssemblyShadowBoundedFirstUseCapture.h"
#include "AssemblyShadowMetadataLifetime.h"
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
#include <thread>
#include <new>

extern bool watch;
extern size_t newCalls;

namespace first_use_host {
thread_local bool usageHeld=false,metadataHeld=false,legacyNameRead=false;
std::atomic<unsigned> metadataEntries{0},nameReads{0};
std::atomic<bool> pauseMetadata{false},metadataEntered{false},releaseMetadata{false};
void (*afterMetadata)()=nullptr; // Only a mock of an independent source writer.
}
struct CheckedName
{
    const char* bytes;
    operator const char*() const noexcept
    {
        if(!first_use_host::legacyNameRead)
        {
            assert(first_use_host::metadataHeld && !first_use_host::usageHeld);
            assert(il2cpp::vm::assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()>0);
            first_use_host::nameReads.fetch_add(1,std::memory_order_relaxed);
        }
        return bytes;
    }
};
// MOCK layout; actual Il2CppClass has many fields and is not ABI-tested here.
struct Il2CppClass { CheckedName namespaze,name; };
namespace il2cpp { namespace os {
class FastAutoLock final
{
    std::unique_lock<std::mutex> lock_;
public:
    explicit FastAutoLock(std::mutex* mutex):lock_(*mutex)
    {
        assert(!first_use_host::usageHeld && !first_use_host::metadataHeld);
        assert(vm::assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()>0);
        first_use_host::metadataHeld=true;
        first_use_host::metadataEntries.fetch_add(1,std::memory_order_relaxed);
        if(first_use_host::pauseMetadata.load(std::memory_order_acquire))
        {
            first_use_host::metadataEntered.store(true,std::memory_order_release);
            while(!first_use_host::releaseMetadata.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
    }
    ~FastAutoLock()
    {
        first_use_host::metadataHeld=false;
        lock_.unlock();
        if(first_use_host::afterMetadata)first_use_host::afterMetadata();
    }
};
}}
namespace il2cpp { namespace vm {
namespace assembly_shadow_metadata_lifetime { Gate g_MetadataLifetimeGate; }
namespace {
std::mutex g_MetadataLock;
struct UseRecord
{
    bool present=false;
    BaselineUseKind kind=BaselineUseKind::AssemblyReflection;
    char detail[160]{};
    const Il2CppClass* klass=nullptr;
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
    assert(!first_use_host::usageHeld && !first_use_host::metadataHeld);
    while(s_usageLock.test_and_set(std::memory_order_acquire)) {}
    first_use_host::usageHeld=true;
    ++usageLockEntries;
    if(usageLockEntries==2)
    {
        if(mutationMode==1) ++s_usageGeneration;
        if(mutationMode==2) s_candidates.store(&otherRegistry,std::memory_order_release);
    }
}
void UnlockUsage(){ first_use_host::usageHeld=false; s_usageLock.clear(std::memory_order_release); }
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
    BaselineUseKind kind,const char* detail,const Il2CppClass* physical,
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
    auto& gate=assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate;
    assert(gate.ReaderCount()==0);
    // HOST FIXTURE RESET, not runtime reopening. Each Reset creates a fresh
    // independent mock process/registry after all fixture threads have joined.
    // Reopening the SAME gate is tested separately and must reject old source.
    using MetadataGate=assembly_shadow_metadata_lifetime::Gate;
    gate.~MetadataGate();
    ::new (static_cast<void*>(&gate)) MetadataGate();
    assert(gate.OpenAfterInit());
    first_use_host::metadataEntries.store(0);
    first_use_host::nameReads.store(0);
    first_use_host::pauseMetadata.store(false);
    first_use_host::metadataEntered.store(false);
    first_use_host::releaseMetadata.store(false);
    first_use_host::afterMetadata=nullptr;
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
    Reset();Il2CppClass malformed{{nullptr},{"Class"}};
    registry.entries[0]->firstUse.klass=&malformed;
    {
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::InvalidInput && !c->Complete());
        u::Row row;
        assert(!c->Get(0,row)); // No null-name omission disguised as complete.
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
static void ExactCapacityAndPhaseContracts()
{
    Reset();
    for(size_t i=registry.entries.size();i<Cap::kMaximumCandidates;++i)
        registry.entries.push_back(Make("unused",false,
            BaselineUseKind::AssemblyReflection,"",nullptr,0,0,0));
    {
        auto cap=std::make_unique<Cap>();
        assert(AssemblyShadow::TryCaptureBoundedFirstUses(*cap));
        assert(cap->Complete() &&
               cap->SourceCandidateCount()==Cap::kMaximumCandidates &&
               cap->PresentCount()==3);
        u::Row row;
        assert(cap->Get(0,row) && !cap->Get(Cap::kMaximumCandidates-1,row));
        std::cout<<"first-use exact 4096-candidate capacity and sparse output passed\n";
    }
    {
        auto cap=std::make_unique<Cap>();
        assert(cap->Begin(1));
        assert(!cap->EndRaw(0));
        assert(cap->FirstError()==u::Error::IndexMismatch && !cap->Complete());
    }
    {
        auto cap=std::make_unique<Cap>();
        assert(cap->Begin(1));
        u::Raw value;value.present=true;
        std::memcpy(value.detail,"a",2);
        assert(cap->AddRaw(value) && cap->EndRaw(0));
        assert(!cap->AddCandidateName(0,{reinterpret_cast<const char*>(cap.get()),8}));
        assert(cap->FirstError()==u::Error::AliasedInput);
    }
    {
        // Adjacent independent input after the exact Capture object must not
        // be falsely rejected as aliasing its internal storage.
        struct Adjacent { Cap cap; char text[32]{}; };
        auto side=std::make_unique<Adjacent>();
        std::memcpy(side->text,"adjacent",9);
        assert(side->cap.Begin(1));
        u::Raw value;value.present=true;value.detail[0]=0;
        assert(side->cap.AddRaw(value) && side->cap.EndRaw(7));
        assert(side->cap.AddCandidateName(0,{side->text,8}));
        assert(side->cap.EndNames() && side->cap.Seal(7,true));
        u::Row row;assert(side->cap.Get(0,row));
        assert(ToString(row.name)=="adjacent");
        assert(ToString(row.detail)==" FirstUseSequence=0");
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
static void ComposeCompleteGeneral(bool denseClasses=false)
{
    Reset();
    Il2CppClass dense{{"Space.中文"},{"Type`1"}};
    if(denseClasses)
    {
        registry.entries.clear();
        for(size_t i=0;i<Cap::kMaximumCandidates;++i)
            registry.entries.push_back(Make("Assembly."+std::to_string(i),true,
                static_cast<BaselineUseKind>(i%9),"class source",&dense,
                UINT64_MAX-i,i,i));
    }
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
    // Independent expected source uses the legacy first-use producer's exact
    // field expressions, NOT rows returned by the new capture. Only names in
    // this expected-data block are permitted to be read outside the mock lock.
    first_use_host::legacyNameRead=true;
    std::vector<UseRecord> uses(registry.entries.size());
    {
        UsageLock usage;
        for(size_t index=0;index<uses.size();++index)
            uses[index]=registry.entries[index]->firstUse;
    }
    for(size_t index=0;index<uses.size();++index)
    {
        const auto& use=uses[index];
        if(!use.present)continue;
        ShadowUseDiagnostic diagnostic;
        diagnostic.name=registry.entries[index]->name;
        diagnostic.kind=use.kind;
        diagnostic.detail=std::string(use.detail)+" FirstUseSequence="+std::to_string(use.sequence);
        if(use.klass)diagnostic.type=std::string(use.klass->namespaze)+"."+static_cast<const char*>(use.klass->name);
        diagnostic.thread=use.thread;
        diagnostic.timestamp=use.timestamp;
        v1.baselineUses.push_back(diagnostic);
    }
    first_use_host::legacyNameRead=false;
    assert(v1.baselineUses.size()==first->PresentCount());
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
    if(denseClasses)assert(expected.size()>262144 && first->SourceCandidateCount()==4096);
    std::cout<<"P1 candidate first-use subset + MOCK other VM fields, independent V1 bytes="
             <<expected.size()<<" exact\n";
}
static void ClassSourceAndOwnership()
{
    Reset();
    std::string ns="Game.中文",name="C\n\"\\`1";
    auto source=std::make_unique<Il2CppClass>(Il2CppClass{{ns.c_str()},{name.c_str()}});
    Il2CppClass empty{{""},{"Global"}};
    registry.entries[0]->firstUse.klass=source.get();
    registry.entries[2]->firstUse.klass=&empty;
    auto captured=Collect();
    assert(first_use_host::metadataEntries.load()==1);
    assert(first_use_host::nameReads.load()==4);
    assert(usageLockEntries==2);
    assert(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()==0);
    u::Row row;assert(captured->Get(0,row));
    const std::string expected=ns+"."+name;
    assert(ToString(row.type)==expected);
    assert(captured->Get(2,row) && ToString(row.type)==".Global");
    const void* pending=nullptr;
    assert(!captured->PendingOpaqueClass(0,pending));
    source.reset();
    ns.assign(200,'z');name.assign(200,'q');
    registry.entries.clear();
    assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();
    assert(captured->Get(0,row) && ToString(row.type)==expected);
    assert(first_use_host::nameReads.load()==4); // Get never re-reads metadata.
}
static void NullAndAbsentClasses()
{
    for(int which=0;which<2;++which)
    {
        Reset();Il2CppClass bad{{which==0?nullptr:"N"},{which==1?nullptr:"C"}};
        registry.entries[0]->firstUse.klass=&bad;
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        assert(c->FirstError()==u::Error::InvalidInput && !c->Complete());
        u::Row row;assert(!c->Get(0,row));
        assert(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()==0);
    }
    Reset();
    registry.entries[1]->firstUse.klass=reinterpret_cast<const Il2CppClass*>(uintptr_t(1));
    auto absent=Collect();
    assert(first_use_host::nameReads.load()==0);
    u::Row row;assert(!absent->Get(1,row));
    Reset();Il2CppClass bothEmpty{{""},{""}};
    registry.entries[0]->firstUse.klass=&bothEmpty;
    auto c=Collect();assert(c->Get(0,row) && ToString(row.type)==".");
    Reset();s_candidates.store(nullptr);
    auto noRegistry=Collect();assert(noRegistry->PresentCount()==0);
}
static std::unique_ptr<Cap> Pending(const void* token)
{
    auto c=std::make_unique<Cap>();
    u::Raw raw;raw.present=true;raw.opaqueClass=token;
    assert(c->Begin(1) && c->AddRaw(raw) && c->EndRaw(19));
    assert(c->AddCandidateName(0,d::Quoted("A")) && c->EndNames());
    return c;
}
static void CStringBoundaries()
{
    int token=1;
    for(int extra=0;extra<=1;++extra)
    {
        auto c=Pending(&token);
        const auto remaining=Cap::kTextCapacity-c->CapturedTextBytes();
        const std::string text(remaining-1+extra,'x');
        const bool ok=c->ResolvePhysicalClassCString(0,&token,"",text.c_str());
        assert(ok==(extra==0));
        if(ok)
        {
            assert(c->Seal(19,true));
            u::Row row;assert(c->Get(0,row));
            assert(row.type.bytes==remaining && row.type.data[0]=='.');
            assert(c->CapturedTextBytes()==Cap::kTextCapacity);
        }
        else assert(c->FirstError()==u::Error::TextCapacity && !c->Complete());
    }
    {
        auto c=Pending(&token);
        const auto remaining=Cap::kTextCapacity-c->CapturedTextBytes();
        const std::string ns(remaining-1,'n');
        assert(c->ResolvePhysicalClassCString(0,&token,ns.c_str(),""));
        assert(c->Seal(19,true));
        u::Row row;assert(c->Get(0,row) && row.type.data[row.type.bytes-1]=='.');
    }
    {
        auto c=Pending(&token);
        // Exactly the readable bound, without a terminator. ASan must not see
        // a read beyond it while the bounded scan correctly rejects the row.
        const auto remaining=Cap::kTextCapacity-c->CapturedTextBytes();
        std::vector<char> nonterminated(remaining,'x');
        assert(!c->ResolvePhysicalClassCString(0,&token,"",nonterminated.data()));
        assert(c->FirstError()==u::Error::TextCapacity);
    }
    Reset();Il2CppClass normal{{"N"},{"C"}};
    std::string enormous(Cap::kTextCapacity+1,'n');
    Il2CppClass huge{{enormous.c_str()},{"C"}};
    registry.entries[0]->firstUse.klass=&normal;
    registry.entries[2]->firstUse.klass=&huge;
    auto c=std::make_unique<Cap>();
    assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
    u::Row row;assert(!c->Get(0,row) && !c->Get(2,row));
    assert(c->FirstError()==u::Error::TextCapacity);
}
static void CStringAliasAndToken()
{
    int token=1,other=2;
    {
        auto c=Pending(&token);
        const char* invalid=reinterpret_cast<const char*>(uintptr_t(1));
        assert(!c->ResolvePhysicalClassCString(0,&other,invalid,invalid));
        assert(c->FirstError()==u::Error::ClassTokenMismatch);
    }
    {
        auto c=Pending(&token);
        assert(!c->ResolvePhysicalClassCString(0,&token,
            reinterpret_cast<const char*>(c.get()),"C"));
        assert(c->FirstError()==u::Error::AliasedInput);
    }
    {
        struct Before {char prefix[8];Cap c;};
        auto before=std::make_unique<Before>();
        std::memset(before->prefix,'n',sizeof(before->prefix));
        u::Raw raw;raw.present=true;raw.opaqueClass=&token;
        assert(before->c.Begin(1) && before->c.AddRaw(raw) && before->c.EndRaw(0));
        assert(before->c.AddCandidateName(0,d::Quoted("A")) && before->c.EndNames());
        assert(!before->c.ResolvePhysicalClassCString(0,&token,before->prefix,"C"));
        assert(before->c.FirstError()==u::Error::AliasedInput);
    }
    {
        struct After {Cap c;char ns[8]="N",name[8]="C";};
        auto after=std::make_unique<After>();
        u::Raw raw;raw.present=true;raw.opaqueClass=&token;
        assert(after->c.Begin(1) && after->c.AddRaw(raw) && after->c.EndRaw(0));
        assert(after->c.AddCandidateName(0,d::Quoted("A")) && after->c.EndNames());
        assert(after->c.ResolvePhysicalClassCString(0,&token,after->ns,after->name));
        assert(after->c.Seal(0,true));
        u::Row row;assert(after->c.Get(0,row) && ToString(row.type)=="N.C");
    }
    {
        auto c=Pending(&token);
        assert(c->ResolvePhysicalClassCString(0,&token,"N","C"));
        assert(!c->ResolvePhysicalClassCString(0,&token,"N","C"));
        assert(c->FirstError()==u::Error::ClassTokenMismatch);
        assert(!c->Seal(19,true));
    }
}
static void OwnerUnavailable()
{
    Reset();auto& gate=assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate;
    gate.CloseAndDrain();
    registry.entries[0]->firstUse.klass=reinterpret_cast<const Il2CppClass*>(uintptr_t(1));
    auto c=std::make_unique<Cap>();
    assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
    assert(c->FirstError()==u::Error::OwnerUnavailable);
    assert(usageLockEntries==0 && first_use_host::metadataEntries.load()==0);
    assert(first_use_host::nameReads.load()==0 && gate.ReaderCount()==0);
    assert(!c->Complete());
    // A new epoch on the SAME gate must not authenticate the unstamped,
    // process-retained first-use registry, even when the test pointer is valid.
    Il2CppClass oldSource{{"Old"},{"Retired"}};
    registry.entries[0]->firstUse.klass=&oldSource;
    assert(gate.OpenAfterInit());
    auto reopened=std::make_unique<Cap>();
    assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*reopened));
    assert(reopened->FirstError()==u::Error::OwnerUnavailable);
    assert(usageLockEntries==0 && first_use_host::metadataEntries.load()==0);
    assert(first_use_host::nameReads.load()==0 && gate.ReaderCount()==0);
}
static void GenerationAfterMetadata()
{
    for(int mode=0;mode<2;++mode)
    {
        Reset();Il2CppClass physical{{"N"},{"C"}};
        registry.entries[0]->firstUse.klass=&physical;
        first_use_host::afterMetadata=mode==0 ? +[]{
            UsageLock lock;++s_usageGeneration;
        } : +[]{
            UsageLock lock;s_candidates.store(&otherRegistry,std::memory_order_release);
        };
        auto c=std::make_unique<Cap>();
        assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*c));
        first_use_host::afterMetadata=nullptr;
        assert(c->FirstError()==u::Error::SourceChanged && !c->Complete());
        assert(first_use_host::nameReads.load()==2);
        assert(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()==0);
    }
}
static void ShutdownDrain()
{
    Reset();Il2CppClass physical{{"N"},{"C"}};
    registry.entries[0]->firstUse.klass=&physical;
    auto& gate=assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate;
    auto capture=std::make_unique<Cap>();
    first_use_host::pauseMetadata.store(true);
    std::atomic<bool> result{false},drained{false};
    std::thread worker([&]{result.store(AssemblyShadow::TryCaptureBoundedFirstUses(*capture));});
    while(!first_use_host::metadataEntered.load(std::memory_order_acquire))std::this_thread::yield();
    assert(gate.ReaderCount()==1);
    std::thread closer([&]{gate.CloseAndDrain();drained.store(true);});
    while(!gate.Closed())std::this_thread::yield();
    assert(!drained.load() && gate.ReaderCount()==1);
    auto refused=std::make_unique<Cap>();
    assert(!AssemblyShadow::TryCaptureBoundedFirstUses(*refused));
    assert(refused->FirstError()==u::Error::OwnerUnavailable);
    first_use_host::releaseMetadata.store(true,std::memory_order_release);
    worker.join();closer.join();
    assert(result.load() && drained.load() && gate.ReaderCount()==0 && gate.Closed());
    assert(capture->Complete());
    u::Row row;assert(capture->Get(0,row) && ToString(row.type)=="N.C");
}
static void UnresolvedStillRefused()
{
    int token=1;
    auto c=Pending(&token);
    assert(!c->Seal(19,true));
    assert(c->FirstError()==u::Error::UnverifiedClassMetadata);
    const void* out=nullptr;assert(!c->PendingOpaqueClass(0,out));
    u::Row row;assert(!c->Get(0,row));
}
static void ParallelCaptures()
{
    Reset();Il2CppClass physical{{"N"},{"C"}};
    registry.entries[0]->firstUse.klass=&physical;
    auto a=std::make_unique<Cap>(),b=std::make_unique<Cap>();
    std::atomic<bool> ready{false};bool ra=false,rb=false;
    std::thread one([&]{while(!ready.load())std::this_thread::yield();ra=AssemblyShadow::TryCaptureBoundedFirstUses(*a);});
    std::thread two([&]{while(!ready.load())std::this_thread::yield();rb=AssemblyShadow::TryCaptureBoundedFirstUses(*b);});
    ready.store(true);one.join();two.join();
    assert(ra && rb && assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()==0);
    u::Row x,y;assert(a->Get(0,x) && b->Get(0,y) && ToString(x.type)==ToString(y.type));
}
}} // il2cpp::vm

int main(int argc,char** argv)
{
    using namespace il2cpp::vm;
    // The old-source negative build intentionally lacks the metadata operation.
    // Keep the mock referenced there without suppressing compiler warnings.
    (void)&g_MetadataLock;
    struct Case {const char* name;void (*run)();};
    const Case cases[]={
        {"classless-ownership",NoClassSourceAndOwnership},
        {"existing-fail-closed",FailClosed},
        {"existing-capacity-phase",ExactCapacityAndPhaseContracts},
        {"existing-token-contract",ExplicitFutureMetadataGate},
        {"classless-general-parity",[]{ComposeCompleteGeneral();}},
        {"class-ownership",ClassSourceAndOwnership},
        {"null-absent-names",NullAndAbsentClasses},
        {"cstring-budget",CStringBoundaries},
        {"cstring-alias",CStringAliasAndToken},
        {"owner-unavailable",OwnerUnavailable},
        {"usage-after-metadata",GenerationAfterMetadata},
        {"shutdown-drain",ShutdownDrain},
        {"dense-general-parity",[]{ComposeCompleteGeneral(true);}},
        {"unresolved-refusal",UnresolvedStillRefused},
        {"parallel-captures",ParallelCaptures}
    };
    unsigned completed=0;
    for(const auto& c:cases)
    {
        if(argc==2 && std::strcmp(c.name,argv[1])!=0)continue;
        std::cout<<"RUN "<<c.name<<std::endl;
        c.run();
        std::cout<<"PASS "<<c.name<<std::endl;
        ++completed;
    }
    assert(completed==(argc==2?1:sizeof(cases)/sizeof(cases[0])));
    std::cout<<"P1 first-use metadata HOST cases="<<completed<<" PASSED\n";
}
