#include "Limiter.h"

#include "../core/Denormal.h"
#include "../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

void Limiter::prepare(const ProcessSpec& spec, float lookaheadMs)
{
    fs = spec.sampleRate;
    window = std::max(1, static_cast<int>(std::lround(lookaheadMs * 0.001 * fs)));
    delayL.assign(static_cast<size_t>(window), 0.0f);
    delayR.assign(static_cast<size_t>(window), 0.0f);
    boxBuffer.assign(static_cast<size_t>(window), 1.0f);
    // The minimum spans window + 1 samples so the box filter, which averages the
    // `window` most recent values, only ever sees values covering the delayed sample.
    dequeValue.assign(static_cast<size_t>(window) + 2, 1.0f);
    dequeIndex.assign(static_cast<size_t>(window) + 2, 0);
    setReleaseMs(250.0f);
    reset();
}

void Limiter::reset() noexcept
{
    std::fill(delayL.begin(), delayL.end(), 0.0f);
    std::fill(delayR.begin(), delayR.end(), 0.0f);
    std::fill(boxBuffer.begin(), boxBuffer.end(), 1.0f);
    boxSum = static_cast<double>(window);
    writeIndex = 0;
    dequeHead = 0;
    dequeSize = 0;
    sampleCounter = 0;
    resumCounter = 0;
    released = 1.0f;
    lastGain = 1.0f;
}

void Limiter::setCeilingDb(float db) noexcept { ceiling = dbToGain(std::min(db, 0.0f)); }

void Limiter::setReleaseMs(float ms) noexcept
{
    releaseCoeff = onePoleCoefficient(std::max(ms, 1.0f) * 0.001f, fs);
}

float Limiter::pushMin(float g) noexcept
{
    const int capacity = static_cast<int>(dequeValue.size());

    // Drop entries from the back that are >= the new value; they can never be the minimum.
    while (dequeSize > 0)
    {
        const int back = (dequeHead + dequeSize - 1) % capacity;
        if (dequeValue[static_cast<size_t>(back)] < g)
            break;
        --dequeSize;
    }

    const int slot = (dequeHead + dequeSize) % capacity;
    dequeValue[static_cast<size_t>(slot)] = g;
    dequeIndex[static_cast<size_t>(slot)] = sampleCounter;
    ++dequeSize;

    // Expire the front once it falls out of the window.
    while (dequeIndex[static_cast<size_t>(dequeHead)] <= sampleCounter - (window + 1))
    {
        dequeHead = (dequeHead + 1) % capacity;
        --dequeSize;
    }

    ++sampleCounter;
    return dequeValue[static_cast<size_t>(dequeHead)];
}

void Limiter::process(float* left, float* right, int numSamples) noexcept
{
    const auto w = static_cast<size_t>(window);

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = right[i];
        const float peak = std::max(std::fabs(inL), std::fabs(inR));
        const float required = peak > ceiling ? ceiling / peak : 1.0f;

        const float held = pushMin(required);
        released = held < released ? held : flushDenormal(released + releaseCoeff * (held - released));

        const auto idx = static_cast<size_t>(writeIndex);
        boxSum += static_cast<double>(released) - static_cast<double>(boxBuffer[idx]);
        boxBuffer[idx] = released;

        // Periodically re-sum to stop floating-point drift in the running total.
        if (++resumCounter >= 1 << 16)
        {
            resumCounter = 0;
            boxSum = 0.0;
            for (size_t k = 0; k < w; ++k)
                boxSum += static_cast<double>(boxBuffer[k]);
        }

        const float gain = std::min(1.0f, static_cast<float>(boxSum / static_cast<double>(window)));

        // The slot about to be overwritten holds the sample from `window` samples ago.
        const float outL = delayL[idx] * gain;
        const float outR = delayR[idx] * gain;
        delayL[idx] = inL;
        delayR[idx] = inR;
        writeIndex = (writeIndex + 1) % window;

        left[i] = std::clamp(outL, -ceiling, ceiling);
        right[i] = std::clamp(outR, -ceiling, ceiling);
        lastGain = gain;
    }
}

} // namespace tf::dsp
