#pragma once
// R03 P1: admitted native metadata readers versus Runtime::Shutdown teardown.
// A read ticket is valid only while the entire originating source operation
// runs; it is NOT authorization for Unity engine callbacks after terminal
// failure and does NOT establish a coherent multi-subsystem VM epoch.
//
// Runtime::Init opens the gate only after complete core initialization.
// Runtime::Shutdown closes/drains before Thread::AbortAllThreads and before
// MetadataCache::Clear / MetadataAllocCleanup. The gate is deliberately
// initially CLOSED and is reopenable only once all readers have drained.
#include <atomic>
#include <cstdint>
#include <thread>

namespace il2cpp { namespace vm { namespace assembly_shadow_metadata_lifetime {

class Gate final
{
    static constexpr uint32_t kClosed=uint32_t(1)<<31;
    static constexpr uint32_t kReaders=kClosed-1;
    std::atomic<uint32_t> state_{kClosed};
    bool TryEnter() noexcept
    {
        uint32_t observed=state_.load(std::memory_order_acquire);
        do
        {
            if((observed&kClosed)!=0 || (observed&kReaders)==kReaders)
                return false;
        } while(!state_.compare_exchange_weak(observed,observed+1,
                   std::memory_order_acq_rel,std::memory_order_acquire));
        return true;
    }
    void Leave() noexcept {state_.fetch_sub(1,std::memory_order_release);}
public:
    class Reader final
    {
        Gate* gate_;
    public:
        explicit Reader(Gate& gate) noexcept :gate_(gate.TryEnter()?&gate:nullptr) {}
        ~Reader() noexcept {if(gate_)gate_->Leave();}
        Reader(const Reader&)=delete;
        Reader& operator=(const Reader&)=delete;
        Reader(Reader&&)=delete;
        Reader& operator=(Reader&&)=delete;
        explicit operator bool() const noexcept {return gate_!=nullptr;}
    };

    Gate() noexcept=default;
    Gate(const Gate&)=delete;
    Gate& operator=(const Gate&)=delete;
    // Called only by Runtime::Init, after the complete initialization tail,
    // serialized under s_InitLock; return false on invalid re-open.
    bool OpenAfterInit() noexcept
    {
        uint32_t expected=kClosed;
        return state_.compare_exchange_strong(expected,0,
                  std::memory_order_acq_rel,std::memory_order_acquire);
    }
    // Called only by the final Runtime::Shutdown, serialized under
    // s_InitLock. No metadata teardown may start before this returns.
    // This can WAIT INDEFINITELY if a reader deadlocks; all reader code and
    // lock ordering must be reviewed before a Unity runtime qualification.
    void CloseAndDrain() noexcept
    {
        state_.fetch_or(kClosed,std::memory_order_acq_rel);
        while((state_.load(std::memory_order_acquire)&kReaders)!=0)
            std::this_thread::yield();
    }
    bool Closed() const noexcept
    {return (state_.load(std::memory_order_acquire)&kClosed)!=0;}
    uint32_t ReaderCount() const noexcept
    {return state_.load(std::memory_order_acquire)&kReaders;}
};

// One storage instance belongs to the native Runtime.cpp; all P1 adapters
// use this exact gate rather than a per-TU or thread-local imitation.
extern Gate g_MetadataLifetimeGate;

}}} // il2cpp::vm::assembly_shadow_metadata_lifetime
