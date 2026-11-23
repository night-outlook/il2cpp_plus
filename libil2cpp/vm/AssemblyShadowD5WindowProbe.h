#pragma once
// D5-B C10 TEST-ONLY phase window witness; only included when
// HYBRIDCLR_R03_D5_WINDOW_PROBE=1 in an isolated IL2CPP validation Player.
// NO Unity/VM callbacks, heap allocation, log output, locks or exceptions.
// This observable widened window is NOT a production scheduling/Player pass.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace il2cpp { namespace vm { namespace assembly_shadow_d5 {

enum class WindowPhase : int32_t
{
    Idle = 0, Armed = 1, AtP5 = 2, AtP6 = 3, Complete = 4
};

struct WindowSnapshot
{
    int32_t schemaVersion;
    int32_t phase;
    int32_t armed;
    int32_t p5Count;
    int32_t p6Count;
    int32_t releaseCount;
    int32_t timedOut;
    // C10 counters are scoped by AssemblyShadow.cpp to exact Methods/R03.Node.Keep.
    // Constructors, unrelated methods and synthetic host booleans are not Player proof.
    int32_t shadowMethodAttemptsInP5;
    int32_t shadowMethodAllowedInP5;
    int32_t shadowMethodDeniedInP5;
    int32_t methodResultRacedP6;
};

class WindowProbe final
{
public:
    WindowProbe() noexcept = default;
    WindowProbe(const WindowProbe&) = delete;
    WindowProbe& operator=(const WindowProbe&) = delete;

    // The singleton is constructed before the first P5 by the pre-commit Arm
    // test call. A C++11 inline function-local static is shared across TUs.
    static WindowProbe& Instance() noexcept
    {
        static WindowProbe witness;
        return witness;
    }

    bool Arm(int32_t maximumWaitMs) noexcept
    {
        if (maximumWaitMs < 20 || maximumWaitMs > 2000)
            return false;
        int32_t expected = static_cast<int32_t>(WindowPhase::Idle);
        if (!phase_.compare_exchange_strong(expected,
                static_cast<int32_t>(WindowPhase::Armed),
                std::memory_order_acq_rel))
            return false;
        maximumWaitMs_.store(maximumWaitMs, std::memory_order_release);
        armed_.store(1, std::memory_order_release);
        return true; // one-shot, cannot reset/rearm in this process
    }

    void AtP5() noexcept
    {
        int32_t expected = static_cast<int32_t>(WindowPhase::Armed);
        if (phase_.compare_exchange_strong(expected,
                static_cast<int32_t>(WindowPhase::AtP5),
                std::memory_order_acq_rel))
            p5Count_.fetch_add(1, std::memory_order_relaxed);
    }

    // Called AFTER metadata, assembly, usage and transaction locks are
    // released in the test-only path. Every wait is bounded, even if the test
    // worker fails to signal; no managed callback or VM work occurs here.
    void HoldP5Bounded() noexcept
    {
        if (phase_.load(std::memory_order_acquire) !=
                static_cast<int32_t>(WindowPhase::AtP5))
            return;
        const auto start = std::chrono::steady_clock::now();
        while (!release_.load(std::memory_order_acquire))
        {
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
            if (elapsed >= maximumWaitMs_.load(std::memory_order_acquire))
            {
                timedOut_.store(1, std::memory_order_release);
                break;
            }
            std::this_thread::yield();
        }
    }

    bool ReleaseP5() noexcept
    {
        if (phase_.load(std::memory_order_acquire) !=
                static_cast<int32_t>(WindowPhase::AtP5))
            return false;
        int32_t expected = 0;
        if (!release_.compare_exchange_strong(expected, 1,
                std::memory_order_acq_rel))
            return false;
        releaseCount_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void AtP6() noexcept
    {
        int32_t expected = static_cast<int32_t>(WindowPhase::AtP5);
        if (phase_.compare_exchange_strong(expected,
                static_cast<int32_t>(WindowPhase::AtP6),
                std::memory_order_acq_rel))
            p6Count_.fetch_add(1, std::memory_order_relaxed);
    }

    void Complete() noexcept
    {
        int32_t expected = static_cast<int32_t>(WindowPhase::AtP6);
        phase_.compare_exchange_strong(expected,
            static_cast<int32_t>(WindowPhase::Complete),
            std::memory_order_acq_rel);
    }

    int32_t MethodBegin() const noexcept
    {
        return phase_.load(std::memory_order_acquire);
    }

    // In a real Player the caller passes target=true ONLY for the exact
    // active-shadow Methods/R03.Node.Keep MethodInfo. Host tests use synthetic
    // booleans to exercise accounting, never as real method-entry evidence.
    void MethodResult(int32_t begunPhase, bool shadow, bool allowed) noexcept
    {
        if (!shadow ||
            begunPhase != static_cast<int32_t>(WindowPhase::AtP5))
            return;
        if (phase_.load(std::memory_order_acquire) !=
                static_cast<int32_t>(WindowPhase::AtP5))
        {
            methodResultRacedP6_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        shadowMethodAttemptsInP5_.fetch_add(1, std::memory_order_relaxed);
        (allowed ? shadowMethodAllowedInP5_ :
                   shadowMethodDeniedInP5_).fetch_add(1,
                       std::memory_order_relaxed);
    }

    WindowSnapshot Read() const noexcept
    {
        WindowSnapshot out{};
        out.schemaVersion = 1;
        out.phase = phase_.load(std::memory_order_acquire);
        out.armed = armed_.load(std::memory_order_acquire);
        out.p5Count = p5Count_.load(std::memory_order_relaxed);
        out.p6Count = p6Count_.load(std::memory_order_relaxed);
        out.releaseCount = releaseCount_.load(std::memory_order_relaxed);
        out.timedOut = timedOut_.load(std::memory_order_acquire);
        out.shadowMethodAttemptsInP5 =
            shadowMethodAttemptsInP5_.load(std::memory_order_relaxed);
        out.shadowMethodAllowedInP5 =
            shadowMethodAllowedInP5_.load(std::memory_order_relaxed);
        out.shadowMethodDeniedInP5 =
            shadowMethodDeniedInP5_.load(std::memory_order_relaxed);
        out.methodResultRacedP6 =
            methodResultRacedP6_.load(std::memory_order_relaxed);
        return out;
    }

private:
    std::atomic<int32_t> phase_{0};
    std::atomic<int32_t> maximumWaitMs_{0};
    std::atomic<int32_t> armed_{0};
    std::atomic<int32_t> release_{0};
    std::atomic<int32_t> p5Count_{0};
    std::atomic<int32_t> p6Count_{0};
    std::atomic<int32_t> releaseCount_{0};
    std::atomic<int32_t> timedOut_{0};
    std::atomic<int32_t> shadowMethodAttemptsInP5_{0};
    std::atomic<int32_t> shadowMethodAllowedInP5_{0};
    std::atomic<int32_t> shadowMethodDeniedInP5_{0};
    std::atomic<int32_t> methodResultRacedP6_{0};
};

}}} // namespace il2cpp::vm::assembly_shadow_d5
