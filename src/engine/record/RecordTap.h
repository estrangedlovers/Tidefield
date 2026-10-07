#pragma once

#include "../mix/Layout.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace tf::engine {

/** Lock-free hand-off of the engine's output to a disk writer: a single-producer,
    single-consumer ring of interleaved frames plus a small state machine.

    Channel layout (stride 2 for master only, kMaxChannels with stems):
      0-1   master (post-Medium, post-limiter: exactly what the speakers get)
      2-17  each strip post-fader, in StripId order
      18-19 send bus A return, 20-21 send bus B return
    The stems sum to the master before its inserts, Medium and safety chain.

    States:
      Idle     -> Running   begin()        consumer, only while Idle
      Running  -> Stopping  end()          consumer
      Stopping -> Stopped   next block     producer (after its last push)
      Stopped  -> Idle      finish()       consumer, after draining
    The producer only touches the ring while Running, so begin() can move the read
    index (which the consumer owns) without racing the writer. The write index is
    never reset: a new recording starts reading wherever the writer is. */
class RecordTap
{
public:
    static constexpr int kStemPairs = kNumStrips + 2;
    static constexpr int kMaxChannels = 2 + 2 * kStemPairs;
    enum class State : int { Idle, Running, Stopping, Stopped };

    /** Message thread, audio stopped. Allocates `seconds` of headroom at the full
        stem width, so a writer can stall that long before frames are dropped. */
    void prepare(double sampleRate, double seconds = 4.0)
    {
        // A writer may still be draining (the device restarted mid-recording): keep the
        // ring it is reading. Headroom in seconds shrinks if the rate went up; that only
        // matters until the recorder stops, which it does on a rate change.
        if (state.load(std::memory_order_acquire) != State::Idle && capacity != 0)
            return;
        const auto frames = static_cast<std::size_t>(std::max(1.0, sampleRate * seconds));
        ring.assign(frames * kMaxChannels, 0.0f);
        capacity = ring.size();
    }

    // --- Consumer (writer thread / message thread) ------------------------------------

    /** Starts capturing at the next block. False if a recording is still running or
        draining, or the tap was never prepared. */
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

    /** Asks the producer to stop after its current block. */
    void end() noexcept
    {
        auto expected = State::Running;
        state.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel);
    }

    /** For when no audio is running (device stopped): nothing will complete the stop. */
    void forceStopped() noexcept
    {
        auto expected = State::Stopping;
        state.compare_exchange_strong(expected, State::Stopped, std::memory_order_acq_rel);
    }

    /** Copies up to maxFrames whole frames (interleaved at getStride()) into `out`. */
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
        publishedRead.store(readPos, std::memory_order_release); // frees the space for the producer
        return static_cast<int>(frames);
    }

    /** After the final read in the Stopped state: ready for the next begin(). */
    void finish() noexcept
    {
        auto expected = State::Stopped;
        state.compare_exchange_strong(expected, State::Idle, std::memory_order_acq_rel);
    }

    State getState() const noexcept { return state.load(std::memory_order_acquire); }
    int getStride() const noexcept { return stride.load(std::memory_order_relaxed); }
    /** Frames lost because the writer fell more than the headroom behind. */
    std::uint64_t getDroppedFrames() const noexcept { return dropped.load(std::memory_order_relaxed); }

    // --- Producer (audio thread) --------------------------------------------------------

    /** Once per block, before rendering it: the number of channels to push this block
        (0 = not recording; > 2 = stems are wanted). Completes a pending stop. */
    int beginBlock() noexcept
    {
        const auto s = state.load(std::memory_order_acquire);
        if (s == State::Stopping)
            state.store(State::Stopped, std::memory_order_release);
        blockStride = s == State::Running ? stride.load(std::memory_order_relaxed) : 0;
        return blockStride;
    }

    /** Pushes n frames of the channels latched by beginBlock(). A block that does not
        fit is dropped whole, so channels never fall out of step. */
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
    std::uint64_t readPos = 0; // consumer only; mirrored to publishedRead for the producer
    int blockStride = 0;       // producer only
};

} // namespace tf::engine
