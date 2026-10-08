#include <engine/Engine.h>
#include <engine/perform/GestureManager.h>
#include <dsp/core/SampleBuffer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::engine;
using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

struct Rig
{
    Engine engine;
    GestureManager gestures { engine };
    TelemetryFrame f;
    std::vector<float> l = std::vector<float>(kBlock), r = std::vector<float>(kBlock);
    double t = 0.0;

    Rig() { engine.prepare(kFs, kBlock); }

    void runUntil(double seconds)
    {
        float* outs[2] = { l.data(), r.data() };
        while (t < seconds)
        {
            engine.process(nullptr, 0, outs, 2, kBlock);
            while (engine.popTelemetry(f)) {}
            gestures.tick();
            engine.collectGarbage();
            t += kBlock / kFs;
        }
    }
    float target(P p) const { return f.paramTargets[idx(p)]; }
};

} // namespace

TEST_CASE("A gesture records the performer's moves with their times, nothing else", "[gesture]")
{
    Rig rig;
    rig.runUntil(0.1);
    rig.gestures.record();
    rig.runUntil(0.6);
    rig.engine.setParam(P::DroneCutoff, 2000.0f);
    rig.engine.post(ControlEvent::setParam(idx(P::DroneShape), 0.9f, ControlSource::Terrain)); // the terrain, not a move
    rig.runUntil(1.1);
    rig.engine.noteOn(64, 0.7f);
    rig.engine.command(Command::FadeOut); // transport, not part of a gesture
    rig.runUntil(2.1);
    rig.gestures.stop();
    rig.runUntil(2.2);

    REQUIRE_FALSE(rig.gestures.isRecording());
    REQUIRE(rig.gestures.hasTake());
    const auto& take = rig.gestures.getTake();
    REQUIRE(take.events.size() == 3);
    CHECK(take.events[0].event.param == idx(P::DroneCutoff));
    CHECK(static_cast<double>(take.events[0].time) / kFs == Approx(0.5).margin(0.01));
    CHECK(take.events[1].event.type == ControlEvent::Type::Note);
    CHECK(static_cast<double>(take.events[1].time) / kFs == Approx(1.0).margin(0.01));
    // The key still held when recording stopped is released at the end of the take.
    CHECK(take.events[2].event.type == ControlEvent::Type::Note);
    CHECK(take.events[2].event.value == 0.0f);
    CHECK(take.events[2].time == take.length);
    CHECK(static_cast<double>(take.length) / kFs == Approx(2.0).margin(0.01));
    CHECK(take.sampleRate == Approx(kFs));
}

TEST_CASE("A take plays back on time and loops", "[gesture]")
{
    Rig rig;
    GestureTake take;
    take.sampleRate = kFs;
    take.length = static_cast<std::uint64_t>(1.0 * kFs);
    take.loop = true;
    take.events.push_back({ static_cast<std::uint64_t>(0.5 * kFs), ControlEvent::setParam(idx(P::DroneCutoff), 2000.0f) });
    rig.gestures.setTake(take);
    rig.engine.setParam(P::DroneCutoff, 500.0f);
    rig.runUntil(0.1);

    rig.gestures.play();
    const double start = rig.t;
    rig.runUntil(start + 0.45);
    CHECK(rig.f.gestureState == GestureState::Playing);
    CHECK(rig.target(P::DroneCutoff) == Approx(500.0f));
    rig.runUntil(start + 0.6);
    CHECK(rig.target(P::DroneCutoff) == Approx(2000.0f));

    // Change it by hand; the next pass plays the move again.
    rig.engine.setParam(P::DroneCutoff, 700.0f);
    rig.runUntil(start + 1.4);
    CHECK(rig.target(P::DroneCutoff) == Approx(700.0f));
    rig.runUntil(start + 1.6);
    CHECK(rig.target(P::DroneCutoff) == Approx(2000.0f));
    CHECK(rig.f.gestureLength == Approx(1.0f));

    // Once: it stops by itself at the end.
    rig.gestures.setLoop(false, true);
    rig.runUntil(rig.t + 1.2);
    CHECK(rig.f.gestureState == GestureState::Idle);
}

