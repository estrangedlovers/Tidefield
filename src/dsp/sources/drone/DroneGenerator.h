#pragma once

#include "../../core/MathUtil.h"
#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../harmony/HarmonicGravity.h"
#include "../../filters/Svf.h"
#include "../../mod/Drift.h"

#include <array>
#include <cstdint>

namespace tf::dsp {
class DroneGenerator
{
public:
    static constexpr int kMaxVoices = 6;
    static constexpr int kControlInterval = 32;

    struct Params
    {
        float rootNote = 38.0f;
        float detuneCents = 8.0f;
        float shape = 0.3f;
        float cutoffHz = 900.0f;
        float resonance = 0.2f;
        float noise = 0.1f;
        float driftDepth = 0.5f;
        float driftRate = 0.05f;
        float density = 3.0f;
        float evolve = 0.3f;
        float spread = 0.7f;
        float gravity = 0.0f;
        int wave = 0;
        int chord = 0;
        float sub = 0.0f;
        float fmRatio = 2.0f;
        float tilt = 0.0f;
        int filterType = 0;
        float keyTrack = 0.0f;
        float vibrato = 0.0f;
        float vibratoRate = 4.5f;
        float tremolo = 0.0f;
        float tremoloRate = 0.2f;
        float glideSeconds = 1.2f;
        float revoiceSeconds = 4.0f;
        float drive = 0.0f;
        float breathTone = 1.0f;
    };

    enum class Wave : int { Classic, Pulse, Fold, Organ, Fm, Count };
    enum class Chord : int { Open, Fifths, Octaves, Minor, Major, Suspended, Cluster, Harmonic, Count };
    static constexpr int kNumWaves = static_cast<int>(Wave::Count);
    static constexpr int kNumChords = static_cast<int>(Chord::Count);
    static const char* waveName(int w) noexcept;
    static const char* chordName(int c) noexcept;

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }
    void setHarmony(const HarmonicGravity* h) noexcept { harmony = h; }

    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    float getVoiceLevel(int voice) const noexcept;

    float getVoiceInterval(int voice) const noexcept;

    float getVoiceNote(int voice) const noexcept;

private:
    struct Voice
    {
        std::array<double, 3> phase {};
        std::array<double, 3> modPhase {};
        float breath = 0.0f;
        double fmIndex = 0.0;
        Svf filter;
        Random noise;
        Drift pitchDrift, cutoffDrift, panDrift, ampDrift;
        float interval = 0.0f;
        float pendingInterval = 0.0f;
        float densityGain = 0.0f;
        float revoiceGain = 1.0f;
        bool revoicing = false;
        bool fadingIn = false;
        float level = 0.0f;
        float basePan = 0.0f;
        float seed = 0.0f;
        float note = 0.0f;

        std::array<double, 3> increment {};
        float gainL = 0.0f, gainR = 0.0f;
        float prevGainL = 0.0f, prevGainR = 0.0f;
    };

    void updateControl(float dtSeconds, float realSeconds) noexcept;
    float renderVoiceSample(Voice& v) noexcept;
    float waveSample(int wave, double t, double dt, double tm, double fmIndex) const noexcept;

    ProcessSpec spec;
    Params params;
    Random rng;
    std::array<Voice, kMaxVoices> voices;
    float sineMix = 0.3f;
    float noiseGain = 0.0f;
    float breathCoef = 1.0f;
    float breathMakeup = 1.0f;
    float fmRatio = 2.0f;
    int filterType = 0;
    std::array<float, 3> filterWeight { 1.0f, 0.0f, 0.0f };
    float filterStep = 0.0f;
    int wave = 0, previousWave = 0;
    float waveFade = 1.0f, waveFadeStep = 0.0f;
    int chord = 0;
    double vibratoPhase = 0.0, tremoloPhase = 0.0;
    double subPhase = 0.0, subIncrement = 0.0;
    float subGain = 0.0f, prevSubGain = 0.0f;
    float driveGain = 1.0f, driveAmount = 0.0f, prevDriveGain = 1.0f, prevDriveAmount = 0.0f;
    TanhAdaa driveL, driveR;
    int samplesUntilControl = 0;
    const HarmonicGravity* harmony = nullptr;
    bool snapPitch = true;
};
}
