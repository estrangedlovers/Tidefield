#include "SpectralFreeze.h"

#include "../core/Denormal.h"
#include "../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

void SpectralFreeze::prepare(const ProcessSpec& s, std::uint64_t seed)
{
    spec = s;
    rng.setSeed(seed);
    fft.prepare(kOrder);
    window.resize(kSize);
    for (int i = 0; i < kSize; ++i)
        window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / kSize);
    inRing.assign(kSize, 0.0f);
    work.assign(kSize, {});
    avgMag.assign(kBins, 0.0f);
    frozenMag.assign(kBins, 0.0f);
    phaseL.assign(kBins, 0.0f);
    phaseR.assign(kBins, 0.0f);
    olaL.assign(kSize, 0.0f);
    olaR.assign(kSize, 0.0f);
    reset();
}

void SpectralFreeze::reset() noexcept
{
    std::fill(inRing.begin(), inRing.end(), 0.0f);
    std::fill(avgMag.begin(), avgMag.end(), 0.0f);
    std::fill(olaL.begin(), olaL.end(), 0.0f);
    std::fill(olaR.begin(), olaR.end(), 0.0f);
    for (std::size_t k = 0; k < phaseL.size(); ++k)
    {
        phaseL[k] = kTwoPi * rng.nextFloat();
        phaseR[k] = kTwoPi * rng.nextFloat();
    }
    inPos = hopCount = olaPos = 0;
    frozen = haveSpectrum = false;
    gain = 0.0f;
}

void SpectralFreeze::setFrozen(bool f) noexcept
{
    if (f && ! frozen)
    {
        std::copy(avgMag.begin(), avgMag.end(), frozenMag.begin());
        haveSpectrum = true;
    }
    frozen = f;
}

void SpectralFreeze::analyse() noexcept
{
    for (int i = 0; i < kSize; ++i)
        work[static_cast<std::size_t>(i)] = { inRing[static_cast<std::size_t>((inPos + i) & (kSize - 1))] * window[static_cast<std::size_t>(i)], 0.0f };
    fft.forward(work.data());
    for (int k = 0; k < kBins; ++k)
    {
        const float m = std::abs(work[static_cast<std::size_t>(k)]);
        auto& a = avgMag[static_cast<std::size_t>(k)];
        a = flushDenormal(0.5f * a + 0.5f * m);
    }
}

void SpectralFreeze::synthesise() noexcept
{
    // Unnormalised inverse of a hann-analysed sinusoid, times a hann synthesis window
    // that overlap-adds to 2 at 4x: 1/kSize restores the original amplitude.
    constexpr float kScale = 1.0f / static_cast<float>(kSize);
    const float jitter = drift * kPi;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& phase = ch == 0 ? phaseL : phaseR;
        auto& ola = ch == 0 ? olaL : olaR;
        for (int k = 0; k < kBins; ++k)
        {
            auto& p = phase[static_cast<std::size_t>(k)];
            // Centre-frequency advance per hop is 2*pi*k*hop/size = k*pi/2.
            p += 0.5f * kPi * static_cast<float>(k & 3) + jitter * (rng.nextFloat() - 0.5f);
            p -= kTwoPi * std::floor(p * (1.0f / kTwoPi));
            const float m = frozenMag[static_cast<std::size_t>(k)];
            work[static_cast<std::size_t>(k)] = { m * std::cos(p), m * std::sin(p) };
        }
        work[0] = { work[0].real(), 0.0f };
        work[kSize / 2] = { work[kSize / 2].real(), 0.0f };
        for (int k = 1; k < kSize / 2; ++k)
            work[static_cast<std::size_t>(kSize - k)] = std::conj(work[static_cast<std::size_t>(k)]);
        fft.inverse(work.data());
        for (int i = 0; i < kSize; ++i)
        {
            auto& o = ola[static_cast<std::size_t>((olaPos + i) & (kSize - 1))];
            o += work[static_cast<std::size_t>(i)].real() * window[static_cast<std::size_t>(i)] * kScale;
        }
    }
}

void SpectralFreeze::process(const float* in, float* outL, float* outR, int n) noexcept
{
    const float fs = static_cast<float>(spec.sampleRate);
    const float attackStep = 1.0f / (0.3f * fs);
    const float releaseStep = 1.0f / (releaseSeconds * fs);
    for (int i = 0; i < n; ++i)
    {
        inRing[static_cast<std::size_t>(inPos)] = in != nullptr ? in[i] : 0.0f;
        inPos = (inPos + 1) & (kSize - 1);

        gain = frozen ? std::min(1.0f, gain + attackStep) : std::max(0.0f, gain - releaseStep);
        auto& l = olaL[static_cast<std::size_t>(olaPos)];
        auto& r = olaR[static_cast<std::size_t>(olaPos)];
        outL[i] = l * gain;
        outR[i] = r * gain;
        l = r = 0.0f;
        olaPos = (olaPos + 1) & (kSize - 1);

        if (++hopCount == kHop)
        {
            hopCount = 0;
            if (! frozen)
                analyse(); // frozen: the held spectrum needs no fresh analysis
            if (haveSpectrum && (frozen || gain > 0.0f))
                synthesise();
        }
    }
}

} // namespace tf::dsp
