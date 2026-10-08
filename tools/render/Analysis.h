#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace tf::tools {
struct Analysis
{
    double seconds = 0.0;
    float peakDb = -120.0f;
    float rmsDb = -120.0f;
    float dcOffset = 0.0f;
    std::int64_t nonFiniteSamples = 0;
    std::int64_t samplesAboveCeiling = 0;
    float longestSilenceSeconds = 0.0f;
    std::vector<float> rmsPerSecondDb;

    static Analysis run(const std::vector<std::vector<float>>& channels, double sampleRate, float ceilingDb);
    juce::var toJson() const;
};
}
