#include "vm/AssemblyShadowObservationCounters.h"
#include <cstdlib>
#include <new>
std::atomic<uint64_t> allocations{0};
void* operator new(std::size_t size)
{
    if (void* p = std::malloc(size ? size : 1)) { ++allocations; return p; }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
#if __cplusplus >= 201402L
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif
extern "C" void R02OtherTuAdd()
{
    using namespace il2cpp::vm::assembly_shadow_r02;
    ObservationCounters::Add(Metric::DefinitionHits, 7);
}
