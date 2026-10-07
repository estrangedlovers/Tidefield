#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace tf::tools {

/** Objective checks on a rendered buffer, so a render can be asserted on without
    listening to it. */
struct Analysis
{
    double seconds = 0.0;
    float peakDb = -120.0f;
    float rmsDb = -120.0f;
    float dcOffset = 0.0f;           // worst channel, linear
    std::int64_t nonFiniteSamples = 0;
    std::int64_t samplesAboveCeiling = 0;
    float longestSilenceSeconds = 0.0f;  // longest run below -90 dBFS
    std::vector<float> rmsPerSecondDb;   // loudness envelope, one value per second

    static Analysis run(const std::vector<std::vector<float>>& channels, double sampleRate, float ceilingDb);
    juce::var toJson() const;
};

} // namespace tf::tools
