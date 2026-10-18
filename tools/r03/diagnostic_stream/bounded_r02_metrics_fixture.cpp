// R03 P1: real R02 counters + original source serializer, no VM test doubles.
// No Unity Editor/Player, GC, native shutdown or coherent global epoch.
#include "AssemblyShadowBoundedR02Metrics.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
extern bool watch;
extern size_t newCalls;
namespace {
namespace r02=il2cpp::vm::assembly_shadow_r02;
namespace owned=il2cpp::vm::assembly_shadow_bounded_r02;
namespace diagnostic=il2cpp::vm::assembly_shadow_diagnostic;
using r02::Metric;
using r02::ObservationCounters;
void Prewarm()
{
    assert(ObservationCounters::Level==2);
    (void)ObservationCounters::Read(Metric::AdmissionHits);
    (void)ObservationCounters::RetainedBytes();
    (void)std::locale();
}
void SeedAllFields()
{
    for(size_t i=0;i<owned::kFields.size();++i)
        ObservationCounters::Add(owned::kFields[i].metric,
                                  static_cast<uint64_t>(i+10));
}
void FullOriginalR02ByteParity()
{
    owned::Capture capture;
    assert(owned::TryCapture(capture));
    assert(capture.Complete());
    owned::Frame frame{};
    assert(capture.Get(frame));
    assert(frame.diagnosticsLevel==2 && !frame.saturated);
    assert(frame.droppedClasses==0 && frame.droppedThreads==0);
    std::ostringstream original,newOutput;
    r02::AppendDiagnostics(original);
    assert(capture.Append(newOutput));
    assert(original.str()==newOutput.str());
    assert(newOutput.str().find("\"definitionSearches\":10")!=std::string::npos);
    assert(newOutput.str().find("\"observationMemoHits\":31")!=std::string::npos);
    assert(newOutput.str().find("\"classesCoverage\":\"BoundedComplete\"")!=std::string::npos);
    std::cout<<"GROUP:real-r02-original-byte-parity\n";
}
void FixedWriterByteParity()
{
    owned::Capture capture;
    assert(owned::TryCapture(capture));
    std::ostringstream original;
    r02::AppendDiagnostics(original);
    char storage[4096]{};
    struct Context { char* data;size_t size,used; } ctx{storage,sizeof(storage),0};
    const diagnostic::Sink sink{&ctx,[](void* ptr,const char* data,size_t n) noexcept {
        auto* c=static_cast<Context*>(ptr);
        if(n>c->size-c->used)return false;
        std::memcpy(c->data+c->used,data,n);c->used+=n;return true;
    }};
    diagnostic::Writer writer(sink,sizeof(storage));
    assert(capture.Append(writer));
    const auto result=writer.Finish();
    assert(result.complete && result.error==diagnostic::WriteError::None);
    assert(result.bytesAccepted==original.str().size());
    assert(std::memcmp(storage,original.str().data(),result.bytesAccepted)==0);
    std::cout<<"GROUP:actual-fixed-writer-byte-parity\n";
}
void NoNewDuringRealCounterCapture()
{
    owned::Capture capture;
    newCalls=0;watch=true;
    const bool ok=owned::TryCapture(capture);
    const size_t allocated=newCalls;
    watch=false;
    assert(ok && capture.Complete() && allocated==0);
    std::cout<<"GROUP:zero-cpp-new-after-counter-prewarm\n";
}
void MissingCoverageAndDisabledModesRejected()
{
    const auto source=owned::Sample();
    owned::Frame v=source;
    v.diagnosticsLevel=0;
    owned::Capture disabled;
    assert(!disabled.Begin(v));
    assert(disabled.FirstError()==owned::Error::CoverageUnavailable);
    v=source;v.diagnosticsLevel=1;
    owned::Capture partial;
    assert(!partial.Begin(v));
    assert(partial.FirstError()==owned::Error::CoverageUnavailable);
    v=source;v.threadCapacity=0;
    owned::Capture wrong;
    assert(!wrong.Begin(v));
    assert(wrong.FirstError()==owned::Error::CoverageUnavailable);
    std::cout<<"GROUP:disabled-or-incomplete-coverage-rejected\n";
}
void LostThreadsAndClassesRejected()
{
    auto v=owned::Sample();
    v.droppedThreads=1;
    owned::Capture droppedThreads;
    assert(!droppedThreads.Begin(v));
    assert(droppedThreads.FirstError()==owned::Error::CoverageUnavailable);
    v=owned::Sample();v.droppedClasses=1;
    owned::Capture droppedClasses;
    assert(!droppedClasses.Begin(v));
    assert(droppedClasses.FirstError()==owned::Error::CoverageUnavailable);
    std::cout<<"GROUP:dropped-thread-and-class-coverage-rejected\n";
}
void SaturationRefusedAtEitherBoundary()
{
    auto sample=owned::Sample();
    sample.saturated=true;
    owned::Capture early;
    assert(!early.Begin(sample));
    assert(early.FirstError()==owned::Error::CoverageUnavailable);
    auto stable=owned::Sample();
    owned::Capture late;
    assert(late.Begin(stable));
    stable.saturated=true;
    assert(!late.Seal(stable));
    assert(late.FirstError()==owned::Error::CoverageUnavailable);
    std::cout<<"GROUP:saturation-fails-closed\n";
}
void EveryCounterFieldChangeRefused()
{
    const auto stable=owned::Sample();
    for(size_t i=0;i<owned::kFields.size();++i)
    {
        owned::Capture changed;
        assert(changed.Begin(stable));
        auto after=stable;
        ++after.values[i];
        assert(!changed.Seal(after));
        assert(changed.FirstError()==owned::Error::SourceChanged);
        assert(!changed.Complete());
    }
    std::cout<<"GROUP:all-twenty-two-field-changes-rejected\n";
}
void AuxiliaryMetricChangeRefused()
{
    const auto stable=owned::Sample();
    for(int i=0;i<2;++i)
    {
        owned::Capture changed;
        assert(changed.Begin(stable));
        auto after=stable;
        if(i==0)++after.storageBytes;
        else ++after.memoTlsBytes;
        assert(!changed.Seal(after));
        assert(changed.FirstError()==owned::Error::SourceChanged);
    }
    std::cout<<"GROUP:accounting-metadata-change-rejected\n";
}
void CoverageLostAfterBeginRefused()
{
    auto stable=owned::Sample();
    owned::Capture threads;
    assert(threads.Begin(stable));
    auto later=stable;later.droppedThreads=1;
    assert(!threads.Seal(later));
    assert(threads.FirstError()==owned::Error::CoverageUnavailable);
    owned::Capture classes;
    assert(classes.Begin(stable));
    later=stable;later.droppedClasses=1;
    assert(!classes.Seal(later));
    assert(classes.FirstError()==owned::Error::CoverageUnavailable);
    std::cout<<"GROUP:late-coverage-loss-rejected\n";
}
void PartialOutputNeverEscapes()
{
    owned::Capture pending;
    std::ostringstream sink;
    assert(!pending.Append(sink));
    assert(sink.str().empty());
    auto stable=owned::Sample();
    assert(pending.Begin(stable));
    assert(!pending.Append(sink));
    assert(sink.str().empty());
    assert(pending.Seal(stable));
    assert(pending.Append(sink));
    assert(!sink.str().empty());
    std::cout<<"GROUP:partial-output-not-released\n";
}
void PermanentSingleOwnerFailure()
{
    owned::Capture one;
    assert(owned::TryCapture(one));
    assert(one.Complete());
    assert(!owned::TryCapture(one));
    assert(one.FirstError()==owned::Error::WrongPhase);
    assert(!one.Complete());
    std::ostringstream output;
    assert(!one.Append(output));
    assert(output.str().empty());
    std::cout<<"GROUP:one-shot-reentry-invalidates-old-success\n";
}
void NoGlobalEpochOrMetadataClaim()
{
    owned::Capture capture;
    assert(owned::TryCapture(capture));
    owned::Frame frame{};
    assert(capture.Get(frame));
    assert(capture.Complete());
    assert(frame.threadCapacity==ObservationCounters::kThreadCapacity);
    assert(sizeof(owned::Capture)<=owned::Capture::kMaximumObjectBytes);
    std::cout<<"GROUP:bounded-r02-snapshot-not-vm-epoch\n";
}
}

int main()
{
    Prewarm();
    SeedAllFields();
    FullOriginalR02ByteParity();
    FixedWriterByteParity();
    NoNewDuringRealCounterCapture();
    MissingCoverageAndDisabledModesRejected();
    LostThreadsAndClassesRejected();
    SaturationRefusedAtEitherBoundary();
    EveryCounterFieldChangeRefused();
    AuxiliaryMetricChangeRefused();
    CoverageLostAfterBeginRefused();
    PartialOutputNeverEscapes();
    PermanentSingleOwnerFailure();
    NoGlobalEpochOrMetadataClaim();
    std::cout<<"R03 P1 BOUNDED REAL R02 METRICS HOST PASS\n";
}
