// Test executable allocator accounting only. Kept in its own translation unit
// to avoid attributing its malloc-backed replacement-new implementation to the
// product serializer. No warning or sanitizer is disabled.
#include <cstddef>
#include <cstdlib>
#include <new>
bool watch = false;
size_t newCalls = 0;
void* operator new(std::size_t n)
{
    if (watch) ++newCalls;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t alignment)
{
    if (watch) ++newCalls;
    void* p = nullptr;
    if (!posix_memalign(&p, static_cast<size_t>(alignment), n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
