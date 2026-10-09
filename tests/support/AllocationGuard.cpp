#include "AllocationGuard.h"

#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
 #include <malloc.h>
#endif

namespace {
thread_local bool tracking = false;
thread_local std::size_t allocations = 0;

void* allocate(std::size_t size)
{
    if (tracking)
        ++allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* allocateAligned(std::size_t size, std::align_val_t alignment)
{
    if (tracking)
        ++allocations;
    const auto a = static_cast<std::size_t>(alignment);
    const auto rounded = ((size == 0 ? 1 : size) + a - 1) / a * a;
#if defined(_MSC_VER)
    if (void* p = _aligned_malloc(rounded, a))
#else
    if (void* p = std::aligned_alloc(a, rounded))
#endif
        return p;
    throw std::bad_alloc();
}

void releaseAligned(void* p) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t a) { return allocateAligned(size, a); }
void* operator new[](std::size_t size, std::align_val_t a) { return allocateAligned(size, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }

namespace tf::test {
ScopedAllocationCounter::ScopedAllocationCounter() noexcept
{
    allocations = 0;
    tracking = true;
}

ScopedAllocationCounter::~ScopedAllocationCounter() noexcept { tracking = false; }

std::size_t ScopedAllocationCounter::count() const noexcept { return allocations; }
}
