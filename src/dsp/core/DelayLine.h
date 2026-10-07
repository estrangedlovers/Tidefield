#pragma once

#include "Interpolation.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace tf::dsp {

/** Power-of-two circular delay with Hermite fractional reads. Allocated in prepare. */
class DelayLine
{
public:
    void prepare(std::size_t maxDelaySamples)
    {
        std::size_t size = 4;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer.assign(size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void push(float x) noexcept
    {
        buffer[writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Value `delay` samples behind the most recent push: read(0) is the newest
        sample, read(1) the one before. Fractional delays use Hermite interpolation and
        are clamped to [1, capacity]. In a feedback loop, read before pushing: the loop
        delay is then delay + 1 samples. */
    float read(float delay) const noexcept
    {
        const float d = std::clamp(delay, 1.0f, static_cast<float>(capacity()));
        const auto whole = static_cast<std::size_t>(d);
        const float frac = d - static_cast<float>(whole);
        return hermite(at(whole - 1), at(whole), at(whole + 1), at(whole + 2), frac);
    }

    /** Integer read: k samples behind the newest (0 = newest). */
    float at(std::size_t k) const noexcept { return buffer[(writePos + mask - k) & mask]; }

    std::size_t capacity() const noexcept { return mask - 3; }

private:
    std::vector<float> buffer;
    std::size_t mask = 0;
    std::size_t writePos = 0;
};

} // namespace tf::dsp
