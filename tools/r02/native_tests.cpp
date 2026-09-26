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
#include <vector>

using namespace il2cpp::vm::assembly_shadow_r02;
extern std::atomic<uint64_t> allocations;
extern "C" void R02OtherTuAdd();
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(#condition); } while (false)
struct Class { int identity; bool used; explicit Class(int value = 0) : identity(value), used(false) {} };
struct Guard
{
    static std::atomic<int> acquisitions;
    std::recursive_mutex* mutex;
    explicit Guard(std::recursive_mutex* value) : mutex(value) { ++acquisitions; mutex->lock(); }
    ~Guard() { mutex->unlock(); }
};
std::atomic<int> Guard::acquisitions{0};
using Cert = AllocationCertificate<Class>;
using Cache = AllocationProofCache<Class>;
static AdmissionKey Key(Class* type, uint64_t generation = 1, const void* context = nullptr, uint32_t domain = 1)
{ return {generation, type, context, domain}; }

static void Basic()
{
    R02OtherTuAdd();
    CHECK(ObservationCounters::Read(Metric::DefinitionHits) == (ObservationCounters::Level ? 7u : 0u));
    Class a{1}, b{2}, context{3};
    AdmissionCache<int, 1> collisions;
    bool inserted;
    CHECK(*collisions.Publish(Key(&a), 7, inserted) == 7 && inserted);
    CHECK(*collisions.Publish(Key(&a), 9, inserted) == 7 && !inserted);
    CHECK(collisions.Find(Key(&a, 2)) == nullptr);
    CHECK(collisions.Find(Key(&a, 1, &context)) == nullptr);
    CHECK(collisions.Find(Key(&a, 1, nullptr, 2)) == nullptr);
    CHECK(*collisions.Publish(Key(&b), 11, inserted) == 11 && inserted);
    CHECK(*collisions.Find(Key(&a)) == 7 && *collisions.Find(Key(&b)) == 11);
    AdmissionCache<Class*> absent;
    const auto* missing = absent.Publish(Key(&a), nullptr, inserted);
    CHECK(inserted && missing != nullptr && *missing == nullptr);
    CHECK(absent.Find(Key(&a)) != nullptr && *absent.Find(Key(&a)) == nullptr);
    CHECK(absent.EntryBytes() > sizeof(Class*) && absent.BucketBytes() > 0);

    Cache cache;
    std::recursive_mutex mutex;
    int builds = 0, validations = 0;
    auto build = [&](Cert& proof) { ++builds; proof.Observe(&a, &b, false); return &b; };
    auto validate = [&](const Cert* proof) {
        ++validations;
        if (proof && proof->dependencies[0].baseline->used) throw std::runtime_error("baseline used");
    };
    CHECK(cache.Resolve<Guard>(Key(&a), &mutex, build, validate) == &b);
    CHECK(builds == 1 && cache.Find(Key(&a)) != nullptr);
    const int lockCount = Guard::acquisitions.load();
    const uint64_t before = allocations.load();
    for (int i = 0; i < 10000; ++i)
        if (cache.Resolve<Guard>(Key(&a), &mutex, build, validate) != &b) std::abort();
    CHECK(allocations.load() == before);
    CHECK(Guard::acquisitions.load() == lockCount);
    CHECK(builds == 1 && validations == 20003);
    a.used = true;
    bool rejected = false;
    try { cache.Resolve<Guard>(Key(&a), &mutex, build, validate); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected && builds == 1);
    a.used = false;
    cache.Resolve<Guard>(Key(&a, 2), &mutex, build, validate);
    CHECK(builds == 2);
    cache.Resolve<Guard>(Key(&a, 2, &context), &mutex, build, validate);
    cache.Resolve<Guard>(Key(&a, 2, &context, 2), &mutex, build, validate);
    CHECK(builds == 4);
    CHECK(AllocationProofTrace<Class>::Current() == nullptr);
    std::ostringstream json;
    AppendDiagnostics(json);
    CHECK(json.str().find("memoryAccountingScope") != std::string::npos);
}

static void Failures()
{
    Class a{1}, b{2}; Cache cache; std::recursive_mutex mutex;
    auto validate = [](const Cert*) {};
    bool rejected = false;
    try { cache.Resolve<Guard>(Key(&a), &mutex, [](Cert&) -> Class* { throw std::runtime_error("proof"); }, validate); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected && cache.Find(Key(&a)) == nullptr);
    CHECK(AllocationProofTrace<Class>::Current() == nullptr);
    int builds = 0; bool ready = false;
    auto build = [&](Cert& proof) { ++builds; proof.complete = ready; return &b; };
    CHECK(cache.Resolve<Guard>(Key(&a), &mutex, build, validate) == &b);
    CHECK(cache.Resolve<Guard>(Key(&a), &mutex, build, validate) == &b);
    CHECK(builds == 2 && cache.Find(Key(&a)) == nullptr);
    ready = true;
    cache.Resolve<Guard>(Key(&a), &mutex, build, validate);
    cache.Resolve<Guard>(Key(&a), &mutex, build, validate);
    CHECK(builds == 3 && cache.Find(Key(&a)) != nullptr);
    bool poisoned = true;
    auto poisonedValidator = [&](const Cert*) { if (poisoned) throw std::runtime_error("poison"); };
    rejected = false;
    try { cache.Resolve<Guard>(Key(&a), &mutex, build, poisonedValidator); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected && builds == 3);
    rejected = false;
    try { cache.Resolve<Guard>(Key(&b), &mutex, [](Cert&) -> Class* { return nullptr; }, validate); }
    catch (const std::logic_error&) { rejected = true; }
    CHECK(rejected && cache.Find(Key(&b)) == nullptr);

    Cache recursive;
    rejected = false;
    try
    {
        recursive.Resolve<Guard>(Key(&a), &mutex, [&](Cert&) {
            return recursive.Resolve<Guard>(Key(&a), &mutex, build, validate);
        }, validate);
    }
    catch (const std::logic_error&) { rejected = true; }
    CHECK(rejected && recursive.Find(Key(&a)) == nullptr);
    CHECK(AllocationProofTrace<Class>::Current() == nullptr);
    recursive.Resolve<Guard>(Key(&a), &mutex, [&](Cert&) {
        CHECK(AllocationProofTrace<Class>::Current() != nullptr);
        recursive.Resolve<Guard>(Key(&b), &mutex, build, validate);
        CHECK(AllocationProofTrace<Class>::Current() != nullptr);
        return &b;
    }, validate);
    CHECK(recursive.Find(Key(&a)) != nullptr && recursive.Find(Key(&b)) != nullptr);
    CHECK(AllocationProofTrace<Class>::Current() == nullptr);
}

static void Concurrent()
{
    Class a{1}, b{2}; Cache cache; std::recursive_mutex mutex;
    std::atomic<int> builds{0}, errors{0};
    std::vector<std::thread> threads;
    std::atomic<bool> go{false};
    for (int t = 0; t < 16; ++t) threads.emplace_back([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int n = 0; n < 1000; ++n)
        {
            Class* result = cache.Resolve<Guard>(Key(&a), &mutex, [&](Cert& proof) {
                ++builds; proof.Observe(&a, &b, false); return &b;
            }, [&](const Cert* proof) {
                if (proof && (proof->target != &b || proof->dependencies.size() != 1)) ++errors;
            });
            if (result != &b) ++errors;
        }
    });
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    CHECK(builds == 1 && errors == 0);

    AdmissionCache<uint64_t, 1> collisions;
    Class ids[64];
    threads.clear(); go.store(false); std::atomic<int> writers{8};
    for (int t = 0; t < 8; ++t) threads.emplace_back([&, t] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = t; i < 64; i += 8) { bool inserted; collisions.Publish(Key(&ids[i]), uint64_t(i + 1), inserted); }
        --writers;
    });
    for (int t = 0; t < 4; ++t) threads.emplace_back([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        do { for (int i = 0; i < 64; ++i) {
            const uint64_t* found = collisions.Find(Key(&ids[i]));
            if (found && *found != uint64_t(i + 1)) ++errors;
        } } while (writers.load());
    });
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    CHECK(errors == 0);
    for (int i = 0; i < 64; ++i) CHECK(*collisions.Find(Key(&ids[i])) == uint64_t(i + 1));
    CHECK(ObservationCounters::DroppedThreads() == 0);
}

