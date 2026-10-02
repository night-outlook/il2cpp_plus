#pragma once
// R03 diagnostic-only producer control. Never used by ordinary Players.
// The VM adapter admits only the pinned BCL ArrayPool Gen2 callback here.
// A lease does not suspend GC, change counters, warm types, or discard work.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace il2cpp { namespace vm { namespace assembly_shadow_r03 {
class ProducerFence
{
    using Clock = std::chrono::steady_clock;
public:
    static constexpr uint32_t kMaximumLeaseMs = 5000;
    struct Report
    {
        bool requested = false, acquired = false, released = false;
        bool expired = false, invalid = false, drained = false;
        uint64_t admitted = 0, completed = 0, deferred = 0, deferredCompleted = 0;
        uint64_t ownerOsThread = 0, elapsedMicros = 0;
        uint32_t leaseMs = 0;
    };
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    Report report_;
    bool closed_ = false;
    uint32_t running_ = 0;
    std::thread::id owner_;
    Clock::time_point deadline_, started_;
    void Expire()
    {
        if (closed_ && Clock::now() >= deadline_)
        { report_.expired = true; closed_ = false; changed_.notify_all(); }
    }
    bool Enter()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        Expire();
        const bool deferred = closed_;
        if (deferred)
        {
            ++report_.deferred;
            // Never strand the runtime finalizer thread on a crashed/abandoned
            // witness. Expiration reopens the gate but INVALIDATES the proof.
            while (closed_)
            { changed_.wait_until(lock, deadline_); Expire(); }
        }
        ++running_; ++report_.admitted; return deferred;
    }
    void Leave(bool deferred)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) report_.invalid = true;
        else { --running_; ++report_.completed; if (deferred) ++report_.deferredCompleted; }
        changed_.notify_all();
    }
public:
    class Callback
    {
        ProducerFence* fence_;
        bool deferred_ = false;
    public:
        Callback(ProducerFence& fence, bool identified) : fence_(identified ? &fence : nullptr)
        { if (fence_) deferred_ = fence_->Enter(); }
        ~Callback() { if (fence_) fence_->Leave(deferred_); }
        Callback(const Callback&) = delete;
        Callback& operator=(const Callback&) = delete;
    };
    // Call outside metadata/probe locks. Wait for existing matching callbacks
    // to FINISH, never initialize them or trigger a collection to obtain proof.
    bool Acquire(uint64_t osThread, uint32_t leaseMs = kMaximumLeaseMs)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (report_.requested || !osThread || !leaseMs || leaseMs > kMaximumLeaseMs)
        { report_.invalid = true; return false; }
        report_.requested = true; report_.ownerOsThread = osThread;
        report_.leaseMs = leaseMs; owner_ = std::this_thread::get_id();
        started_ = Clock::now(); deadline_ = started_ + std::chrono::milliseconds(leaseMs);
        closed_ = true;
        while (running_ && closed_)
        { changed_.wait_until(lock, deadline_); Expire(); }
        Expire();
        if (!closed_) return false;
        report_.acquired = true; return true;
    }
    bool Release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!report_.requested) return true;
        if (owner_ != std::this_thread::get_id())
        { report_.invalid = true; return false; }
        Expire();
        if (!report_.released)
        {
            report_.elapsedMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_).count());
            closed_ = false; report_.released = true; changed_.notify_all();
        }
        return report_.acquired && !report_.expired && !report_.invalid;
    }
    bool Drain()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!report_.requested) return true;
        if (!report_.released || owner_ != std::this_thread::get_id())
        { report_.invalid = true; return false; }
        const auto deadline = Clock::now() + std::chrono::milliseconds(kMaximumLeaseMs);
        if (!changed_.wait_until(lock, deadline, [&] { return report_.deferredCompleted == report_.deferred; }))
        { report_.invalid = true; return false; }
        report_.drained = true; return !report_.expired && !report_.invalid;
    }
    Report Read()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Expire(); return report_;
    }
    static ProducerFence& Instance() { static ProducerFence instance; return instance; }
};
}}}
