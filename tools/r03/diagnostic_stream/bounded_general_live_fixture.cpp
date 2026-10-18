// Host model of the EXACT inactive AssemblyShadow live-source method.
// It compiles the actual production Source.inl with real owned-source Capture,
// frozen Composer and metadata Gate types. Only VM globals and the individual
// reader methods are explicit mocks. No Unity SDK, GC, Player or live VM epoch.
#include "AssemblyShadowBoundedGeneralLive.h"
#include "AssemblyShadowMetadataLifetime.h"
#include "AssemblyShadowDiagnostics.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <string>
extern bool watch;
extern size_t newCalls;

namespace hybridclr {
    const uint32_t g_assemblyShadowStartupCandidateSchemaVersion=1;
    const char* g_assemblyShadowStartupCandidates[4098]={nullptr};
}
namespace il2cpp { namespace vm {
namespace assembly_shadow_metadata_lifetime {
    Gate g_MetadataLifetimeGate;
}
struct ActiveSnapshot { uint64_t generation=55; };
struct CandidateRegistry {int token=3;};
struct StableAotConfiguration {int token=4;};
struct TypeFailure {int token=5;};
enum class StartupAttempt {Unattempted,Initializing,Ready,Failed};
namespace {
std::atomic<CandidateRegistry*> s_candidates{nullptr};
std::atomic<const StableAotConfiguration*> s_configuration{nullptr};
std::atomic<StartupAttempt> s_startupAttempt{StartupAttempt::Ready};
std::atomic<bool> s_earlyTracking{true};
std::atomic<const ActiveSnapshot*> s_active{nullptr};
std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Disabled};
std::atomic<bool> s_lateBaselineUse{false};
std::atomic<bool> s_unexpectedFailure{false};
std::atomic<bool> s_referenceViolation{false};
std::atomic<const std::string*> s_referenceViolationDetail{nullptr};
std::atomic<const TypeFailure*> s_typeFailure{nullptr};
uint64_t s_usageGeneration=19;
int usageReads=0;
enum class Mode {
    Success, MissingTransaction, DenyTransaction,DenyFirst,DenyPhysical,
    DenyClasses, ChangedDuringRead, ChangedDuringCompose,
    StateFlipDuringRead, StartupTextOverflow
};
Mode mode=Mode::Success;
ActiveSnapshot activeInstance{};
CandidateRegistry candidatesInstance{};
StableAotConfiguration configInstance{};
struct UsageLock
{
    UsageLock()
    {
        ++usageReads;
        if((mode==Mode::ChangedDuringRead && usageReads==2) ||
           (mode==Mode::ChangedDuringCompose && usageReads==3))
            ++s_usageGeneration;
        if(mode==Mode::StateFlipDuringRead && usageReads==2)
            s_state.store(AssemblyShadowState::Failed,std::memory_order_release);
    }
};
}
class AssemblyShadow
{
public:
    static bool TryCaptureBoundedGeneralLive(
        const assembly_shadow_bounded_general_live::Sources&,
        assembly_shadow_bounded_general_live::Scratch&,
        assembly_shadow_frozen_general::Capture&,
        assembly_shadow_bounded_general_live::Receipt&);
    static bool TryCaptureBoundedTransactionInventory(
        assembly_shadow_bounded_inventory::Capture&);
    static bool TryCaptureBoundedFirstUses(
        assembly_shadow_bounded_first_use::Capture&);
};
class Assembly
{
public:
    static bool TryCaptureBoundedPhysicalAssemblies(
        assembly_shadow_bounded_physical::Capture&);
};
class AssemblyShadowVisibility
{
public:
    static bool TryCaptureBoundedOrdinaryClasses(
        assembly_shadow_bounded_classes::Capture&);
};

// __EXACT_REAL_PRODUCTION_GENERAL_LIVE_SOURCE__
// The runner replaces this sentinel with the exact Source.inl bytes,
// ensuring mock testing never hand-transcribes the orchestrator.
bool AssemblyShadow::TryCaptureBoundedTransactionInventory(
    assembly_shadow_bounded_inventory::Capture& c)
{
    if(mode==Mode::DenyTransaction)return false;
    namespace tx=assembly_shadow_bounded_transaction;
    using assembly_shadow_diagnostic::Quoted;
    tx::Input s{};
    s.state=AssemblyShadowState::Disabled;
    s.lastError=AssemblyShadowError::Success;
    s.detail=Quoted("text");
    s.baselineBuildId=Quoted("");
    s.patchId=Quoted("");
    s.generation=55;
    if(!c.Begin(s) || !c.EndStable() || !c.EndClosure() ||
       !c.EndCommit() || !c.EndLocked())return false;
    return c.Seal(true,false,AssemblyShadowError::Success,Quoted(""),false);
}
bool AssemblyShadow::TryCaptureBoundedFirstUses(
    assembly_shadow_bounded_first_use::Capture& c)
{
    if(mode==Mode::DenyFirst)return false;
    return c.Begin(0) && c.EndRaw(s_usageGeneration) &&
           c.EndNames() && c.Seal(s_usageGeneration,true);
}
bool Assembly::TryCaptureBoundedPhysicalAssemblies(
    assembly_shadow_bounded_physical::Capture& c)
{
    if(mode==Mode::DenyPhysical)return false;
    return c.Begin() && c.Seal(activeInstance.generation,true);
}
bool AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(
    assembly_shadow_bounded_classes::Capture& c)
{
    if(mode==Mode::DenyClasses)return false;
    return c.Begin() && c.Seal(activeInstance.generation,true);
}

namespace {
namespace live=assembly_shadow_bounded_general_live;
namespace frozen=assembly_shadow_frozen_general;
struct Fixture
{
    std::unique_ptr<assembly_shadow_bounded_inventory::Capture> transaction{
        new assembly_shadow_bounded_inventory::Capture()};
    std::unique_ptr<assembly_shadow_bounded_first_use::Capture> firstUses{
        new assembly_shadow_bounded_first_use::Capture()};
    std::unique_ptr<assembly_shadow_bounded_physical::Capture> physical{
        new assembly_shadow_bounded_physical::Capture()};
    std::unique_ptr<assembly_shadow_bounded_classes::Capture> classes{
        new assembly_shadow_bounded_classes::Capture()};
    std::unique_ptr<frozen::Capture> output{new frozen::Capture()};
    std::unique_ptr<live::Scratch> scratch{new live::Scratch()};
    live::Receipt receipt{};
    live::Sources Sources()
    {return {transaction.get(),firstUses.get(),physical.get(),classes.get()};}
    bool Run()
    {
        return AssemblyShadow::TryCaptureBoundedGeneralLive(
            Sources(),*scratch,*output,receipt);
    }
};
void Reset(Mode desired)
{
    mode=desired;
    usageReads=0;
    s_usageGeneration=19;
    activeInstance.generation=55;
    s_active.store(&activeInstance,std::memory_order_release);
    s_candidates.store(&candidatesInstance,std::memory_order_release);
    s_configuration.store(&configInstance,std::memory_order_release);
    s_state.store(AssemblyShadowState::Disabled,std::memory_order_release);
    s_startupAttempt.store(StartupAttempt::Ready,std::memory_order_release);
    s_typeFailure.store(nullptr,std::memory_order_release);
    s_lateBaselineUse.store(false,std::memory_order_release);
    s_unexpectedFailure.store(false,std::memory_order_release);
    s_referenceViolation.store(false,std::memory_order_release);
    s_referenceViolationDetail.store(nullptr,std::memory_order_release);
    s_earlyTracking.store(true,std::memory_order_release);
    hybridclr::g_assemblyShadowStartupCandidates[0]="Module.Contracts";
    hybridclr::g_assemblyShadowStartupCandidates[1]=nullptr;
}
void SuccessAndExactV1Parity()
{
    Reset(Mode::Success);
    Fixture f;
    assert(f.Run());
    assert(f.output->Complete() && f.receipt.componentSourcesComplete);
    assert(f.receipt.observableSourceFenceStable);
    assert(f.receipt.usageGenerationBefore==19 &&
           f.receipt.usageGenerationAfter==19);
    assert(f.receipt.activeGenerationBefore==55 &&
           f.receipt.activeGenerationAfter==55);
    assert(f.receipt.startupCandidates==1);
    assert(f.receipt.error==live::Error::None);
    assert(f.receipt.composed.serializedBytes==f.output->Bytes());
    assert(!f.receipt.coherentVmEpochProven && !f.receipt.runtimeAcceptance &&
           !f.receipt.playerExecution);
    ShadowDiagnosticSnapshot legacy{};
    legacy.enabled=true;
    legacy.startupCandidateSchemaVersion=1;
    legacy.startupObservationMode="EarlyTracking";
    legacy.startupCandidateNames={"Module.Contracts"};
    legacy.state=AssemblyShadowState::Disabled;
    legacy.lastError=AssemblyShadowError::Success;
    legacy.detail="text";
    legacy.generation=55;
    legacy.enumerationGeneration=55;
    legacy.classEnumerationGeneration=55;
    const std::string exact=AssemblyShadowDiagnostics::Serialize(legacy);
    assert(f.output->Bytes()==exact.size());
    assert(std::memcmp(f.output->Data(),exact.data(),exact.size())==0);
    std::cout<<"GROUP:actual-method-legacy-v1-parity\n";
}
void AllocationAfterPrewarm()
{
    Reset(Mode::Success);
    Fixture f;
    (void)std::locale();
    newCalls=0;watch=true;
    const bool ok=f.Run();
    const size_t allocated=newCalls;
    watch=false;
    assert(ok && allocated==0 && f.output->Complete());
    std::cout<<"GROUP:actual-method-zero-new-after-prewarm\n";
}
void MissingOwnedSource()
{
    Reset(Mode::Success);
    Fixture f;
    auto bundle=f.Sources();
    bundle.transaction=nullptr;
    assert(!AssemblyShadow::TryCaptureBoundedGeneralLive(
        bundle,*f.scratch,*f.output,f.receipt));
    assert(f.receipt.error==live::Error::MissingSource);
    assert(!f.output->Complete() && f.output->Bytes()==0);
    std::cout<<"GROUP:missing-owned-source-denied\n";
}
void SourceFailure(Mode m,live::Error expected,const char* group)
{
    Reset(m);
    Fixture f;
    assert(!f.Run());
    assert(f.receipt.error==expected);
    assert(!f.output->Complete() && f.output->Bytes()==0);
    assert(!f.receipt.observableSourceFenceStable);
    std::cout<<"GROUP:"<<group<<"\n";
}
void SourceChanged(Mode m,const char* group)
{
    Reset(m);
    Fixture f;
    assert(!f.Run());
    assert(f.receipt.error==live::Error::ObservedSourceChange);
    assert(!f.output->Complete() && f.output->Data()==nullptr);
    assert(!f.receipt.observableSourceFenceStable);
    assert(f.receipt.usageGenerationAfter>f.receipt.usageGenerationBefore ||
           m==Mode::StateFlipDuringRead);
    std::cout<<"GROUP:"<<group<<"\n";
}
void StartupLimit()
{
    Reset(Mode::StartupTextOverflow);
    Fixture f;
    std::string tooLong(live::Scratch::kMaximumStartupTextBytes,'x');
    hybridclr::g_assemblyShadowStartupCandidates[0]=tooLong.c_str();
    assert(!f.Run());
    assert(f.receipt.error==live::Error::MalformedStartup);
    assert(!f.output->Complete() && f.output->Bytes()==0);
    std::cout<<"GROUP:bounded-startup-scan-refusal\n";
}
void NoReentry()
{
    Reset(Mode::Success);
    Fixture f;
    assert(f.Run());
    assert(!f.Run());
    assert(f.receipt.error==live::Error::MissingSource);
    assert(!f.output->Complete() && f.output->Bytes()==0);
    std::cout<<"GROUP:single-owner-one-shot-output\n";
}
void AdmissionClosed()
{
    Reset(Mode::Success);
    assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();
    Fixture f;
    assert(!f.Run());
    assert(f.receipt.error==live::Error::MetadataAdmissionUnavailable);
    assert(!f.output->Complete() && f.output->Bytes()==0);
    std::cout<<"GROUP:closed-metadata-admission-refusal\n";
}
}}} // namespaces

int main()
{
    using namespace il2cpp::vm;
    assert(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit());
    SuccessAndExactV1Parity();
    AllocationAfterPrewarm();
    MissingOwnedSource();
    SourceFailure(Mode::DenyTransaction,assembly_shadow_bounded_general_live::Error::TransactionFailed,
                  "transaction-source-failure");
    SourceFailure(Mode::DenyFirst,assembly_shadow_bounded_general_live::Error::FirstUseFailed,
                  "first-use-source-failure");
    SourceFailure(Mode::DenyPhysical,assembly_shadow_bounded_general_live::Error::PhysicalFailed,
                  "physical-assembly-source-failure");
    SourceFailure(Mode::DenyClasses,assembly_shadow_bounded_general_live::Error::ClassesFailed,
                  "ordinary-class-source-failure");
    SourceChanged(Mode::ChangedDuringRead,"usage-generation-changed-during-read");
    SourceChanged(Mode::ChangedDuringCompose,"usage-generation-changed-during-compose");
    SourceChanged(Mode::StateFlipDuringRead,"terminal-state-changed-during-read");
    StartupLimit();
    NoReentry();
    AdmissionClosed();
    std::cout<<"P1 REAL GENERAL LIVE SOURCE METHOD HOST PASS\n";
}
