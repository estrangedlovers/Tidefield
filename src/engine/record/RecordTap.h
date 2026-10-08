#pragma once

#include "../mix/Layout.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace tf::engine {
class RecordTap
{
public:
    static constexpr int kStemPairs = kNumStrips + 2;
    static constexpr int kMaxChannels = 2 + 2 * kStemPairs;
    enum class State : int { Idle, Running, Stopping, Stopped };

    void prepare(double sampleRate, double seconds = 4.0)
    {
        if (state.load(std::memory_order_acquire) != State::Idle && capacity != 0)
            return;
        const auto frames = static_cast<std::size_t>(std::max(1.0, sampleRate * seconds));
        ring.assign(frames * kMaxChannels, 0.0f);
        capacity = ring.size();
    }

    bool begin(bool withStems) noexcept
    {
        if (capacity == 0 || state.load(std::memory_order_acquire) != State::Idle)
            return false;
        stride.store(withStems ? kMaxChannels : 2, std::memory_order_relaxed);
        readPos = writePos.load(std::memory_order_acquire);
        publishedRead.store(readPos, std::memory_order_release);
        dropped.store(0, std::memory_order_relaxed);
        state.store(State::Running, std::memory_order_release);
        return true;
    }

    void end() noexcept
    {
        auto expected = State::Running;
        state.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel);
    }

    void forceStopped() noexcept
    {
        auto expected = State::Stopping;
        state.compare_exchange_strong(expected, State::Stopped, std::memory_order_acq_rel);
    }

    int read(float* out, int maxFrames) noexcept
    {
        const auto s = static_cast<std::size_t>(getStride());
        const auto available = (writePos.load(std::memory_order_acquire) - readPos) / s;
        const auto frames = std::min<std::uint64_t>(available, static_cast<std::uint64_t>(std::max(0, maxFrames)));
        const auto count = static_cast<std::size_t>(frames) * s;
        auto idx = static_cast<std::size_t>(readPos % capacity);
        for (std::size_t i = 0; i < count; ++i)
        {
            out[i] = ring[idx];
            if (++idx == capacity)
                idx = 0;
        }
        readPos += count;
        publishedRead.store(readPos, std::memory_order_release);
        return static_cast<int>(frames);
    }

    void finish() noexcept
    {
        auto expected = State::Stopped;
        state.compare_exchange_strong(expected, State::Idle, std::memory_order_acq_rel);
    }

    State getState() const noexcept { return state.load(std::memory_order_acquire); }
    int getStride() const noexcept { return stride.load(std::memory_order_relaxed); }
    std::uint64_t getDroppedFrames() const noexcept { return dropped.load(std::memory_order_relaxed); }

    int beginBlock() noexcept
    {
        const auto s = state.load(std::memory_order_acquire);
        if (s == State::Stopping)
            state.store(State::Stopped, std::memory_order_release);
        blockStride = s == State::Running ? stride.load(std::memory_order_relaxed) : 0;
        return blockStride;
    }

    void push(const float* const* channels, int n) noexcept
    {
        if (blockStride == 0 || n <= 0)
            return;
        const auto s = static_cast<std::size_t>(blockStride);
        const auto count = static_cast<std::size_t>(n) * s;
        const auto w = writePos.load(std::memory_order_relaxed);
        const auto used = static_cast<std::size_t>(w - publishedRead.load(std::memory_order_acquire));
        if (capacity - used < count)
        {
            dropped.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
            return;
        }
        auto idx = static_cast<std::size_t>(w % capacity);
        for (int i = 0; i < n; ++i)
            for (std::size_t c = 0; c < s; ++c)
            {
                ring[idx] = channels[c][i];
                if (++idx == capacity)
                    idx = 0;
            }
        writePos.store(w + count, std::memory_order_release);
    }

private:
    std::vector<float> ring;
    std::size_t capacity = 0;
    std::atomic<State> state { State::Idle };
    std::atomic<int> stride { 2 };
    std::atomic<std::uint64_t> writePos { 0 };
    std::atomic<std::uint64_t> publishedRead { 0 };
    std::atomic<std::uint64_t> dropped { 0 };
    std::uint64_t readPos = 0;
    int blockStride = 0;
};
}
