// Actual production gate header; native VM/GC/engine lifetime is NOT modeled.
#include "AssemblyShadowMetadataLifetime.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using il2cpp::vm::assembly_shadow_metadata_lifetime::Gate;
namespace word=il2cpp::vm::assembly_shadow_metadata_lifetime::detail;

static void InitialAndRepeatLifecycle()
{
    Gate g;
    assert(g.Closed() && g.ReaderCount()==0);
    {Gate::Reader r(g);assert(!r && r.Epoch()==0);}
    assert(g.OpenAfterInit());
    assert(!g.Closed() && !g.OpenAfterInit());
    {
        Gate::Reader r(g);assert(r && r.Epoch()==1 && g.ReaderCount()==1);
        {Gate::Reader nested(g);assert(nested && nested.Epoch()==1 && g.ReaderCount()==2);}
        {Gate::Reader invalid(g,0);assert(!invalid && invalid.Epoch()==0);}
        {Gate::Reader future(g,2);assert(!future && g.ReaderCount()==1);}
        assert(g.ReaderCount()==1);
    }
    assert(g.ReaderCount()==0);
    g.CloseAndDrain();
    assert(g.Closed());
    {Gate::Reader r(g);assert(!r);}
    g.CloseAndDrain();
    assert(g.OpenAfterInit());
    {Gate::Reader stale(g);assert(!stale && stale.Epoch()==0 && g.ReaderCount()==0);}
    {Gate::Reader fresh(g,2);assert(fresh && fresh.Epoch()==2);}
    g.CloseAndDrain();
    assert(g.OpenAfterInit());
    {Gate::Reader stale(g,2);assert(!stale);}
    {Gate::Reader fresh(g,3);assert(fresh && fresh.Epoch()==3);}
    g.CloseAndDrain();
}

static void BoundaryTransitions()
{
    const uint64_t one=uint64_t(1)<<word::kEpochShift;
    const uint64_t last=uint64_t(UINT32_MAX)<<word::kEpochShift;
    uint64_t next=91;
    assert(word::NextOpen(word::kClosed,next) && next==one);
    assert(!word::NextOpen(one,next) && next==one); // cannot reopen an open gate
    next=91;
    assert(!word::NextOpen(one|word::kClosed|1,next) && next==91);
    assert(!word::NextOpen(last|word::kClosed,next) && next==91); // never wrap to epoch 1
    assert(word::NextOpen((last-one)|word::kClosed,next) && next==last);
    next=91;
    assert(!word::NextReader(one|word::kClosed,1,next) && next==91);
    assert(!word::NextReader(one,0,next) && next==91);
    assert(!word::NextReader(one,2,next) && next==91);
    assert(!word::NextReader(0,0,next) && next==91);
    assert(!word::NextReader(one|word::kReaders,1,next) && next==91);
    assert(word::NextReader(one|(word::kReaders-1),1,next));
    assert(next==(one|word::kReaders) && (next&word::kClosed)==0);
    assert(word::NextReader(last,UINT32_MAX,next) && next==last+1);
    // A delayed CAS carrying the old open/count word cannot match a reopen.
    std::atomic<uint64_t> state{2*one};
    uint64_t observed=one;
    assert(!state.compare_exchange_strong(observed,one+1));
    assert(observed==2*one && !word::NextReader(observed,1,next));
}

static void ConcurrentCloseAndWait()
{
    Gate g;assert(g.OpenAfterInit());
    std::atomic<bool> workerHeld{false},workerRelease{false},closeReturned{false};
    std::thread reader([&]{
        Gate::Reader ticket(g);assert(ticket && ticket.Epoch()==1);
        workerHeld.store(true,std::memory_order_release);
        while(!workerRelease.load(std::memory_order_acquire))std::this_thread::yield();
        assert(ticket.Epoch()==1); // the admitted lifetime never changes
    });
    while(!workerHeld.load(std::memory_order_acquire))std::this_thread::yield();
    std::thread closer([&]{g.CloseAndDrain();closeReturned.store(true,std::memory_order_release);});
    while(!g.Closed())std::this_thread::yield();
    assert(g.ReaderCount()==1 && !closeReturned.load(std::memory_order_acquire));
    {Gate::Reader late(g);assert(!late);}
    assert(!g.OpenAfterInit()); // failure must not consume a new epoch
    workerRelease.store(true,std::memory_order_release);
    reader.join();closer.join();
    assert(closeReturned.load(std::memory_order_acquire));
    assert(g.ReaderCount()==0 && g.Closed());
    assert(g.OpenAfterInit());
    {Gate::Reader fresh(g,2);assert(fresh && fresh.Epoch()==2);}
    {Gate::Reader stale(g);assert(!stale);}
    g.CloseAndDrain();
}

static void ConcurrentReaderCloseRace()
{
    for(int round=0;round<300;++round)
    {
        Gate g;assert(g.OpenAfterInit());
        std::atomic<bool> start{false};
        std::vector<std::thread> pool;
        for(int i=0;i<4;++i)pool.emplace_back([&]{
            while(!start.load(std::memory_order_acquire))std::this_thread::yield();
            for(int n=0;n<200;++n)
            {
                Gate::Reader guard(g);
                if(guard)assert(guard.Epoch()==1 && (!g.Closed() || g.ReaderCount()!=0));
            }
        });
        start.store(true,std::memory_order_release);
        g.CloseAndDrain();
        for(auto& thread:pool)thread.join();
        assert(g.Closed() && g.ReaderCount()==0);
        {Gate::Reader late(g);assert(!late);}
    }
}

static void DelayedReadersAfterReopen()
{
    Gate g;assert(g.OpenAfterInit());
    std::atomic<bool> released{false};
    std::atomic<unsigned> wronglyAdmitted{0};
    std::vector<std::thread> pool;
    for(int i=0;i<8;++i)pool.emplace_back([&]{
        while(!released.load(std::memory_order_acquire))std::this_thread::yield();
        for(int j=0;j<2000;++j)
        {
            Gate::Reader oldOrigin(g); // source belongs to the first lifetime
            if(oldOrigin)wronglyAdmitted.fetch_add(1,std::memory_order_relaxed);
        }
    });
    g.CloseAndDrain();
    assert(g.OpenAfterInit());
    released.store(true,std::memory_order_release);
    for(auto& thread:pool)thread.join();
    assert(wronglyAdmitted.load()==0 && g.ReaderCount()==0 && !g.Closed());
    {Gate::Reader currentOrigin(g,2);assert(currentOrigin && currentOrigin.Epoch()==2);}
    g.CloseAndDrain();
}

int main()
{
    static_assert(std::atomic<uint64_t>::is_always_lock_free,
                  "This host suite requires lock-free 64-bit admission");
    InitialAndRepeatLifecycle();
    BoundaryTransitions();
    ConcurrentCloseAndWait();
    ConcurrentReaderCloseRace();
    DelayedReadersAfterReopen();
    std::cout<<"P1 epoch gate HOST PASS: 300 race rounds; 16000 stale attempts rejected; "
             <<"GateBytes="<<sizeof(Gate)<<" ReaderBytes="<<sizeof(Gate::Reader)<<"\n";
}
