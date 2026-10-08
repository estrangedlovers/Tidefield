#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace tf::engine {
inline constexpr std::size_t kCacheLine = 128;

template <typename T>
class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue elements must be trivially copyable");

public:
    explicit SpscQueue(std::size_t minCapacity)
    {
        std::size_t capacity = 2;
        while (capacity < minCapacity + 1)
            capacity <<= 1;
        buffer.resize(capacity);
        mask = capacity - 1;
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    bool push(const T& item) noexcept
    {
        const auto w = writePos.load(std::memory_order_relaxed);
        const auto next = (w + 1) & mask;
        if (next == readPos.load(std::memory_order_acquire))
            return false;
        buffer[w] = item;
        writePos.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) noexcept
    {
        const auto r = readPos.load(std::memory_order_relaxed);
        if (r == writePos.load(std::memory_order_acquire))
            return false;
        out = buffer[r];
        readPos.store((r + 1) & mask, std::memory_order_release);
        return true;
    }

    std::size_t sizeApprox() const noexcept
    {
        const auto w = writePos.load(std::memory_order_acquire);
        const auto r = readPos.load(std::memory_order_acquire);
        return (w - r) & mask;
    }

    std::size_t capacity() const noexcept { return mask; }

private:
    std::vector<T> buffer;
    std::size_t mask = 0;
    alignas(kCacheLine) std::atomic<std::size_t> writePos { 0 };
    alignas(kCacheLine) std::atomic<std::size_t> readPos { 0 };
};
}
