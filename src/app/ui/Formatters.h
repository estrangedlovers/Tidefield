#pragma once

#include <dsp/fx/medium/Medium.h>
#include <dsp/sources/bloom/BloomSampler.h>
#include <dsp/harmony/Scale.h>

#include <juce_core/juce_core.h>

#include <cmath>

namespace tf::app::format {

inline juce::String note(double v) { return dsp::kNoteNames[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 11))]; }

inline juce::String scale(double v)
{
    return dsp::kScaleTypes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(v)), 0, static_cast<int>(dsp::kScaleTypes.size()) - 1))].name;
}

inline juce::String medium(double v)
{
    return dsp::Medium::typeName(static_cast<dsp::Medium::Type>(std::clamp(static_cast<int>(std::lround(v)), 0, dsp::Medium::kNumTypes - 1)));
}

inline juce::String wanderStyle(double v)
{
    static const char* names[] = { "Drift", "Orbit", "Tide pool" };
    return names[std::clamp(static_cast<int>(std::lround(v)), 0, 2)];
}

inline juce::String inputChannel(double v)
{
    static const char* names[] = { "Input 1", "Input 2", "1 + 2" };
    return names[std::clamp(static_cast<int>(std::lround(v)), 0, 2)];
}

inline juce::String bloomTransform(double v)
{
    return dsp::BloomSampler::transformName(
        static_cast<dsp::BloomSampler::Transform>(std::clamp(static_cast<int>(std::lround(v)), 0, dsp::BloomSampler::kNumTransforms - 1)));
}

inline juce::String catchSource(double v) { return v >= 0.5 ? "Live input" : "Output"; }

inline juce::String catchTarget(double v)
{
    const int t = static_cast<int>(std::lround(v));
    return t <= 0 ? juce::String("Auto") : "Cloud " + juce::String(t);
}

inline juce::String onOff(double v) { return v >= 0.5 ? "On" : "Off"; }

inline juce::String midiNote(double v)
{
    const int n = static_cast<int>(std::lround(v));
    return juce::String(dsp::kNoteNames[static_cast<std::size_t>(((n % 12) + 12) % 12)]) + juce::String(n / 12 - 1);
}

} // namespace tf::app::format
