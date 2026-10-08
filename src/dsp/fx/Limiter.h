#pragma once

#include "../core/ProcessSpec.h"

#include <vector>

namespace tf::dsp {
class Limiter
{
public:
    void prepare(const ProcessSpec& spec, float lookaheadMs = 3.0f);
    void reset() noexcept;

    void setCeilingDb(float db) noexcept;
    void setReleaseMs(float ms) noexcept;

    void process(float* left, float* right, int numSamples, float* gainOut = nullptr, const float* extraPeak = nullptr) noexcept;
    float getCeiling() const noexcept { return ceiling; }

    int getLatencySamples() const noexcept { return window + kTruePeakDelay; }
    float getCurrentGain() const noexcept { return lastGain; }

    static constexpr int kTruePeakDelay = 4;

private:
    float pushMin(float g) noexcept;
    float truePeak(const float* history) const noexcept;

    double fs = 48000.0;
    int window = 1;
    float ceiling = 0.89f;
    float releaseCoeff = 0.001f;
    float released = 1.0f;
    float lastGain = 1.0f;

    float histL[9] {}, histR[9] {}, histX[9] {};
    float phaseTaps[3][8] {};

    std::vector<float> delayL, delayR, boxBuffer;
    int writeIndex = 0;
    double boxSum = 0.0;

    std::vector<float> dequeValue;
    std::vector<long long> dequeIndex;
    int dequeHead = 0;
    int dequeSize = 0;
    long long sampleCounter = 0;
    int resumCounter = 0;
};
}
