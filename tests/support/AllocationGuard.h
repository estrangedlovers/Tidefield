#pragma once

#include <cstddef>

namespace tf::test {

/** While an instance is alive on a thread, every global operator new on that thread is
    counted. Used to prove Engine::process never allocates. */
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept;
    ~ScopedAllocationCounter() noexcept;

    std::size_t count() const noexcept;
};

} // namespace tf::test
