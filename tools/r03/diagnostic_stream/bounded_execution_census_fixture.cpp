// R03 P1: substitute the EXACT production execution census source method.
// Only the registry/VM metadata gate and R02 counter sources are mocked.
// Real bounded Capture and real Gate are compiled from production headers.
// This is an identity-only census, not class metadata or Player execution.
#include "AssemblyShadowBoundedExecutionCensus.h"
#include "AssemblyShadowMetadataLifetime.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <locale>
#include <type_traits>
extern bool watch;
extern size_t newCalls;
struct Il2CppClass { int proof=0; };
namespace il2cpp { namespace vm {
namespace assembly_shadow_metadata_lifetime {
    Gate g_MetadataLifetimeGate;
}
namespace assembly_shadow_r02 {
struct ObservationCounters {
    static constexpr int Level=2;
    static uint64_t dropped;
    static bool saturated;
    static uint64_t DroppedThreads() noexcept {return dropped;}
    static bool Saturated() noexcept {return saturated;}
};
uint64_t ObservationCounters::dropped=0;
bool ObservationCounters::saturated=false;
}
namespace {
constexpr size_t kMaximumExecutionClasses=1024;
std::array<Il2CppClass*,kMaximumExecutionClasses*2> s_executionClasses{};
size_t s_executionClassCount=0;
Il2CppClass one{1},two{2},three{3};
enum class Mode {
    None, AppendOnSecondRead, RebindSameCount, DropOnSecondRead
};
Mode mode=Mode::None;
int locks=0;
struct FakeDropCounter {
    uint64_t value=0;
    uint64_t load(std::memory_order) const noexcept {return value;}
};
FakeDropCounter s_droppedClassObservations{};
struct ExecutionObservationLock {
    ExecutionObservationLock()
    {
        ++locks;
        if(locks==2)
        {
            if(mode==Mode::AppendOnSecondRead)
            {s_executionClasses[777]=&three;++s_executionClassCount;}
            if(mode==Mode::RebindSameCount)s_executionClasses[31]=&three;
            if(mode==Mode::DropOnSecondRead)s_droppedClassObservations.value=1;
        }
    }
};
}
class AssemblyShadow {
public:
    static bool TryCaptureBoundedExecutionCensus(
        assembly_shadow_bounded_execution_census::Capture&);
};
// __EXACT_BOUND_EXECUTION_CENSUS_SOURCE__
namespace {
namespace census=assembly_shadow_bounded_execution_census;
void Reset(Mode m=Mode::None)
{
    mode=m;locks=0;
    s_executionClasses.fill(nullptr);
    s_executionClasses[5]=&one;
    s_executionClasses[31]=&two;
    s_executionClassCount=2;
    s_droppedClassObservations.value=0;
    assembly_shadow_r02::ObservationCounters::dropped=0;
    assembly_shadow_r02::ObservationCounters::saturated=false;
}
void RealRegistryCountWithoutMetadataDereference()
{
    Reset();
    census::Capture cap;
    assert(AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.Complete());
    census::Result output{};
    assert(cap.Get(output));
    assert(output.distinctClasses==2 && output.scannedSlots==2048);
    assert(output.classIdentitySourceOnly && !output.classPointerOriginProven);
    assert(!output.coherentVmEpochProven && !output.classTypeKeyCaptured);
    assert(!output.runtimeAcceptance && locks==2);
    static_assert(!std::is_pointer<decltype(output.distinctClasses)>::value,"No borrowed ptr");
    std::cout<<"GROUP:real-registry-two-observed-no-class-dereference\n";
}
void ZeroAllocationsOnActualSourcePath()
{
    Reset();
    census::Capture cap;
    (void)std::locale();
    newCalls=0;watch=true;
    const bool result=AssemblyShadow::TryCaptureBoundedExecutionCensus(cap);
    const size_t allocations=newCalls;
    watch=false;
    assert(result && allocations==0 && cap.Complete());
    std::cout<<"GROUP:zero-new-after-execution-census-prewarm\n";
}
void EmptyRegistryIsWellDefined()
{
    Reset();s_executionClasses.fill(nullptr);s_executionClassCount=0;
    census::Capture cap;
    assert(AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    census::Result result{};
    assert(cap.Get(result) && result.distinctClasses==0);
    std::cout<<"GROUP:empty-execution-registry\n";
}
void RegistryCountMismatch()
{
    Reset();s_executionClassCount=1;
    census::Capture cap;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.FirstError()==census::Error::RegistryMismatch && !cap.Complete());
    std::cout<<"GROUP:observed-slot-count-mismatch-refused\n";
}
void RegistryOverflow()
{
    Reset();s_executionClassCount=1025;
    census::Capture cap;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.FirstError()==census::Error::TooManyClasses);
    std::cout<<"GROUP:declared-class-limit-refused\n";
}
void ChangedCountBetweenScans()
{
    Reset(Mode::AppendOnSecondRead);
    census::Capture cap;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.FirstError()==census::Error::ObservedChange && !cap.Complete());
    std::cout<<"GROUP:concurrent-insert-source-change-refused\n";
}
void ChangedIdentityWithSameCount()
{
    Reset(Mode::RebindSameCount);
    census::Capture cap;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.FirstError()==census::Error::ObservedChange);
    std::cout<<"GROUP:rebound-identity-same-count-refused\n";
}
void DropSecondRead()
{
    Reset(Mode::DropOnSecondRead);
    census::Capture cap;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.FirstError()==census::Error::IncompleteCoverage);
    std::cout<<"GROUP:late-dropped-observation-refused\n";
}
void DroppedThreadsAndSaturation()
{
    Reset();assembly_shadow_r02::ObservationCounters::dropped=1;
    census::Capture dropped;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(dropped));
    assert(dropped.FirstError()==census::Error::IncompleteCoverage);
    Reset();assembly_shadow_r02::ObservationCounters::saturated=true;
    census::Capture saturated;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(saturated));
    assert(saturated.FirstError()==census::Error::IncompleteCoverage);
    std::cout<<"GROUP:dropped-thread-and-saturation-refused\n";
}
void UnsupportedProfileInPureCapture()
{
    census::Capture cap;
    census::Sample sample{};
    sample.tableCapacity=2048;
    sample.diagnosticsLevel=1;
    assert(!cap.Begin(sample));
    assert(cap.FirstError()==census::Error::IncompleteCoverage);
    std::cout<<"GROUP:partial-diagnostics-level-refused\n";
}
void AlreadyCompletedCaptureInvalidated()
{
    Reset();
    census::Capture cap;
    assert(AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.FirstError()==census::Error::WrongPhase && !cap.Complete());
    census::Result out{};
    assert(!cap.Get(out));
    std::cout<<"GROUP:reentry-invalidates-previous-census\n";
}
void OwnerGateClosed()
{
    Reset();
    assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.CloseAndDrain();
    census::Capture cap;
    assert(!AssemblyShadow::TryCaptureBoundedExecutionCensus(cap));
    assert(cap.FirstError()==census::Error::OwnerUnavailable);
    assert(locks==0);
    std::cout<<"GROUP:initial-metadata-admission-closed\n";
}
}}} // il2cpp vm local

int main()
{
    using namespace il2cpp::vm;
    assert(assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.OpenAfterInit());
    RealRegistryCountWithoutMetadataDereference();
    ZeroAllocationsOnActualSourcePath();
    EmptyRegistryIsWellDefined();
    RegistryCountMismatch();
    RegistryOverflow();
    ChangedCountBetweenScans();
    ChangedIdentityWithSameCount();
    DropSecondRead();
    DroppedThreadsAndSaturation();
    UnsupportedProfileInPureCapture();
    AlreadyCompletedCaptureInvalidated();
    OwnerGateClosed();
    std::cout<<"P1 R03 EXECUTION IDENTITY-ONLY CENSUS HOST PASS\n";
}
