#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
#include <engine/scene/SceneManager.h>
#include <io/Performance.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tf;
using Catch::Approx;

namespace {
constexpr double kRate = 48000.0;

io::Performance sweep(const engine::Engine& e, double seconds)
{
    io::Performance p;
    p.start = io::defaultSession(e);
    p.sampleRate = kRate;
    p.length = static_cast<std::uint64_t>(seconds * kRate);
    for (int k = 0; k <= 20; ++k)
    {
        const double t = seconds * k / 20.0;
        p.events.push_back({ static_cast<std::uint64_t>(t * kRate),
                             engine::ControlEvent::setParam(engine::idx(engine::P::DroneCutoff), 200.0f + 4000.0f * static_cast<float>(k) / 20.0f), 0 });
    }
    p.events.push_back({ static_cast<std::uint64_t>(0.5 * kRate), engine::ControlEvent::note(60, 0.8f), 0 });
    p.events.push_back({ static_cast<std::uint64_t>(1.0 * kRate), engine::ControlEvent::note(60, 0.0f), 0 });
    p.events.push_back({ static_cast<std::uint64_t>(1.2 * kRate), engine::ControlEvent::makeCommand(engine::Command::Catch), 0 });
    return p;
}

juce::MemoryBlock fileBytes(const juce::File& f)
{
    juce::MemoryBlock m;
    f.loadFileAsData(m);
    return m;
}
}

TEST_CASE("The engine logs a performance with sample times and leaves its own replays out", "[performance]")
{
    engine::Engine e;
    e.prepare(kRate, 256);
    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    e.setPerformanceRecording(true);
    e.process(nullptr, 0, outs, 2, 256);
    for (int b = 0; b < 100; ++b)
    {
        if (b == 50)
        {
            e.setParam(engine::P::DroneCutoff, 1234.0f);
            e.post(engine::ControlEvent::setParam(engine::idx(engine::P::BloomTone), 0.2f, engine::ControlSource::Score));
            e.noteOn(64, 0.5f);
        }
        e.process(nullptr, 0, outs, 2, 256);
    }
    e.setPerformanceRecording(false);
    std::vector<engine::GestureEvent> got;
    engine::GestureEvent g;
    while (e.popPerformance(g))
        got.push_back(g);
    REQUIRE(got.size() == 2);
    CHECK(got[0].event.param == engine::idx(engine::P::DroneCutoff));
    CHECK(static_cast<double>(got[0].time) / kRate == Approx(50.0 * 256.0 / kRate).margin(256.0 / kRate * 1.5));
    CHECK(got[1].event.type == engine::ControlEvent::Type::Note);
}

TEST_CASE("Performance edits: erase, mute, smooth and trim", "[performance]")
{
    engine::Engine e;
    e.prepare(kRate, 256);
    auto p = sweep(e, 4.0);
    const io::Performance::Lane cutoff { io::Performance::LaneKind::Param, engine::idx(engine::P::DroneCutoff) };
    CHECK(p.lanes().size() == 3);

    auto erased = p;
    erased.erase(1.0, 2.0, &cutoff);
    CHECK(erased.events.size() == p.events.size() - 6);

    auto muted = p;
    muted.setMuted(cutoff, true);
    CHECK(muted.playable().size() == 3);

    auto smoothed = p;
    smoothed.smooth(cutoff, 1.0);
    CHECK(smoothed.events.size() < p.events.size());

    auto trimmed = p;
    trimmed.trim(1.0, 3.0);
    CHECK(trimmed.seconds() == Approx(2.0).margin(1.0e-3));
    CHECK(trimmed.events.front().time == 0);
    CHECK(trimmed.events.front().event.value == Approx(200.0f + 4000.0f * 4.0f / 20.0f).margin(1.0f));
}