TEST_CASE("Restarting the audio ends a recording cleanly", "[gesture]")
{
    Rig rig;
    rig.gestures.record();
    rig.runUntil(0.5);
    rig.engine.setParam(P::DroneCutoff, 1500.0f);
    rig.runUntil(1.0);
    rig.engine.prepare(44100.0, kBlock); // e.g. the device changed rate
    rig.gestures.tick();
    REQUIRE(rig.gestures.hasTake());
    CHECK(rig.gestures.getTake().events.size() == 1);
    CHECK(static_cast<double>(rig.gestures.getTake().length) / kFs == Approx(1.0).margin(0.02));
}

TEST_CASE("Loading a take while recording keeps the loaded take", "[gesture]")
{
    Rig rig;
    rig.gestures.record();
    rig.runUntil(0.3);
    rig.engine.setParam(P::DroneCutoff, 1500.0f);
    // A session is opened mid-recording: its take must not be replaced by the
    // abandoned recording's moves or end marker.
    GestureTake loaded;
    loaded.sampleRate = kFs;
    loaded.length = static_cast<std::uint64_t>(3.0 * kFs);
    loaded.events.push_back({ static_cast<std::uint64_t>(kFs), ControlEvent::setParam(idx(P::DroneShape), 0.2f) });
    rig.gestures.setTake(loaded);
    rig.runUntil(1.0);
    REQUIRE(rig.gestures.hasTake());
    CHECK(rig.gestures.getTake().events.size() == 1);
    CHECK(rig.gestures.getTake().events[0].event.param == idx(P::DroneShape));
    CHECK_FALSE(rig.gestures.isRecording());

    // Recording twice in a row: only the second take survives.
    rig.gestures.record();
    rig.runUntil(1.2);
    rig.engine.setParam(P::DroneCutoff, 800.0f);
    rig.gestures.record();
    rig.runUntil(1.5);
    rig.engine.setParam(P::DroneCutoff, 900.0f);
    rig.runUntil(1.7);
    CHECK(rig.gestures.isRecording());
    rig.gestures.stop();
    rig.runUntil(1.8);
    REQUIRE(rig.gestures.getTake().events.size() == 1);
    CHECK(rig.gestures.getTake().events[0].event.value == Approx(900.0f));
}

TEST_CASE("Stopping playback releases the notes the take was holding", "[gesture]")
{
    Rig rig;
    rig.engine.loadBloomSample([] {
        auto b = std::make_shared<tf::dsp::SampleBuffer>();
        b->left.assign(48000, 0.1f);
        return std::shared_ptr<const tf::dsp::SampleBuffer>(std::move(b));
    }());
    GestureTake take;
    take.sampleRate = kFs;
    take.length = static_cast<std::uint64_t>(10.0 * kFs);
    take.events.push_back({ static_cast<std::uint64_t>(0.1 * kFs), ControlEvent::note(60, 0.8f) }); // held, no note-off
    rig.gestures.setTake(take);
    // A Bloom note lasts at least Length, and as long as it is held: short values
    // make "released" and "still held" easy to tell apart.
    rig.engine.setParam(P::BloomLength, 0.5f);
    rig.engine.setParam(P::BloomRelease, 0.1f);
    rig.runUntil(0.1);
    rig.gestures.play();
    rig.runUntil(1.5);
    bool heldWhilePlaying = false;
    for (const auto& v : rig.f.bloomVoices)
        heldWhilePlaying = heldWhilePlaying || v.active;
    CHECK(heldWhilePlaying);
    rig.gestures.stop();
    rig.runUntil(4.0);
    bool anyHeld = false;
    for (const auto& v : rig.f.bloomVoices)
        anyHeld = anyHeld || (v.active && v.level > 0.01f);
    CHECK_FALSE(anyHeld);
}
