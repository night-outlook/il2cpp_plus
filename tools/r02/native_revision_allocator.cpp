#include <atomic>
#include <cstdlib>
#include <new>
std::atomic<long> failNext{-1};
std::atomic<unsigned long> allocationCount{0};
void* operator new(std::size_t n)
{
    long expected = 0;
    if (failNext.compare_exchange_strong(expected, -1)) throw std::bad_alloc();
    if (void* value = std::malloc(n ? n : 1)) { ++allocationCount; return value; }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
#if __cplusplus >= 201402L
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif
