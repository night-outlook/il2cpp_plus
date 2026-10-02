#include "vm/AssemblyShadowAllocationProof.h"
#include "vm/AssemblyShadowRuntimeProbe.h"
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
using namespace il2cpp::vm::assembly_shadow_r02;
using il2cpp::vm::assembly_shadow_r03::RuntimeProbe;
static int checks = 0;
static void Need(bool value) { ++checks; if (!value) std::abort(); }
struct Guard { std::lock_guard<std::recursive_mutex> lock; explicit Guard(std::recursive_mutex* m) : lock(*m) {} };
extern bool OtherTranslationUnitBegin(const void*, uint64_t);
int main(int argc, char** argv)
{
    Need(argc == 2); const std::string mode = argv[1];
#if HYBRIDCLR_R03_RUNTIME_PROBE
    int target = 1, unrelated = 2; std::recursive_mutex mutex;
    AllocationProofCache<int> cache;
    auto resolve = [&](int* klass) {
        RuntimeProbe::SiteScope site("RuntimeProbeUnit:Allocation");
        return cache.Resolve<Guard>(AdmissionKey{1, klass, nullptr, 1}, &mutex,
            [&](AllocationCertificate<int>& c) { c.complete = true; return klass; },
            [&](const AllocationCertificate<int>* c) { if (c) ObservationCounters::Add(Metric::BaselineChecks, 1); });
    };
    Need(resolve(&target) == &target);
    Need(OtherTranslationUnitBegin(&target, 1));
    Need(!RuntimeProbe::Begin(&target, 1));
    if (mode == "wrong-owner")
    {
        std::thread t([] { RuntimeProbe::Mark(1); }); t.join();
        Need(RuntimeProbe::Read().invalid);
    }
    else if (mode == "unsealed")
    { Need(RuntimeProbe::Read().invalid); Need(!RuntimeProbe::Read().sealed); }
    else
    {
        RuntimeProbe::ObserverSnapshot(); Need(RuntimeProbe::Mark(1));
        for (int i = 0; i < 10000; ++i) Need(resolve(&target) == &target);
        if (mode == "loop-cold") Need(resolve(&unrelated) == &unrelated);
        Need(RuntimeProbe::Mark(2));
        if (mode == "window") Need(resolve(&unrelated) == &unrelated);
        if (mode == "thread") { std::thread t([&] { resolve(&unrelated); }); t.join(); }
        if (mode == "overflow")
            for (size_t i = 0; i < 129; ++i) RuntimeProbe::Count(Metric::AdmissionMisses, 1, AdmissionKey{1, &unrelated, nullptr, 1});
        RuntimeProbe::ObserverSnapshot(); Need(RuntimeProbe::Mark(3));
        const auto r = RuntimeProbe::Read(); Need(r.used && r.sealed && !r.invalid);
        Need(r.samples == 6); Need(r.sample[2].label == 1 && r.sample[3].label == 2);
        const size_t hits = static_cast<size_t>(Metric::AdmissionHits), misses = static_cast<size_t>(Metric::AdmissionMisses);
        Need(r.sample[3].values[hits] - r.sample[2].values[hits] == 10000);
        Need(r.sample[3].values[misses] - r.sample[2].values[misses] == (mode == "loop-cold" ? 1u : 0u));
        if (mode == "overflow") { Need(r.overflow && r.events == 128); }
        else
        {
            Need(!r.overflow); Need(r.events == 4);
            for (size_t i = 0; i < r.events; ++i)
            {
                Need(r.event[i].key.physical == &unrelated);
                Need(r.event[i].phase == (mode == "loop-cold" ? 2u : 3u));
                Need(std::string(r.event[i].site) == "RuntimeProbeUnit:Allocation");
                Need((r.event[i].thread != r.owner) == (mode == "thread"));
            }
        }
        const size_t retained = RuntimeProbe::Read().events;
        RuntimeProbe::Count(Metric::AdmissionMisses, 1, AdmissionKey{1, &unrelated, nullptr, 1});
        Need(RuntimeProbe::Read().events == retained); // no late writes after seal
    }
#else
    Need(mode == "disabled");
    RuntimeProbe::Count(Metric::AdmissionMisses, 1, AdmissionKey{1, nullptr, nullptr, 1});
    Need(ObservationCounters::Read(Metric::AdmissionMisses) == (ObservationCounters::Level ? 1u : 0u));
#endif
    std::cout << "{\"kind\":\"R03RuntimeProbeHeaderTest\",\"result\":\"Passed\",\"mode\":\"" << mode
        << "\",\"checks\":" << checks << ",\"runtimeAcceptance\":false}\n";
}
