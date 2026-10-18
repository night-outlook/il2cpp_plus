// Real owned R02 + real execution-scalar Capture + unchanged original
// serializers. The result is ONLY a classless JSON formatting fixture.
#include "AssemblyShadowBoundedExecutionComposite.h"
#include "AssemblyShadowDiagnostics.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
extern bool watch;
extern size_t newCalls;
namespace {
namespace ex=il2cpp::vm::assembly_shadow_execution_diagnostic;
namespace eh=il2cpp::vm::assembly_shadow_bounded_execution_header;
namespace r02=il2cpp::vm::assembly_shadow_r02;
namespace bm=il2cpp::vm::assembly_shadow_bounded_r02;
namespace bc=il2cpp::vm::assembly_shadow_bounded_execution_composite;
using il2cpp::vm::AssemblyShadowDiagnostics;
using il2cpp::vm::AssemblyShadowState;

ex::Header Header()
{
    ex::Header h{};
    h.stateCode=static_cast<int32_t>(AssemblyShadowState::FailedAfterCommit);
    h.state=AssemblyShadowDiagnostics::StateName(AssemblyShadowState::FailedAfterCommit);
    h.generation=12345;
    h.methodChecks=100;h.shadowMethodChecks=20;
    h.rejectedBaselineMethods=5;
    h.baselineClassCctorStarted=3;h.shadowClassCctorStarted=2;
    h.interpreterTransformations=50;h.shadowInterpreterTransformations=40;
    h.droppedClassObservations=0;
    return h;
}
struct Fixtures
{
    eh::Capture scalars{};
    bm::Capture metrics{};
    std::unique_ptr<bc::Capture> destination{new bc::Capture()};
    Fixtures()
    {
        const auto h=Header();
        assert(scalars.Begin(h,0,false));
        assert(scalars.Seal(h,0,false,true));
        assert(bm::TryCapture(metrics));
    }
};
std::string Original()
{
    std::ostringstream out;
    ex::Begin(out,Header());
    ex::EndClasses(out);
    r02::AppendDiagnostics(out);
    out << '}';
    return out.str();
}
void ByteCompleteClasslessParity()
{
    Fixtures f;
    assert(f.destination->Build(&f.scalars,&f.metrics));
    const std::string original=Original();
    assert(f.destination->Complete());
    assert(original.size()==f.destination->Bytes());
    assert(std::memcmp(original.data(),f.destination->Data(),original.size())==0);
    assert(original.find("\"classes\":[]")!=std::string::npos);
    assert(original.find("\"r02\":{\"schemaVersion\":1")!=std::string::npos);
    std::cout<<"GROUP:classless-original-execution-r02-byte-parity\n";
}
void NoHeapAfterPrewarm()
{
    Fixtures f;
    (void)std::locale();
    newCalls=0;watch=true;
    const bool good=f.destination->Build(&f.scalars,&f.metrics);
    const size_t cost=newCalls;
    watch=false;
    assert(good && cost==0 && f.destination->Complete());
    std::cout<<"GROUP:classless-composition-zero-new-allocations\n";
}
void MissingInputs()
{
    Fixtures f;
    assert(!f.destination->Build(nullptr,&f.metrics));
    assert(f.destination->FirstError()==bc::Error::MissingSource);
    assert(!f.destination->Complete() && f.destination->Data()==nullptr);
    std::cout<<"GROUP:missing-execution-header-rejected\n";
}
void MissingMetrics()
{
    Fixtures f;
    assert(!f.destination->Build(&f.scalars,nullptr));
    assert(f.destination->FirstError()==bc::Error::MissingSource);
    std::cout<<"GROUP:missing-r02-metrics-rejected\n";
}
void UnfinishedHeader()
{
    Fixtures f;
    eh::Capture notSealed;
    assert(!f.destination->Build(&notSealed,&f.metrics));
    assert(f.destination->FirstError()==bc::Error::IncompleteSource);
    assert(!f.destination->Complete());
    std::cout<<"GROUP:unsealed-header-rejected\n";
}
void UnfinishedMetrics()
{
    Fixtures f;
    bm::Capture notSealed;
    assert(!f.destination->Build(&f.scalars,&notSealed));
    assert(f.destination->FirstError()==bc::Error::IncompleteSource);
    std::cout<<"GROUP:unsealed-r02-rejected\n";
}
void OversizedOrZeroLimit()
{
    for(int which=0;which<2;++which)
    {
        Fixtures f;
        const size_t bound=which ? bc::Capture::kClasslessMaxBytes+1 : 0;
        assert(!f.destination->Build(&f.scalars,&f.metrics,bound));
        assert(f.destination->FirstError()==bc::Error::InvalidLimit);
        assert(!f.destination->Complete());
    }
    std::cout<<"GROUP:invalid-classless-capacity-denied\n";
}
void ShortBoundFailsWithoutReadablePrefix()
{
    Fixtures f;
    const std::string expected=Original();
    assert(expected.size()>20);
    assert(!f.destination->Build(&f.scalars,&f.metrics,expected.size()-1));
    assert(f.destination->FirstError()==bc::Error::OutputLimit);
    assert(!f.destination->Complete() && f.destination->Bytes()==0);
    assert(f.destination->Data()==nullptr);
    std::cout<<"GROUP:short-capacity-fails-closed-no-prefix\n";
}
void ReentryInvalidatesOldResult()
{
    Fixtures f;
    assert(f.destination->Build(&f.scalars,&f.metrics));
    assert(f.destination->Complete());
    assert(!f.destination->Build(&f.scalars,&f.metrics));
    assert(f.destination->FirstError()==bc::Error::WrongPhase);
    assert(!f.destination->Complete() && f.destination->Bytes()==0);
    std::cout<<"GROUP:classless-once-only-reentry-refused\n";
}
void CountersOwnedAtCaptureTime()
{
    Fixtures f;
    const std::string expected=Original();
    r02::ObservationCounters::Add(r02::Metric::DefinitionSearches,1);
    assert(f.destination->Build(&f.scalars,&f.metrics));
    assert(f.destination->Bytes()==expected.size());
    assert(std::memcmp(expected.data(),f.destination->Data(),expected.size())==0);
    assert(Original()!=expected);
    // Demonstrates byte ownership; does NOT claim a source-wide epoch.
    std::cout<<"GROUP:owned-metric-values-not-live-aliases\n";
}
void MaximumResultBudgetBounded()
{
    Fixtures f;
    assert(sizeof(bc::Capture)<=bc::Capture::kMaximumObjectBytes);
    assert(bc::Capture::kClasslessMaxBytes==32768);
    assert(f.destination->Build(&f.scalars,&f.metrics));
    assert(f.destination->Bytes()<bc::Capture::kClasslessMaxBytes);
    std::cout<<"GROUP:classless-object-bound-not-full-execution\n";
}
void MissingCoherentEpochCannotBePromoted()
{
    Fixtures f;
    const auto frame=bm::Sample();
    assert(frame.diagnosticsLevel==2 && !frame.saturated);
    assert(f.destination->Build(&f.scalars,&f.metrics));
    // This intentionally emits classes:[]. There is NO class census proof,
    // no full native execution JSON or authenticated V2 transport.
    const std::string got(f.destination->Data(),f.destination->Bytes());
    assert(got.find("\"classes\":[]")!=std::string::npos);
    std::cout<<"GROUP:no-vm-epoch-or-class-census-acceptance\n";
}
}
int main()
{
    (void)std::locale();
    // Prewarm actual per-thread production counter storage.
    r02::ObservationCounters::Add(r02::Metric::DefinitionSearches,10);
    r02::ObservationCounters::Add(r02::Metric::AdmissionHits,4);
    ByteCompleteClasslessParity();
    NoHeapAfterPrewarm();
    MissingInputs();
    MissingMetrics();
    UnfinishedHeader();
    UnfinishedMetrics();
    OversizedOrZeroLimit();
    ShortBoundFailsWithoutReadablePrefix();
    ReentryInvalidatesOldResult();
    CountersOwnedAtCaptureTime();
    MaximumResultBudgetBounded();
    MissingCoherentEpochCannotBePromoted();
    std::cout<<"R03 P1 INACTIVE CLASSLESS EXECUTION COMPOSITE HOST PASS\n";
}
