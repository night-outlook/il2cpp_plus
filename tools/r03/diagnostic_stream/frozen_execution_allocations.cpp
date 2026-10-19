// Separate translation unit: instrument actual global allocation calls without
// inlining replacement new/delete into the tests. No warning suppression.
#include <cstddef>
#include <cstdlib>
#include <new>
bool watchNew=false;
size_t watchedNew=0;
void* operator new(std::size_t bytes)
{
    if(watchNew)++watchedNew;
    if(void* value=std::malloc(bytes ? bytes : 1))return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value,std::size_t) noexcept { std::free(value); }
void operator delete[](void* value,std::size_t) noexcept { std::free(value); }
#if defined(__cpp_aligned_new)
void* operator new(std::size_t bytes,std::align_val_t alignment)
{
    if(watchNew)++watchedNew;
    void* value=nullptr;
    if(posix_memalign(&value,static_cast<size_t>(alignment),bytes ? bytes : 1)==0)return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes,std::align_val_t alignment){return ::operator new(bytes,alignment);}
void operator delete(void* value,std::align_val_t) noexcept { std::free(value); }
void operator delete[](void* value,std::align_val_t) noexcept { std::free(value); }
void operator delete(void* value,std::size_t,std::align_val_t) noexcept { std::free(value); }
void operator delete[](void* value,std::size_t,std::align_val_t) noexcept { std::free(value); }
#endif
