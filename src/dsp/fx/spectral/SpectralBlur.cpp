#include "SpectralBlur.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kFreezeChoices[] = { "Off", "On" };
}

using Curve = DisplayMap::Curve;

const ProcessorInfo SpectralBlur::kInfo {
    "tf.blur", "Spectral Blur",
    { { { "Blur", 0.5f, { Curve::Power, 20.0f, 2.0f, "s", 2 } },
        { "Smear", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Drift", 0.4f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Shimmer", 0.0f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Tone", 0.5f, { Curve::Linear, -6.0f, 12.0f, "dB/oct", 1 } },
        { "Freeze", 0.0f, { Curve::Choice, 0.0f, 1.0f, "", 0, kFreezeChoices, 2 } } } },
    false
};

void SpectralBlur::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    fft.prepare(kOrder);
    window.resize(kSize);
    for (int i = 0; i < kSize; ++i)
        window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / kSize);
    tilt.assign(kBins, 1.0f);
    scratch.assign(kBins + 1, 0.0f);
    for (auto& c : chans)
    {
        c.in.assign(kSize, 0.0f);
        c.ola.assign(kSize, 0.0f);
        c.mag.assign(kBins, 0.0f);
        c.rotRe.assign(kBins, 1.0f);
        c.rotIm.assign(kBins, 0.0f);
        c.spectrum.assign(kSize, {});
    }
    appliedTone = -1.0f;
    reset();
}

void SpectralBlur::reset() noexcept
{
    for (auto& c : chans)
    {
        std::fill(c.in.begin(), c.in.end(), 0.0f);
        std::fill(c.ola.begin(), c.ola.end(), 0.0f);
        std::fill(c.mag.begin(), c.mag.end(), 0.0f);
        std::fill(c.rotRe.begin(), c.rotRe.end(), 1.0f);
        std::fill(c.rotIm.begin(), c.rotIm.end(), 0.0f);
    }
    pos = hopCount = 0;
}

void SpectralBlur::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    blurSeconds = kInfo.controls[0].display.value(c[0]);
    smear = std::clamp(c[1], 0.0f, 1.0f);
    drift = std::clamp(c[2], 0.0f, 1.0f);
    shimmer = std::clamp(c[3], 0.0f, 1.0f);
    tone = std::clamp(c[4], 0.0f, 1.0f);
    freeze = c[5] >= 0.5f;
    timeScale = ctx.timeScale;
}

void SpectralBlur::updateTilt() noexcept
{
    const float slope = kInfo.controls[4].display.value(tone);
    const float binHz = static_cast<float>(fs) / kSize;
    for (int k = 0; k < kBins; ++k)
    {
        const float f = std::max(20.0f, static_cast<float>(k) * binHz);
        tilt[static_cast<std::size_t>(k)] = std::min(8.0f, dbToGain(slope * std::log2(f / 1000.0f)));
    }
    appliedTone = tone;
}

void SpectralBlur::hop(Channel& ch) noexcept
{
    for (int i = 0; i < kSize; ++i)
        ch.spectrum[static_cast<std::size_t>(i)] = { ch.in[static_cast<std::size_t>((pos + i) & (kSize - 1))] * window[static_cast<std::size_t>(i)], 0.0f };
    fft.forward(ch.spectrum.data());

    const float hopSeconds = static_cast<float>(kHop / fs) * std::max(0.01f, timeScale);
    const float follow = freeze ? 0.0f : (blurSeconds < 0.005f ? 1.0f : 1.0f - std::exp(-hopSeconds / blurSeconds));
    for (int k = 0; k < kBins; ++k)
    {
        auto& m = ch.mag[static_cast<std::size_t>(k)];
        const auto& x = ch.spectrum[static_cast<std::size_t>(k)];
        m = flushDenormal(m + follow * (std::sqrt(x.real() * x.real() + x.imag() * x.imag()) - m));
    }

    const int width = static_cast<int>(smear * 16.0f);
    const float* mags = ch.mag.data();
    if (width > 0)
    {
        scratch[0] = 0.0f;
        for (int k = 0; k < kBins; ++k)
            scratch[static_cast<std::size_t>(k + 1)] = scratch[static_cast<std::size_t>(k)] + ch.mag[static_cast<std::size_t>(k)];
    }

    const float jitter = drift * kPi * 0.5f;
    for (int k = 0; k < kBins; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        float m = mags[uk];
        if (width > 0)
        {
            const int lo = std::max(0, k - width), hi = std::min(kBins - 1, k + width);
            m = (scratch[static_cast<std::size_t>(hi + 1)] - scratch[static_cast<std::size_t>(lo)]) / static_cast<float>(hi - lo + 1);
        }
        if (shimmer > 0.0f && k >= 2)
            m += shimmer * 0.7f * mags[uk / 2];
        m *= tilt[uk];

        float& rr = ch.rotRe[uk];
        float& ri = ch.rotIm[uk];
        if (jitter > 0.0f)
        {
            const float a = jitter * (rng.nextFloat() - 0.5f);
            const float c = 1.0f - 0.5f * a * a;
            const float nr = rr * c - ri * a;
            const float ni = rr * a + ri * c;
            const float norm = 1.0f / std::sqrt(nr * nr + ni * ni);
            rr = nr * norm;
            ri = ni * norm;
        }
        const auto x = ch.spectrum[uk];
        const float xm = std::sqrt(x.real() * x.real() + x.imag() * x.imag());
        const float ur = xm > 1.0e-20f ? x.real() / xm : 1.0f;
        const float ui = xm > 1.0e-20f ? x.imag() / xm : 0.0f;
        ch.spectrum[uk] = { m * (ur * rr - ui * ri), m * (ur * ri + ui * rr) };
    }
    ch.spectrum[0] = { ch.spectrum[0].real(), 0.0f };
    ch.spectrum[kSize / 2] = { ch.spectrum[kSize / 2].real(), 0.0f };
    for (int k = 1; k < kSize / 2; ++k)
        ch.spectrum[static_cast<std::size_t>(kSize - k)] = std::conj(ch.spectrum[static_cast<std::size_t>(k)]);
    fft.inverse(ch.spectrum.data());

    constexpr float kScale = 1.0f / (static_cast<float>(kSize) * 1.5f);
    for (int i = 0; i < kSize; ++i)
        ch.ola[static_cast<std::size_t>((pos + i) & (kSize - 1))] += ch.spectrum[static_cast<std::size_t>(i)].real() * window[static_cast<std::size_t>(i)] * kScale;
}

void SpectralBlur::process(float* left, float* right, int n) noexcept
{
    if (appliedTone != tone)
        updateTilt();
    for (int i = 0; i < n; ++i)
    {
        auto& l = chans[0].ola[static_cast<std::size_t>(pos)];
        auto& r = chans[1].ola[static_cast<std::size_t>(pos)];
        chans[0].in[static_cast<std::size_t>(pos)] = left[i];
        chans[1].in[static_cast<std::size_t>(pos)] = right[i];
        left[i] = l;
        right[i] = r;
        l = r = 0.0f;
        pos = (pos + 1) & (kSize - 1);
        if (++hopCount == kHop)
        {
            hopCount = 0;
            hop(chans[0]);
            hop(chans[1]);
        }
    }
}
}
