#include "vm/AssemblyShadowAllocationProof.h"
#include "vm/AssemblyShadowR02Diagnostics.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>

extern std::atomic<long> failNext;
extern std::atomic<unsigned long> allocationCount;
using namespace il2cpp::vm::assembly_shadow_r02;
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(#x); } while (0)
struct Class { bool used = false; };
using Cert = AllocationCertificate<Class>;
using Cache = AllocationProofCache<Class>;
struct Guard {
    std::recursive_mutex* p;
    explicit Guard(std::recursive_mutex* m) : p(m) { p->lock(); }
    ~Guard() { p->unlock(); }
};
static AdmissionKey Key(Class* p) { return {1, p, nullptr, 1}; }
static void Validate(const Cert* c)
{
    if (c) for (const auto& d : c->dependencies)
        if (d.baseline && d.baseline->used) throw std::runtime_error("used baseline");
}
static void Nested()
{
    Class outer, inner, baseline, active;
    std::recursive_mutex mutex; Cache cache;
    auto child = [&](Cert& c) { c.Observe(&baseline, &active, false); return &active; };
    auto parent = [&](Cert&) { return cache.Resolve<Guard>(Key(&inner), &mutex, child, Validate); };
    CHECK(cache.Resolve<Guard>(Key(&outer), &mutex, parent, Validate) == &active);
    CHECK(cache.Find(Key(&outer))->dependencies.size() == 1);
    CHECK(cache.Find(Key(&outer))->involvesShadow);
    baseline.used = true;
    bool rejected = false;
    try { cache.Resolve<Guard>(Key(&outer), &mutex, parent, Validate); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    baseline.used = false;
    Class secondOuter;
    cache.Resolve<Guard>(Key(&secondOuter), &mutex, parent, Validate);
    CHECK(cache.Find(Key(&secondOuter))->dependencies.size() == 1);
    CHECK(AllocationProofTrace<Class>::Current() == nullptr);
    const auto before = allocationCount.load();
    for (int i = 0; i < 10000; ++i) cache.Resolve<Guard>(Key(&outer), &mutex, parent, Validate);
    CHECK(before == allocationCount.load());
}
static void Unready()
{
    Class outer, inner, active; Cache cache; std::recursive_mutex mutex;
    bool ready = false;
    auto child = [&](Cert& c) { c.complete = ready; return &active; };
    auto parent = [&](Cert&) { return cache.Resolve<Guard>(Key(&inner), &mutex, child, Validate); };
    cache.Resolve<Guard>(Key(&outer), &mutex, parent, Validate);
    CHECK(!cache.Find(Key(&outer)) && !cache.Find(Key(&inner)));
    ready = true;
    cache.Resolve<Guard>(Key(&outer), &mutex, parent, Validate);
    CHECK(cache.Find(Key(&outer)) && cache.Find(Key(&inner)));
}
static void PublicationFailure()
{
    Class input, active; Cache cache; std::recursive_mutex mutex;
    bool failed = false;
    const auto before = ObservationCounters::Read(Metric::AdmissionRejects);
    // No vector allocation in this proof. The injected failure is precisely
    // the entry-node allocation after the builder/validator completed.
    auto build = [&](Cert&) { failNext.store(0); return &active; };
    try { cache.Resolve<Guard>(Key(&input), &mutex, build, Validate); }
    catch (const std::bad_alloc&) { failed = true; }
    CHECK(failed && cache.Find(Key(&input)) == nullptr);
    CHECK(ObservationCounters::Read(Metric::AdmissionRejects) == before + (ObservationCounters::Level ? 1u : 0u));
    CHECK(AllocationProofTrace<Class>::Current() == nullptr);
    CHECK(cache.Resolve<Guard>(Key(&input), &mutex, [&](Cert&) { return &active; }, Validate) == &active);
}
static void Coverage()
{
    for (int i = 0; i < 140; ++i) {
        std::thread thread([] { ObservationCounters::Add(Metric::DroppedClasses, 0); });
        thread.join();
    }
    std::ostringstream out; AppendDiagnostics(out);
    const std::string expected = ObservationCounters::Level < 2 ? "Disabled" : "Truncated";
    CHECK(out.str().find("\"classesCoverage\":\"" + expected + "\"") != std::string::npos);
    CHECK(out.str().find("\"memoryAccountingAvailable\":false") != std::string::npos);
    CHECK(ObservationCounters::Read(Metric::DroppedClasses) == 0);
    CHECK(ObservationCounters::DroppedThreads() == (ObservationCounters::Level ? 12u : 0u));
}
static void Memo()
{
    Class objects[1000];
    CHECK(!ObservationMemo::Contains(nullptr));
    ObservationMemo::Remember(nullptr);
    CHECK(!ObservationMemo::Contains(&objects[0]));
    ObservationMemo::Remember(&objects[0]);
    CHECK(ObservationMemo::Contains(&objects[0]) == (ObservationCounters::Level == 2));
    std::atomic<bool> isolated{false};
    std::thread other([&] { isolated = !ObservationMemo::Contains(&objects[0]); }); other.join();
    CHECK(isolated);
    for (int i = 1; i < 1000; ++i) {
        // A colliding key must never be reported present before registration.
        CHECK(!ObservationMemo::Contains(&objects[i]));
        ObservationMemo::Remember(&objects[i]);
        CHECK(ObservationMemo::Contains(&objects[i]) == (ObservationCounters::Level == 2));
    }
    const auto before = allocationCount.load();
    for (int n = 0; n < 10000; ++n) { ObservationMemo::Remember(&objects[1]); ObservationMemo::Contains(&objects[1]); }
    CHECK(before == allocationCount.load());
    CHECK(ObservationMemo::TlsBytesPerThread() == (ObservationCounters::Level == 2 ? 64 * sizeof(void*) : 0));
}
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return 2;
        if (!std::strcmp(argv[1], "nested")) Nested();
        else if (!std::strcmp(argv[1], "unready")) Unready();
        else if (!std::strcmp(argv[1], "publication-failure")) PublicationFailure();
        else if (!std::strcmp(argv[1], "coverage")) Coverage();
        else if (!std::strcmp(argv[1], "memo")) Memo();
        else return 2;
        std::cout << "{\"kind\":\"R02NativeRevisionUnit\",\"result\":\"Passed\",\"case\":\"" << argv[1]
          << "\",\"level\":" << ObservationCounters::Level << ",\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
