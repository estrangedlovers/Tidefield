#pragma once

#include "../core/ProcessSpec.h"
#include "../filters/Biquad.h"
#include "../filters/OnePole.h"

namespace tf::dsp {
class AutoMaster
{
public:
    struct Params
    {
        bool enabled = false;
        float targetLufs = -16.0f;
        float amount = 0.6f;
    };

    struct State
    {
        float loudness = -70.0f;
        float inputLoudness = -70.0f;
        float gainDb = 0.0f;
        float lowDb = 0.0f, mudDb = 0.0f, highDb = 0.0f;
        float width = 1.0f;
        float reductionDb = 0.0f;
        float mix = 0.0f;
    };

    void prepare(const ProcessSpec& spec);
    void reset() noexcept;
    void setParams(const Params& p) noexcept { params = p; }
    void process(float* left, float* right, int numSamples) noexcept;
    const State& getState() const noexcept { return state; }

private:
    static constexpr int kControlInterval = 64;
    void control() noexcept;

    ProcessSpec spec;
    Params params;
    State state;
    float fs = 48000.0f;
    int untilControl = 0;

    Biquad kShelfL, kShelfR, kHpL, kHpR;
    Biquad kInShelfL, kInShelfR, kInHpL, kInHpR;
    Biquad lowSplit, highSplit, mudLo, mudHi;
    double accK = 0.0, accKIn = 0.0, accLow = 0.0, accMud = 0.0, accMid = 0.0, accHigh = 0.0, accSide = 0.0, accMidSig = 0.0;
    int accCount = 0;
    float eK = 0.0f, eKIn = 0.0f, eLow = 0.0f, eMud = 0.0f, eMidBand = 0.0f, eHigh = 0.0f, eSide = 0.0f, eMidSig = 0.0f;

    Biquad eqLowL, eqLowR, eqMudL, eqMudR, eqHighL, eqHighR;
    OnePole sideLowCut;
    float compEnv = 0.0f, compGain = 1.0f;
    float gainLin = 1.0f, gainTarget = 1.0f;
    float widthCur = 1.0f;
    float mix = 0.0f;
    float appliedLow = 99.0f, appliedMud = 99.0f, appliedHigh = 99.0f;
};
}
