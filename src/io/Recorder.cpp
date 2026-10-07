#include "Recorder.h"

namespace tf::io {

namespace {

constexpr int kChunkFrames = 4096;
constexpr juce::uint32 kForceStopMs = 1000; // no audio callback completed the stop

} // namespace

Recorder::Recorder(engine::RecordTap& t) : juce::Thread("Tidefield recorder"), tap(t)
{
    interleaved.resize(static_cast<std::size_t>(kChunkFrames * engine::RecordTap::kMaxChannels));
    chunk.setSize(2, kChunkFrames);
    startThread(juce::Thread::Priority::normal);
}

Recorder::~Recorder()
{
    stopThread(2000);
    if (state.load() != State::Idle)
    {
        stop();
        tap.forceStopped(); // the device may already be closed
        drainNow();
    }
}

juce::StringArray Recorder::stemNames()
{
    juce::StringArray names;
    for (const auto& s : engine::kStrips)
        names.add(s.id);
    names.add("send-a");
    names.add("send-b");
    return names;
}

juce::File Recorder::makeFolder(const juce::File& parent, const juce::String& name)
{
    auto base = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H.%M.%S");
    if (name.isNotEmpty())
        base << " " << juce::File::createLegalFileName(name);
    auto folder = parent.getChildFile(base);
    return folder.exists() ? folder.getNonexistentSibling(false) : folder;
}

juce::Result Recorder::start(const juce::File& newFolder, double rate, bool withStems)
{
    const juce::ScopedLock sl(lock);
    if (state.load() != State::Idle || tap.getState() != engine::RecordTap::State::Idle)
        return juce::Result::fail("The previous recording is still being written");
    if (rate <= 0.0)
        return juce::Result::fail("No audio is running");

    if (! newFolder.createDirectory())
        return juce::Result::fail("Could not create " + newFolder.getFullPathName());

    juce::Array<juce::File> files { newFolder.getChildFile("master.wav") };
    if (withStems)
    {
        const auto stemDir = newFolder.getChildFile("stems");
        if (! stemDir.createDirectory())
            return juce::Result::fail("Could not create " + stemDir.getFullPathName());
        for (const auto& n : stemNames())
            files.add(stemDir.getChildFile(n + ".wav"));
    }

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate(rate)
                             .withNumChannels(2)
                             .withBitsPerSample(32)
                             .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> opened;
    for (const auto& f : files)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(f);
        if (! static_cast<juce::FileOutputStream*>(stream.get())->openedOk())
            return juce::Result::fail("Could not write " + f.getFullPathName());
        auto writer = wav.createWriterFor(stream, options);
        if (writer == nullptr)
            return juce::Result::fail("Could not start a WAV file at " + f.getFullPathName());
        opened.push_back(std::move(writer));
    }

    if (! tap.begin(withStems))
        return juce::Result::fail("The engine is not ready to record");

    writers = std::move(opened);
    folder = newFolder;
    sampleRate = rate;
    stems = withStems;
    writeFailed = false;
    framesWritten.store(0);
    state.store(State::Recording);
    notify();
    return juce::Result::ok();
}

void Recorder::stop()
{
    auto expected = State::Recording;
    if (! state.compare_exchange_strong(expected, State::Finishing))
        return;
    stopRequestedAt.store(juce::Time::getMillisecondCounter());
    tap.end();
    notify();
}

Recorder::Status Recorder::getStatus() const
{
    Status s;
    s.state = state.load();
    s.droppedFrames = tap.getDroppedFrames();
    const juce::ScopedLock sl(lock);
    s.stems = stems;
    s.seconds = static_cast<double>(framesWritten.load()) / sampleRate;
    s.folder = folder;
    return s;
}

void Recorder::run()
{
    while (! threadShouldExit())
    {
        wait(state.load() == State::Idle ? 200 : 10);
        drain();
    }
}

void Recorder::drainNow() { drain(); }

bool Recorder::drain()
{
    const juce::ScopedLock sl(lock);
    if (state.load() == State::Idle)
        return false;

    const auto stride = static_cast<std::size_t>(tap.getStride());
    auto readAll = [&] {
        int frames = 0;
        while ((frames = tap.read(interleaved.data(), kChunkFrames)) > 0)
        {
            for (std::size_t w = 0; w < writers.size(); ++w)
            {
                auto* l = chunk.getWritePointer(0);
                auto* r = chunk.getWritePointer(1);
                for (int i = 0; i < frames; ++i)
                {
                    const auto base = static_cast<std::size_t>(i) * stride + 2 * w;
                    l[i] = interleaved[base];
                    r[i] = interleaved[base + 1];
                }
                if (! writeFailed && ! writers[w]->writeFromAudioSampleBuffer(chunk, 0, frames))
                    writeFailed = true;
            }
            framesWritten.fetch_add(frames);
        }
    };

    readAll();

    // A full disk ends the recording rather than silently losing the rest.
    if (writeFailed && state.load() == State::Recording)
    {
        state.store(State::Finishing);
        stopRequestedAt.store(juce::Time::getMillisecondCounter());
        tap.end();
    }

    if (state.load() != State::Finishing)
        return false;
    if (tap.getState() == engine::RecordTap::State::Stopping
        && juce::Time::getMillisecondCounter() - stopRequestedAt.load() > kForceStopMs)
        tap.forceStopped();
    if (tap.getState() != engine::RecordTap::State::Stopped)
        return false;

    readAll(); // whatever the last block pushed
    tap.finish();
    closeFiles(! writeFailed);
    return true;
}

void Recorder::closeFiles(bool ok)
{
    writers.clear(); // flushes and finalises the headers
    state.store(State::Idle);
    if (onFinished)
        onFinished(folder, ok);
}

} // namespace tf::io
