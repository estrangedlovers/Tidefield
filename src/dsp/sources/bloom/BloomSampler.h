#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../core/SampleBuffer.h"
#include "../../filters/OnePole.h"
#include "../../harmony/HarmonicGravity.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tf::dsp {
class BloomSampler
{
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kMaxGrains = 8;
    static constexpr int kMaxTaps = 6;
    static constexpr int kWindowSize = 512;

    enum class Transform : int { Swell = 0, Smear, Freeze, Ghost, Constellation, Tape };
    static constexpr int kNumTransforms = 6;
    static const char* transformName(Transform t) noexcept;

    struct Params
    {
        Transform transform = Transform::Swell;
        float amount = 0.5f;
        float lengthSeconds = 8.0f;
        float attackSeconds = 0.05f;
        float releaseSeconds = 2.0f;
        float rootNote = 60.0f;
        float pitch = 0.0f;
        float tone = 0.7f;
        float spread = 0.6f;
        float random = 0.3f;
        float position = 0.3f;
        float gravity = 1.0f;
    };

    struct VoiceView
    {
        bool active = false;
        float note = 0.0f;
        float level = 0.0f;
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;
    void setParams(const Params& p) noexcept { params = p; }
    void setHarmony(const HarmonicGravity* h) noexcept { harmony = h; }
    void setBuffer(const SampleBuffer* b) noexcept;

    static constexpr int kChannels = 16;

    void noteOn(int note, float velocity, int channel = -1) noexcept;
    void setGlobalBend(float semitones) noexcept { globalBend = semitones; }
    void setChannelExpression(int channel, float bendSemitones, float pressure, float timbre) noexcept;
    void noteOff(int note) noexcept;
    void setSustain(bool down) noexcept;
    void releaseAll(float seconds = 0.02f) noexcept;
    void setVoiceLimit(int limit) noexcept;

    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    int getActiveVoices() const noexcept;
    VoiceView getVoice(int i) const noexcept;
    bool isSilent() const noexcept { return getActiveVoices() == 0; }

private:
    struct Tap
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;
        int delay = 0;
        float gainL = 0.0f, gainR = 0.0f;
        int mip = -1;
    };

    struct Grain
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;
        int length = 1, age = 0;
        float gainL = 0.0f, gainR = 0.0f;
        int mip = 0;
    };

    enum class Stage { Attack, Hold, Release };

    struct Voice
    {
        bool active = false;
        bool held = false;
        bool sustained = false;
        int note = 60;
        float playedNote = 60.0f;
        float velocityGain = 1.0f;
        Transform transform = Transform::Swell;
        Stage stage = Stage::Attack;
        float env = 0.0f, attackStep = 1.0f, releaseCoeff = 0.999f;
        int age = 0, minLength = 0;
        float pan = 0.0f;
        double ratio = 1.0;
        Random rng;
        std::array<Tap, kMaxTaps> taps {};
        std::array<Grain, kMaxGrains> grains {};
        bool grainsOn = false;
        double centre = 0.0, centreInc = 0.0;
        double grainLength = 0.0;
        double grainInterval = 0.0, nextGrain = 0.0;
        float grainJitter = 0.0f, pitchJitter = 0.0f;
        int grainsStartAt = 0;
        float wowPhase = 0.0f, flutterPhase = 0.0f, tapeSpeed = 1.0f;
        OnePole lpL, lpR;
        float level = 0.0f;
        int channel = -1;
        float bendNow = 0.0f, pressureNow = 0.0f;
    };

    void startVoice(Voice& v, int note, float velocity) noexcept;
    void spawnGrain(Voice& v) noexcept;
    void renderVoice(Voice& v, float* left, float* right, int n, float timeScale, double pitchRatio, float expressionGain) noexcept;
    float window(float phase) const noexcept;
    float quantizedNote(float note, float seed) const noexcept;

    ProcessSpec spec;
    Params params;
    const SampleBuffer* buffer = nullptr;
    const HarmonicGravity* harmony = nullptr;
    Random rng;
    std::array<Voice, kMaxVoices> voices {};
    std::vector<float> hann;
    bool sustainPedal = false;
    int voiceLimit = kMaxVoices;
    float globalBend = 0.0f;
    std::array<float, kChannels> channelBend {}, channelPressure {};
    std::array<float, kChannels> channelTimbre { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
};
}
