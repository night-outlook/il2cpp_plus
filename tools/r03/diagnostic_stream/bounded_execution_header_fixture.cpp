// Exact inactive production scalar method under independently modeled VM
// atomic sources; real bounded Capture and real legacy execution serializer.
// NO live Unity, native linked SDK or global VM epoch claim.
#include "AssemblyShadowBoundedExecutionHeader.h"
#include "AssemblyShadowMetadataLifetime.h"
#include "AssemblyShadowDiagnostics.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
extern bool watch;
extern size_t newCalls;

namespace il2cpp { namespace vm {
namespace assembly_shadow_metadata_lifetime {
    Gate g_MetadataLifetimeGate;
}
struct ActiveSnapshot { uint64_t generation=55; };
namespace {
enum class Mode : uint8_t {
    Stable, ChangeMethodCount, ChangeStateInsideSecond,
    ChangeActiveInsideSecond
};
Mode mode=Mode::Stable;
uint32_t totalReads=0;
ActiveSnapshot active1{55}, active2{55};
std::atomic<const ActiveSnapshot*> s_active{&active1};
std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Disabled};
struct FakeCounter {
    mutable uint64_t count=0;
    bool marksSample=false;
    uint64_t load(std::memory_order) const noexcept;
};
FakeCounter s_methodChecks{10,true};
FakeCounter s_shadowMethodChecks{4,false};
FakeCounter s_rejectedBaselineMethods{2,false};
FakeCounter s_baselineClassCctorStarted{3,false};
FakeCounter s_shadowClassCctorStarted{1,false};
FakeCounter s_interpreterTransformations{8,false};
FakeCounter s_shadowInterpreterTransformations{3,false};
FakeCounter s_droppedClassObservations{0,false};
uint64_t FakeCounter::load(std::memory_order) const noexcept
{
    if(marksSample)
    {
        ++totalReads;
        if(totalReads==2)
        {
            if(mode==Mode::ChangeMethodCount)++count;
            if(mode==Mode::ChangeStateInsideSecond)
                s_state.store(AssemblyShadowState::Failed,std::memory_order_release);
            if(mode==Mode::ChangeActiveInsideSecond)
                s_active.store(&active2,std::memory_order_release);
        }
    }
    return count;
}
}
namespace assembly_shadow_r02 {
struct ObservationCounters {
    static uint64_t dropped;
    static bool saturated;
    static uint64_t DroppedThreads() noexcept{return dropped;}
    static bool Saturated() noexcept{return saturated;}
};
uint64_t ObservationCounters::dropped=0;
bool ObservationCounters::saturated=false;
}
class AssemblyShadow {
public:
    static bool TryCaptureBoundedExecutionHeader(
        assembly_shadow_bounded_execution_header::Capture& capture);
};
// __EXACT_PRODUCTION_EXECUTION_SCALAR_SOURCE__
namespace {
namespace scalar=assembly_shadow_bounded_execution_header;
namespace serial=assembly_shadow_execution_diagnostic;

void Reset(Mode next=Mode::Stable)
{
    mode=next;totalReads=0;
    s_active.store(&active1,std::memory_order_release);
    s_state.store(AssemblyShadowState::Disabled,std::memory_order_release);
    s_methodChecks.count=10;s_shadowMethodChecks.count=4;
    s_rejectedBaselineMethods.count=2;
    s_baselineClassCctorStarted.count=3;
    s_shadowClassCctorStarted.count=1;
    s_interpreterTransformations.count=8;
    s_shadowInterpreterTransformations.count=3;
    s_droppedClassObservations.count=0;
    assembly_shadow_r02::ObservationCounters::dropped=0;
    assembly_shadow_r02::ObservationCounters::saturated=false;
}
serial::Header Expected() noexcept
{
    serial::Header h{};
    h.stateCode=static_cast<int32_t>(AssemblyShadowState::Disabled);
    h.state=AssemblyShadowDiagnostics::StateName(AssemblyShadowState::Disabled);
    h.generation=55;h.methodChecks=10;h.shadowMethodChecks=4;
    h.rejectedBaselineMethods=2;h.baselineClassCctorStarted=3;
    h.shadowClassCctorStarted=1;h.interpreterTransformations=8;
    h.shadowInterpreterTransformations=3;
    h.droppedClassObservations=0;
    return h;
}
void ExactHeaderParity()
{
    Reset();
    scalar::Capture capture;
    assert(AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.Complete());
    serial::Header actual{};
    assert(capture.Get(actual));
    std::ostringstream legacy, bounded;
    serial::Begin(legacy,Expected());
    serial::EndClasses(legacy);
    serial::Begin(bounded,actual);
    serial::EndClasses(bounded);
    assert(legacy.str()==bounded.str());
    assert(legacy.str().find("\"methodChecks\":10")!=std::string::npos);
    assert(legacy.str().find("\"classes\":[]")!=std::string::npos);
    std::cout<<"GROUP:exact-legacy-execution-header-byte-parity\n";
}
void ZeroHeapAfterPrewarm()
{
    Reset();
    (void)std::locale();
    scalar::Capture capture;
    newCalls=0;watch=true;
    const bool ok=AssemblyShadow::TryCaptureBoundedExecutionHeader(capture);
    const size_t counted=newCalls;watch=false;
    assert(ok && capture.Complete() && counted==0);
    std::cout<<"GROUP:no-new-allocations-after-prewarm\n";
}
void MalformedHeaderRejected()
{
    Reset();
    scalar::Capture capture;
    auto h=Expected();h.state=nullptr;
    assert(!capture.Begin(h,0,false));
    assert(capture.FirstError()==scalar::Error::InvalidInput);
    serial::Header out{};assert(!capture.Get(out));
    std::cout<<"GROUP:missing-state-source-denied\n";
}
void InvalidStateRejected()
{
    Reset();
    scalar::Capture capture;
    auto h=Expected();h.stateCode=999;
    assert(!capture.Begin(h,0,false));
    assert(capture.FirstError()==scalar::Error::InvalidInput);
    std::cout<<"GROUP:invalid-state-code-denied\n";
}
void CounterOrderRejected()
{
    Reset();
    scalar::Capture capture;
    auto h=Expected();h.shadowMethodChecks=h.methodChecks+1;
    assert(!capture.Begin(h,0,false));
    assert(capture.FirstError()==scalar::Error::CounterOrder);
    std::cout<<"GROUP:impossible-subset-total-refused\n";
}
void SaturatedCoverageRejected()
{
    Reset();
    assembly_shadow_r02::ObservationCounters::saturated=true;
    scalar::Capture capture;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.FirstError()==scalar::Error::CoverageUnavailable);
    std::cout<<"GROUP:saturated-counter-coverage-refused\n";
}
void DroppedThreadCoverageRejected()
{
    Reset();
    assembly_shadow_r02::ObservationCounters::dropped=1;
    scalar::Capture capture;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.FirstError()==scalar::Error::CoverageUnavailable);
    std::cout<<"GROUP:dropped-counter-thread-refused\n";
}
void ChangedMethodCounterRejected()
{
    Reset(Mode::ChangeMethodCount);
    scalar::Capture capture;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.FirstError()==scalar::Error::SourceChanged);
    assert(!capture.Complete());
    std::cout<<"GROUP:method-counter-changed-during-sampling\n";
}
void StateFlipWithinSecondReadRejected()
{
    Reset(Mode::ChangeStateInsideSecond);
    scalar::Capture capture;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.FirstError()==scalar::Error::SourceChanged);
    std::cout<<"GROUP:state-flip-within-second-sample\n";
}
void ActiveOwnerSwappedWithSameGenerationRejected()
{
    Reset(Mode::ChangeActiveInsideSecond);
    scalar::Capture capture;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.FirstError()==scalar::Error::SourceChanged);
    std::cout<<"GROUP:active-snapshot-rebound-same-generation\n";
}
void DoubleUseIsPermanentFailure()
{
    Reset();
    scalar::Capture capture;
    assert(AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(!AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.FirstError()==scalar::Error::WrongPhase);
    assert(!capture.Complete());
    std::cout<<"GROUP:single-owner-no-reentry\n";
}
void HeaderDoesNotRetainCallerStatePointer()
{
    Reset();
    scalar::Capture capture;
    auto h=Expected();
    char temporary[]="Disabled";
    h.state=temporary;
    assert(capture.Begin(h,0,false));
    assert(capture.Seal(h,0,false,true));
    temporary[0]='X';
    serial::Header output{};
    assert(capture.Get(output));
    assert(std::string(output.state)=="Disabled");
    std::cout<<"GROUP:state-name-enum-owned-not-borrowed\n";
}
void AdmissionClosedRejected()
{
    Reset();
    assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();
    scalar::Capture capture;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionHeader(capture));
    assert(capture.FirstError()==scalar::Error::CoverageUnavailable);
    assert(!capture.Complete());
    std::cout<<"GROUP:closed-gate-no-source-admission\n";
}
}} // vm and local anonymous

int main()
{
    using namespace il2cpp::vm;
    assert(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit());
    ExactHeaderParity();
    ZeroHeapAfterPrewarm();
    MalformedHeaderRejected();
    InvalidStateRejected();
    CounterOrderRejected();
    SaturatedCoverageRejected();
    DroppedThreadCoverageRejected();
    ChangedMethodCounterRejected();
    StateFlipWithinSecondReadRejected();
    ActiveOwnerSwappedWithSameGenerationRejected();
    DoubleUseIsPermanentFailure();
    HeaderDoesNotRetainCallerStatePointer();
    AdmissionClosedRejected();
    std::cout<<"P1 INACTIVE EXECUTION HEADER SOURCE HOST PASS\n";
}
