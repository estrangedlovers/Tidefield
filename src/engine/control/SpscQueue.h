#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace tf::engine {

// 128 covers Apple Silicon's cache line; 64-byte-line CPUs just pay a little padding.
inline constexpr std::size_t kCacheLine = 128;

/** Bounded wait-free single-producer / single-consumer queue.
    Storage is allocated once in the constructor; push and pop never allocate or block.
    Exactly one thread may push and exactly one (other) thread may pop. */
template <typename T>
class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue elements must be trivially copyable");

public:
    /** Capacity is rounded up to a power of two. One slot is kept free. */
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

    /** Producer only. Returns false (and drops the item) when full. */
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

    /** Consumer only. Returns false when empty. */
    bool pop(T& out) noexcept
    {
        const auto r = readPos.load(std::memory_order_relaxed);
        if (r == writePos.load(std::memory_order_acquire))
            return false;
        out = buffer[r];
        readPos.store((r + 1) & mask, std::memory_order_release);
        return true;
    }

    /** Approximate; safe to call from either side for metering. */
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

} // namespace tf::engine
