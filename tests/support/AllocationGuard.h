#pragma once

#include <cstddef>

namespace tf::test {
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept;
    ~ScopedAllocationCounter() noexcept;

    std::size_t count() const noexcept;
};
}