static void Counters()
{
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < 10000; ++i) {
            ObservationCounters::Add(Metric::MethodChecks, 1);
            ObservationCounters::Add(Metric::ShadowMethodChecks, 1, std::memory_order_release);
        }
        done.store(true, std::memory_order_release);
    });
    bool valid = true;
    do {
        const auto subset = ObservationCounters::Read(Metric::ShadowMethodChecks, std::memory_order_acquire);
        const auto total = ObservationCounters::Read(Metric::MethodChecks);
        valid &= subset <= total;
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    CHECK(valid);
    CHECK(ObservationCounters::Read(Metric::MethodChecks) == (ObservationCounters::Level ? 10000u : 0u));
    // One thread was used above. Exactly 127 remaining slots, then truncation.
    for (int i = 0; i < 140; ++i) {
        std::thread thread([] { ObservationCounters::Add(Metric::LayoutChecks, 1); }); thread.join();
    }
    CHECK(ObservationCounters::Read(Metric::LayoutChecks) == (ObservationCounters::Level ? 127u : 0u));
    CHECK(ObservationCounters::DroppedThreads() == (ObservationCounters::Level ? 13u : 0u));
    CHECK(std::strcmp(ObservationCounters::Coverage(), ObservationCounters::Level ? "Truncated" : "Disabled") == 0);
}

static void Saturation()
{
    ObservationCounters::Add(Metric::LayoutChecks, UINT64_MAX);
    ObservationCounters::Add(Metric::LayoutChecks, 1);
    CHECK(ObservationCounters::Read(Metric::LayoutChecks) == (ObservationCounters::Level ? UINT64_MAX : 0));
    CHECK(ObservationCounters::Saturated() == (ObservationCounters::Level != 0));
    CHECK(std::strcmp(ObservationCounters::Coverage(), ObservationCounters::Level ? "Saturated" : "Disabled") == 0);
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try
    {
        if (!std::strcmp(argv[1], "basic")) Basic();
        else if (!std::strcmp(argv[1], "failures")) Failures();
        else if (!std::strcmp(argv[1], "concurrent")) Concurrent();
        else if (!std::strcmp(argv[1], "counters")) Counters();
        else if (!std::strcmp(argv[1], "saturation")) Saturation();
        else return 2;
        std::cout << "{\"kind\":\"R02NativeUnit\",\"result\":\"Passed\",\"case\":\"" << argv[1]
            << "\",\"level\":" << ObservationCounters::Level << ",\"checks\":" << checks << "}\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
