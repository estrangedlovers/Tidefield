#pragma once

#include <engine/record/RecordTap.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace tf::io {

/** Writes the engine's record tap to disk: the master as one stereo 32-bit float WAV,
    and with stems one more stereo file per strip and send bus, all sample-aligned in
    one folder. A background thread drains the tap; the audio thread only copies into
    the tap's ring. Control (start/stop/status) is for the message thread. */
class Recorder : private juce::Thread
{
public:
    enum class State { Idle, Recording, Finishing };

    struct Status
    {
        State state = State::Idle;
        bool stems = false;
        double seconds = 0.0;            // written so far
        std::uint64_t droppedFrames = 0; // lost to a stalled disk
        juce::File folder;               // current or most recent recording
    };

    explicit Recorder(engine::RecordTap& tap);
    ~Recorder() override;

    /** Opens the files and starts capturing at the engine's next block. `folder` is
        created; the master goes to master.wav, stems to stems/<name>.wav. */
    juce::Result start(const juce::File& folder, double sampleRate, bool withStems);

    /** Captures to the end of the current block, then finishes the files in the
        background (state Finishing until they are closed). */
    void stop();

    Status getStatus() const;
    bool isActive() const { return getStatus().state != State::Idle; }

    /** Called on the writer thread when a recording's files are closed. */
    std::function<void(const juce::File& folder, bool ok)> onFinished;

    /** Tests and offline rendering: writes whatever the tap holds now, and closes the
        files if the engine has acknowledged a stop. */
    void drainNow();

    /** A fresh, non-existing folder name under `parent`: "2026-10-07 18.04.12 <name>". */
    static juce::File makeFolder(const juce::File& parent, const juce::String& name);

    /** Stem file names in tap channel order (after the master pair). */
    static juce::StringArray stemNames();

private:
    void run() override;
    bool drain(); // returns true when the recording completed
    void closeFiles(bool ok);

    engine::RecordTap& tap;
    juce::CriticalSection lock; // writer thread vs control calls; never the audio thread
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

} // namespace tf::io
