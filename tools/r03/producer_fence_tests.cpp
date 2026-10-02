#include "vm/AssemblyShadowProducerFence.h"
#include "vm/AssemblyShadowRuntimeProbe.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace il2cpp::vm::assembly_shadow_r03;
static int checks = 0;
static void Need(bool yes) { ++checks; if (!yes) std::abort(); }
static void Await(const std::atomic<bool>& flag)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!flag.load() && std::chrono::steady_clock::now() < end) std::this_thread::yield();
    Need(flag.load());
}
int main(int argc, char** argv)
{
    Need(argc == 2); const std::string mode = argv[1];
    ProducerFence fence;
    if (mode == "normal")
    {
        { ProducerFence::Callback callback(fence, true); }
        auto r = fence.Read(); Need(!r.requested && r.admitted == 1 && r.completed == 1);
    }
    else if (mode == "defer")
    {
        Need(fence.Acquire(12)); std::atomic<bool> entered{false}, finished{false};
        std::thread callback([&] { entered = true; ProducerFence::Callback x(fence, true); finished = true; });
        Await(entered);
        while (!fence.Read().deferred) std::this_thread::yield();
        Need(!finished); Need(fence.Read().admitted == 0);
        Need(fence.Release()); Need(fence.Drain()); callback.join();
        auto r = fence.Read(); Need(finished && r.deferred == 1 && r.deferredCompleted == 1 && r.drained && !r.expired);
    }
    else if (mode == "unrelated")
    {
        Need(fence.Acquire(13)); std::atomic<bool> finished{false};
        std::thread callback([&] { ProducerFence::Callback x(fence, false); finished = true; });
        Await(finished); callback.join(); Need(fence.Read().deferred == 0); Need(fence.Release()); Need(fence.Drain());
    }
    else if (mode == "existing")
    {
        std::atomic<bool> entered{false}, leave{false};
        std::thread callback([&] { ProducerFence::Callback x(fence, true); entered = true; while (!leave.load()) std::this_thread::yield(); });
        Await(entered);
        std::thread releaser([&] { while (!fence.Read().requested) std::this_thread::yield(); leave = true; });
        Need(fence.Acquire(14)); Need(fence.Read().completed == 1); Need(fence.Release()); Need(fence.Drain());
        releaser.join(); callback.join();
    }
    else if (mode == "expire")
    {
        Need(fence.Acquire(15, 20)); std::atomic<bool> finished{false};
        std::thread callback([&] { ProducerFence::Callback x(fence, true); finished = true; });
        Await(finished); callback.join(); Need(fence.Read().expired); Need(!fence.Release()); Need(!fence.Drain());
    }
    else if (mode == "abandoned")
    {
        Need(fence.Acquire(16, 1)); std::this_thread::sleep_for(std::chrono::milliseconds(3));
        Need(!fence.Release()); Need(fence.Read().expired);
    }
    else if (mode == "wrong-owner")
    {
        Need(fence.Acquire(17)); std::thread wrong([&] { if (fence.Release()) std::abort(); }); wrong.join();
        Need(!fence.Release()); Need(fence.Read().invalid && fence.Read().released);
    }
    else if (mode == "reuse")
    { Need(fence.Acquire(18)); Need(fence.Release()); Need(fence.Drain()); Need(!fence.Acquire(18)); }
    else if (mode == "bounds")
    { Need(!fence.Acquire(0)); ProducerFence f; Need(!f.Acquire(1, 5001)); ProducerFence g; Need(!g.Acquire(1, 0)); }
    else if (mode == "exception")
    {
        try { ProducerFence::Callback callback(fence, true); throw std::runtime_error("expected"); } catch (...) {}
        Need(fence.Read().completed == 1); Need(fence.Acquire(19)); Need(fence.Release());
    }
#if HYBRIDCLR_R03_RUNTIME_PROBE
    else if (mode == "context")
    {
        int a = 1, b = 2;
        Need(!RuntimeProbe::CurrentProducer().callback);
        { RuntimeProbe::ProducerScope outer(&a, &b, 99, true);
          Need(RuntimeProbe::CurrentProducer().recognizedArrayPool);
          { RuntimeProbe::ProducerScope inner(nullptr, nullptr, 77, false); Need(!RuntimeProbe::CurrentProducer().callback); }
          Need(RuntimeProbe::CurrentProducer().callback == &b); }
        Need(!RuntimeProbe::CurrentProducer().callback);
    }
#endif
    else Need(false);
    std::cout << "{\"kind\":\"R03ProducerFenceHeaderTest\",\"result\":\"Passed\",\"checks\":" << checks
        << ",\"mode\":\"" << mode << "\",\"runtimeAcceptance\":false}\n";
}
