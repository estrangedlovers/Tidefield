#include "SympatheticStrings.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"
#include "../../harmony/HarmonicGravity.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kCountChoices[] = { "Six", "Nine", "Twelve" };
constexpr int kCounts[] = { 6, 9, 12 };
const char* const kOctaveChoices[] = { "Low", "Middle", "High", "Very high" };
constexpr std::uint16_t kFallbackMask = 0b010010101001;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo SympatheticStrings::kInfo {
    "tf.strings", "Sympathetic Strings",
    { { { "Excite", 0.5f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Decay", 0.5f, { Curve::Exp, 0.5f, 40.0f, "s", 1 } },
        { "Brightness", 0.5f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Register", 0.4f, { Curve::Choice, 0.0f, 1.0f, "", 0, kOctaveChoices, 4 } },
        { "Strings", 0.5f, { Curve::Choice, 0.0f, 1.0f, "", 0, kCountChoices, 3 } },
        { "Level", 0.6f, { Curve::Linear, 0.0f, 100.0f, "%" } } } },
    false
};

void SympatheticStrings::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto maxSamples = static_cast<std::size_t>(fs / 30.0) + 8;
    for (auto& s : strings)
    {
        s.line.prepare(maxSamples);
        s.damp.prepare(fs);
    }
    tunedRoot = -1;
    retune(kFallbackMask, 2);
    for (auto& s : strings)
        s.period = s.targetPeriod;
    reset();
}

void SympatheticStrings::reset() noexcept
{
    for (auto& s : strings)
    {
        s.line.reset();
        s.damp.reset();
    }
}

void SympatheticStrings::retune(std::uint16_t mask, int root) noexcept
{
    for (int i = std::max(0, tunedCount); i < count; ++i)
    {
        strings[static_cast<std::size_t>(i)].line.reset();
        strings[static_cast<std::size_t>(i)].damp.reset();
    }
    Scale scale;
    scale.mask = mask;
    scale.root = root;
    const int baseRoot = 12 * (octave + 1) + root;
    const float bright = 1500.0f * std::pow(8.0f, brightness);
    for (int i = 0; i < kMaxStrings; ++i)
    {
        auto& s = strings[static_cast<std::size_t>(i)];
        s.note = scale.degreeToNote(baseRoot, i);
        const float hz = midiToHz(s.note);
        s.targetPeriod = std::max(2.0f, static_cast<float>(fs) / hz - 1.0f);
        const float t60 = decaySeconds / (1.0f + 0.04f * static_cast<float>(i));
        s.feedback = std::pow(10.0f, -3.0f * (s.targetPeriod + 1.0f) / (t60 * static_cast<float>(fs)));
        s.inputGain = 6.0f * (1.0f - s.feedback);
        s.damp.setCutoff(std::min(bright * (1.0f + 0.15f * static_cast<float>(i)), static_cast<float>(fs) * 0.45f));
        const float pan = spread * (static_cast<float>(i % 2 == 0 ? i : -i) / static_cast<float>(kMaxStrings));
        s.gainL = std::sqrt(0.5f * (1.0f - pan));
        s.gainR = std::sqrt(0.5f * (1.0f + pan));
    }
    tunedMask = mask;
    tunedRoot = root;
    tunedOctave = octave;
    tunedCount = count;
    tunedDecay = decaySeconds;
    tunedBrightness = brightness;
    tunedSpread = spread;
}

void SympatheticStrings::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    excite = std::clamp(c[0], 0.0f, 1.0f);
    decaySeconds = kInfo.controls[1].display.value(c[1]);
    brightness = std::clamp(c[2], 0.0f, 1.0f);
    octave = 2 + std::clamp(static_cast<int>(c[3] * 4.0f), 0, 3);
    count = kCounts[std::clamp(static_cast<int>(c[4] * 3.0f), 0, 2)];
    level = std::clamp(c[5], 0.0f, 1.0f);

    std::uint16_t mask = kFallbackMask;
    int root = 2;
    if (ctx.harmony != nullptr)
    {
        mask = ctx.harmony->getTarget().mask;
        root = ctx.harmony->getTarget().root;
    }
    if (mask != tunedMask || root != tunedRoot || octave != tunedOctave || count != tunedCount
        || std::fabs(decaySeconds - tunedDecay) > 0.01f || std::fabs(brightness - tunedBrightness) > 0.005f
        || std::fabs(spread - tunedSpread) > 0.005f)
        retune(mask, root);
}

void SympatheticStrings::process(float* left, float* right, int n) noexcept
{
    const float norm = level * 1.6f / std::sqrt(static_cast<float>(count));
    const float glide = 0.0015f;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (left[i] + right[i]) * excite;
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < count; ++k)
        {
            auto& s = strings[static_cast<std::size_t>(k)];
            s.period += glide * (s.targetPeriod - s.period);
            const float y = s.damp.processLow(s.line.read(s.period));
            s.line.push(flushDenormal(x * s.inputGain + y * s.feedback));
            l += y * s.gainL;
            r += y * s.gainR;
        }
        left[i] += l * norm;
        right[i] += r * norm;
    }
}
}
