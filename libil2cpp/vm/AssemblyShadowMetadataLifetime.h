#pragma once
// R03 P1: source-epoch-bound native metadata readers versus Runtime teardown.
// One packed CAS word contains a 32-bit admission epoch, a closed flag and a
// 31-bit reader count. A ticket for an old admitted session cannot match a
// later reopening. Actual pointer origin still needs a separate ownership proof.
//
// Existing P1 registries have process lifetime and no per-pointer epoch.
// Their default Reader admits ONLY the initial admitted session (epoch 1).
// A future caller may supply an independently recorded source epoch, but
// must never substitute the current gate epoch for missing source provenance.
// This is NOT an AssemblyShadow ActiveGeneration or a coherent VM snapshot.
#include <atomic>
#include <cstdint>
#include <thread>

namespace il2cpp { namespace vm { namespace assembly_shadow_metadata_lifetime {
namespace detail
{
    constexpr uint64_t kClosed=uint64_t(1)<<31;
    constexpr uint64_t kReaders=kClosed-1;
    constexpr unsigned kEpochShift=32;
    constexpr uint32_t kInitialEpoch=1;

    inline uint32_t Epoch(uint64_t word) noexcept
    {return static_cast<uint32_t>(word>>kEpochShift);}

    // Pure transition functions used by the actual CAS loops. Failure never
    // alters next; neither a saturated count nor an exhausted epoch wraps.
    inline bool NextReader(uint64_t word,uint32_t sourceEpoch,uint64_t& next) noexcept
    {
        if(!sourceEpoch || Epoch(word)!=sourceEpoch || (word&kClosed)!=0 ||
           (word&kReaders)==kReaders)return false;
        next=word+1;
        return true;
    }
    inline bool NextOpen(uint64_t word,uint64_t& next) noexcept
    {
        if((word&kClosed)==0 || (word&kReaders)!=0 || Epoch(word)==UINT32_MAX)
            return false;
        next=uint64_t(Epoch(word)+1)<<kEpochShift;
        return true;
    }
}

class Gate final
{
    std::atomic<uint64_t> state_{detail::kClosed};
    bool TryEnter(uint32_t sourceEpoch) noexcept
    {
        uint64_t observed=state_.load(std::memory_order_acquire);
        uint64_t desired=0;
        do
        {
            if(!detail::NextReader(observed,sourceEpoch,desired))return false;
        } while(!state_.compare_exchange_weak(observed,desired,
                   std::memory_order_acq_rel,std::memory_order_acquire));
        return true;
    }
    void Leave() noexcept {state_.fetch_sub(1,std::memory_order_release);}
public:
    class Reader final
    {
        Gate* gate_;
        uint32_t epoch_;
    public:
        // Default is deliberately initial-runtime-only: the current bounded
        // class/assembly sources retain identities without a later epoch stamp.
        explicit Reader(Gate& gate,uint32_t sourceEpoch=detail::kInitialEpoch) noexcept
            :gate_(gate.TryEnter(sourceEpoch)?&gate:nullptr),epoch_(gate_?sourceEpoch:0) {}
        ~Reader() noexcept {if(gate_)gate_->Leave();}
        Reader(const Reader&)=delete;
        Reader& operator=(const Reader&)=delete;
        Reader(Reader&&)=delete;
        Reader& operator=(Reader&&)=delete;
        explicit operator bool() const noexcept {return gate_!=nullptr;}
        uint32_t Epoch() const noexcept {return epoch_;} // 0 means not admitted.
    };

    Gate() noexcept=default;
    Gate(const Gate&)=delete;
    Gate& operator=(const Gate&)=delete;
    // Runtime::Init/Shutdown remain serialized under s_InitLock. Publish the
    // next epoch AND open admission in one CAS, only after complete Init and
    // only after every previous reader drained. UINT32_MAX stays closed.
    bool OpenAfterInit() noexcept
    {
        uint64_t expected=state_.load(std::memory_order_acquire);
        uint64_t desired=0;
        return detail::NextOpen(expected,desired) &&
            state_.compare_exchange_strong(expected,desired,
                std::memory_order_acq_rel,std::memory_order_acquire);
    }
    // Final Runtime::Shutdown closes/drains before thread abort and metadata
    // cleanup. No bound on this wait or freedom from VM deadlocks is claimed.
    void CloseAndDrain() noexcept
    {
        state_.fetch_or(detail::kClosed,std::memory_order_acq_rel);
        while((state_.load(std::memory_order_acquire)&detail::kReaders)!=0)
            std::this_thread::yield();
    }
    bool Closed() const noexcept
    {return (state_.load(std::memory_order_acquire)&detail::kClosed)!=0;}
    uint32_t ReaderCount() const noexcept
    {return static_cast<uint32_t>(state_.load(std::memory_order_acquire)&detail::kReaders);}
};

// Exactly one native Runtime.cpp definition; no per-TU substitute.
extern Gate g_MetadataLifetimeGate;

}}} // il2cpp::vm::assembly_shadow_metadata_lifetime
