#pragma once

#include <atomic>
#include <mutex>
#include <stdint.h>

namespace il2cpp { namespace vm { namespace assembly_shadow_startup {

enum class Failure
{
    None, Configuration, CoreInitialization, CoreNotReady, WrongThread,
    PendingEntry, Assembly, Type, Method, NullResult, WrongResultType,
    CallbackRefused, CallbackThrew
};

enum class GateState { Idle, Running, Passed, Failed };

// Process-lifetime, once-only startup. Concurrent/reentrant initialization while
// activation is pending is unsupported: reject and poison, never wait on a
// callback that might itself be waiting for the entering thread.
class Gate
{
public:
    void MarkCoreReady() noexcept { _coreReady.store(true, std::memory_order_release); }
    bool CoreReady() const noexcept { return _coreReady.load(std::memory_order_acquire); }
    GateState State() const noexcept { std::lock_guard<std::mutex> lock(_mutex); return _state; }
    Failure Reason() const noexcept { std::lock_guard<std::mutex> lock(_mutex); return _failure; }

    bool Fail(Failure reason) noexcept
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return FailLocked(reason);
    }

    template<class Callback>
    bool Run(bool configured, bool mainThread, Callback callback) noexcept
    {
        if (!configured) return true;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_state == GateState::Passed) return true;
            if (_state == GateState::Failed) return false;
            if (!CoreReady()) return FailLocked(Failure::CoreNotReady);
            if (_state == GateState::Running) return FailLocked(Failure::PendingEntry);
            if (!mainThread) return FailLocked(Failure::WrongThread);
            _state = GateState::Running;
        }
        // The short gate lock never spans resolution or managed execution.
        try
        {
            if (!callback()) return Fail(Failure::CallbackRefused);
        }
        catch (...) { return Fail(Failure::CallbackThrew); }
        // A recursive/concurrent failure must survive the owner's completion.
        std::lock_guard<std::mutex> lock(_mutex);
        if (_state != GateState::Running) return false;
        _state = GateState::Passed;
        return true;
    }

private:
    std::atomic<bool> _coreReady { false };
    mutable std::mutex _mutex;
    GateState _state = GateState::Idle;
    Failure _failure = Failure::None;

    bool FailLocked(Failure reason) noexcept
    {
        if (_failure == Failure::None) _failure = reason;
        _state = GateState::Failed;
        return false;
    }
};

}}}
