#pragma once

#include <engine/record/RecordTap.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace tf::io {
class Recorder : private juce::Thread
{
public:
    enum class State { Idle, Recording, Finishing };

    struct Status
    {
        State state = State::Idle;
        bool stems = false;
        double seconds = 0.0;
        std::uint64_t droppedFrames = 0;
        juce::File folder;
    };

    explicit Recorder(engine::RecordTap& tap);
    ~Recorder() override;

    juce::Result start(const juce::File& folder, double sampleRate, bool withStems);

    void stop();

    Status getStatus() const;
    bool isActive() const { return getStatus().state != State::Idle; }

    std::function<void(const juce::File& folder, bool ok)> onFinished;

    void drainNow();

    static juce::File makeFolder(const juce::File& parent, const juce::String& name);

    static juce::StringArray stemNames();

private:
    void run() override;
    bool drain();
    void closeFiles(bool ok);

    engine::RecordTap& tap;
    juce::CriticalSection lock;
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
    std::vector<float> interleaved;
    juce::AudioBuffer<float> chunk;
    juce::File folder;
    double sampleRate = 48000.0;
    bool stems = false;
    std::atomic<State> state { State::Idle };
    std::atomic<std::int64_t> framesWritten { 0 };
    std::atomic<std::uint32_t> stopRequestedAt { 0 };
    bool writeFailed = false;
};
}
