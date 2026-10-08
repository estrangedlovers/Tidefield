#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../filters/OnePole.h"

#include <array>

namespace tf::dsp {
class SympatheticStrings final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kMaxStrings = 12;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    float getTailSeconds() const noexcept override { return 20.0f; }

    float getStringNote(int i) const noexcept { return strings[static_cast<std::size_t>(i)].note; }
    int getStringCount() const noexcept { return count; }

private:
    struct String
    {
        DelayLine line;
        OnePole damp;
        float period = 100.0f, targetPeriod = 100.0f;
        float feedback = 0.99f, inputGain = 0.0f;
        float gainL = 0.7f, gainR = 0.7f;
        float note = 60.0f;
    };

    void retune(std::uint16_t mask, int root) noexcept;

    double fs = 48000.0;
    std::array<String, kMaxStrings> strings;
    int count = 9;
    float excite = 0.5f, decaySeconds = 6.0f, brightness = 0.5f, level = 0.7f, spread = 0.7f;
    int octave = 3;
    std::uint16_t tunedMask = 0;
    int tunedRoot = -1, tunedOctave = -1, tunedCount = -1;
    float tunedDecay = -1.0f, tunedBrightness = -1.0f, tunedSpread = -1.0f;
};
}
