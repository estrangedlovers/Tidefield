#pragma once

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"
#include "../../core/ProcessSpec.h"
#include "../../filters/OnePole.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

/** Conditioning for an instrument through the audio interface: channel choice, gain,
    rumble high-pass, and a soft noise gate so a resting cello does not feed hiss into
    the resonators and clouds. Output is mono; the strip pans it. */
class LiveInput
{
public:
    enum class Channel : int { Left = 0, Right = 1, Sum = 2 };

    struct Params
    {
        Channel channel = Channel::Left;
        float gainDb = 0.0f;
        float highPassHz = 40.0f;
        float gateDb = -60.0f;   // -90 = gate off
    };

    void prepare(const ProcessSpec& spec)
    {
        fs = static_cast<float>(spec.sampleRate);
        highPass.prepare(spec.sampleRate);
        attack = onePoleCoefficient(0.002f, spec.sampleRate);
        release = onePoleCoefficient(0.25f, spec.sampleRate);
        followRelease = onePoleCoefficient(0.05f, spec.sampleRate);
        reset();
    }

    void reset() noexcept
    {
        highPass.reset();
        envelope = 0.0f;
        gateGain = 0.0f;
        level = 0.0f;
    }

    void setParams(const Params& p) noexcept { params = p; }

    /** inputs may be null or have fewer than two channels; writes mono to `out`. */
    void process(const float* const* inputs, int numInputs, int offset, float* out, int n) noexcept
    {
        const float* l = numInputs > 0 && inputs != nullptr ? inputs[0] : nullptr;
        const float* r = numInputs > 1 && inputs != nullptr ? inputs[1] : l;
        if (l == nullptr)
        {
            std::fill_n(out, n, 0.0f);
            level = 0.0f;
            return;
        }

        highPass.setCutoff(params.highPassHz);
        const float gain = dbToGain(params.gainDb);
        const float threshold = params.gateDb <= -89.0f ? 0.0f : dbToGain(params.gateDb);

        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            switch (params.channel)
            {
                case Channel::Left: x = l[offset + i]; break;
                case Channel::Right: x = r[offset + i]; break;
                case Channel::Sum: x = 0.5f * (l[offset + i] + r[offset + i]); break;
            }
            x = highPass.processHigh(x * gain);

            const float a = std::fabs(x);
            envelope += (a > envelope ? attack : followRelease) * (a - envelope);
            envelope = flushDenormal(envelope);
            const float target = envelope >= threshold ? 1.0f : 0.0f;
            gateGain += (target > gateGain ? attack : release) * (target - gateGain);
            gateGain = flushDenormal(gateGain);

            out[i] = x * gateGain;
            level = std::max(a, level * 0.9995f);
        }
    }

    float getLevel() const noexcept { return level; }
    bool isGateOpen() const noexcept { return gateGain > 0.5f; }

private:
    Params params;
    OnePole highPass;
    float fs = 48000.0f;
    float attack = 0.1f, release = 0.001f, followRelease = 0.01f;
    float envelope = 0.0f, gateGain = 0.0f, level = 0.0f;
};

} // namespace tf::dsp
