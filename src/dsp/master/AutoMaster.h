#pragma once

#include "../core/ProcessSpec.h"
#include "../filters/Biquad.h"
#include "../filters/OnePole.h"

namespace tf::dsp {

/** Adaptive mastering: listens to the mix and steers it, slowly, toward a balanced,
    finished sound at a chosen loudness. No presets to pick and nothing learned
    offline: it measures and corrects continuously, the way an engineer would ride a
    master bus over a long ambient piece.

    Analysis (on the signal itself, not in the audio path):
      - tonal balance: energy below 150 Hz, 150-500 Hz (mud), 500 Hz-4 kHz and above
        4 kHz, each smoothed over ~3 s, compared to a warm ambient target shape
      - loudness: K-weighted mean square over ~3 s (EBU short-term, ungated), in LUFS
      - stereo: side to mid energy

    Correction (every gain eases over seconds, so it never pumps or chases notes):
      - three-band EQ (low shelf 150 Hz, bell 350 Hz, high shelf 4 kHz), up to
        -6..+4 dB, scaled by `amount`
      - glue: a gentle 1.6:1 RMS compressor whose threshold sits 8 dB above the
        measured loudness, so it only rounds off swells
      - width toward a natural side/mid ratio, and lows below 120 Hz kept mono
      - make-up gain to the loudness target, -12..+12 dB; frozen when the input is
        near silence so it never drags hiss up between pieces

    The safety limiter after it still guarantees the ceiling. Switching on and off
    crossfades over half a second. No lookahead, so no latency. Realtime-safe. */
class AutoMaster
{
public:
    struct Params
    {
        bool enabled = false;
        float targetLufs = -16.0f;
        float amount = 0.6f; // 0..1 strength of EQ and width correction
    };

    struct State
    {
        float loudness = -70.0f;   // LUFS (short-term, before make-up)
        float gainDb = 0.0f;       // make-up applied
        float lowDb = 0.0f, mudDb = 0.0f, highDb = 0.0f;
        float width = 1.0f;        // side gain
        float reductionDb = 0.0f;  // glue
        float mix = 0.0f;          // 0 = bypassed, 1 = fully on
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

    // Analysis.
    Biquad kShelfL, kShelfR, kHpL, kHpR;    // K-weighting
    Biquad lowSplit, highSplit, mudLo, mudHi;
    double accK = 0.0, accLow = 0.0, accMud = 0.0, accMid = 0.0, accHigh = 0.0, accSide = 0.0, accMidSig = 0.0;
    int accCount = 0;
    float eK = 0.0f, eLow = 0.0f, eMud = 0.0f, eMidBand = 0.0f, eHigh = 0.0f, eSide = 0.0f, eMidSig = 0.0f;

    // Processing.
    Biquad eqLowL, eqLowR, eqMudL, eqMudR, eqHighL, eqHighR;
    OnePole sideLowCut;
    float compEnv = 0.0f, compGain = 1.0f;
    float gainLin = 1.0f, gainTarget = 1.0f;
    float widthCur = 1.0f;
    float mix = 0.0f;
    float appliedLow = 99.0f, appliedMud = 99.0f, appliedHigh = 99.0f;
};

} // namespace tf::dsp
