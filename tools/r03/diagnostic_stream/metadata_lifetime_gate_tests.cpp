// Host-only concurrency tests: no Unity Runtime/MetadataCache or GC.
#include "AssemblyShadowMetadataLifetime.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using il2cpp::vm::assembly_shadow_metadata_lifetime::Gate;
static void InitialAndRepeatLifecycle()
{
    Gate g;
    assert(g.Closed() && g.ReaderCount()==0);
    {Gate::Reader r(g);assert(!r);}
    assert(g.OpenAfterInit());
    assert(!g.Closed());
    assert(!g.OpenAfterInit());
    {
        Gate::Reader r(g);assert(r && g.ReaderCount()==1);
        {Gate::Reader nested(g);assert(nested && g.ReaderCount()==2);}
        assert(g.ReaderCount()==1);
    }
    assert(g.ReaderCount()==0);
    g.CloseAndDrain();
    assert(g.Closed());
    {Gate::Reader r(g);assert(!r);}
    g.CloseAndDrain();
    assert(g.OpenAfterInit());
    {Gate::Reader r(g);assert(r);}
    g.CloseAndDrain();
}

static void ConcurrentCloseAndWait()
{
    Gate g;
    assert(g.OpenAfterInit());
    std::atomic<bool> workerHeld{false};
    std::atomic<bool> workerRelease{false};
    std::atomic<bool> closeReturned{false};
    std::thread reader([&]{
        Gate::Reader ticket(g);assert(ticket);
        workerHeld.store(true,std::memory_order_release);
        while(!workerRelease.load(std::memory_order_acquire))std::this_thread::yield();
    });
    while(!workerHeld.load(std::memory_order_acquire))std::this_thread::yield();
    std::thread closer([&]{g.CloseAndDrain();closeReturned.store(true,std::memory_order_release);});
    while(!g.Closed())std::this_thread::yield();
    assert(g.ReaderCount()==1);
    assert(!closeReturned.load(std::memory_order_acquire));
    {Gate::Reader late(g);assert(!late);}
    assert(!g.OpenAfterInit());
    workerRelease.store(true,std::memory_order_release);
    reader.join();closer.join();
    assert(closeReturned.load(std::memory_order_acquire));
    assert(g.ReaderCount()==0 && g.Closed());
}
static void ConcurrentReaderCloseRace()
{
    constexpr int kRounds=300;
    for(int round=0;round<kRounds;++round)
    {
        Gate g;assert(g.OpenAfterInit());
        std::atomic<bool> start{false};
        std::vector<std::thread> pool;
        for(int i=0;i<4;++i)pool.emplace_back([&]{
            while(!start.load(std::memory_order_acquire))std::this_thread::yield();
            for(int n=0;n<200;++n)
            {
                Gate::Reader guard(g);
                if(guard)assert(!g.Closed() || g.ReaderCount()!=0);
            }
        });
        start.store(true,std::memory_order_release);
        g.CloseAndDrain();
        for(auto& thread:pool)thread.join();
        assert(g.Closed() && g.ReaderCount()==0);
        {Gate::Reader late(g);assert(!late);}
    }
}
int main()
{
    InitialAndRepeatLifecycle();
    ConcurrentCloseAndWait();
    ConcurrentReaderCloseRace();
    std::cout<<"P1 lifetime gate host-only concurrency contracts PASS\n";
}