TEST_CASE("A performance round-trips through a session file", "[performance]")
{
    engine::Engine e;
    e.prepare(kRate, 256);
    auto p = sweep(e, 3.0);
    p.setMuted({ io::Performance::LaneKind::Notes, 0 }, true);
    auto data = p.start;
    data.performance = io::performanceToJson(p, e.getRegistry());
    const auto file = juce::File::createTempFile(".tidefield");
    juce::String error;
    REQUIRE(io::saveSession(data, file, error));
    auto loaded = io::loadSession(file, error);
    file.deleteFile();
    REQUIRE(loaded.has_value());
    auto back = io::performanceFromSession(*loaded, e.getRegistry());
    REQUIRE(back.has_value());
    CHECK(back->seconds() == Approx(3.0).margin(1.0e-3));
    CHECK(back->events.size() == p.events.size());
    CHECK(back->isMuted({ io::Performance::LaneKind::Notes, 0 }));
}

TEST_CASE("A performance replays in the engine on time", "[performance]")
{
    engine::Engine e;
    e.prepare(kRate, 256);
    auto take = std::make_unique<engine::GestureTake>();
    take->sampleRate = kRate;
    take->length = static_cast<std::uint64_t>(2.0 * kRate);
    take->events.push_back({ static_cast<std::uint64_t>(1.0 * kRate), engine::ControlEvent::setParam(engine::idx(engine::P::DroneCutoff), 3333.0f), 0 });
    take->version = 1;
    REQUIRE(e.publishPerformance(std::move(take)));
    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    double changedAt = -1.0;
    engine::TelemetryFrame f;
    for (int b = 0; b < static_cast<int>(2.5 * kRate / 256); ++b)
    {
        e.process(nullptr, 0, outs, 2, 256);
        while (e.popTelemetry(f))
            if (changedAt < 0.0 && std::abs(f.paramTargets[engine::idx(engine::P::DroneCutoff)] - 3333.0f) < 1.0f)
                changedAt = static_cast<double>(f.sampleTime) / kRate;
    }
    CHECK(changedAt == Approx(1.0).margin(0.05));
    CHECK(f.performanceState == 0);
}

TEST_CASE("Rendering a performance is faster than real time, audible and identical every time", "[performance][render]")
{
    engine::Engine e;
    e.prepare(kRate, 256);
    const auto p = sweep(e, 3.0);
    const auto dir = juce::File::createTempFile("render");
    io::RenderOptions o;
    o.stems = true;
    o.folder = dir.getChildFile("a");
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto first = io::renderPerformance(p, o);
    const auto took = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    REQUIRE(first.ok);
    CHECK(took < 3.0);
    CHECK(o.folder.getChildFile("stems").getChildFile("drone.wav").existsAsFile());
    o.folder = dir.getChildFile("b");
    const auto second = io::renderPerformance(p, o);
    REQUIRE(second.ok);
    CHECK(fileBytes(first.master) == fileBytes(second.master));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(first.master));
    REQUIRE(reader != nullptr);
    CHECK(reader->lengthInSamples == static_cast<juce::int64>(3.0 * kRate));
    juce::AudioBuffer<float> audio(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
    CHECK(audio.getMagnitude(0, audio.getNumSamples()) > 0.01f);
    dir.deleteRecursively();
}

TEST_CASE("A loop render folds its tail into its start so it repeats without a seam", "[performance][render]")
{
    engine::Engine e;
    e.prepare(kRate, 256);
    const auto p = sweep(e, 3.0);
    const auto dir = juce::File::createTempFile("loop");
    io::RenderOptions o;
    o.folder = dir;
    o.loopCrossfadeSeconds = 1.0;
    const auto result = io::renderPerformance(p, o);
    REQUIRE(result.ok);
    CHECK(result.master.getFileName() == "loop.wav");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(result.master));
    REQUIRE(reader != nullptr);
    CHECK(reader->lengthInSamples == static_cast<juce::int64>(3.0 * kRate));
    juce::AudioBuffer<float> audio(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
    const int n = audio.getNumSamples();
    float typicalStep = 0.0f;
    for (int i = 1000; i < 2000; ++i)
        typicalStep = std::max(typicalStep, std::abs(audio.getSample(0, i) - audio.getSample(0, i - 1)));
    CHECK(std::abs(audio.getSample(0, 0) - audio.getSample(0, n - 1)) <= typicalStep * 2.0f + 1.0e-4f);
    dir.deleteRecursively();
}
