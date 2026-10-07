#include <engine/Engine.h>
#include <engine/perform/GestureManager.h>

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
    REQUIRE(take.events.size() == 2);
    CHECK(take.events[0].event.param == idx(P::DroneCutoff));
    CHECK(static_cast<double>(take.events[0].time) / kFs == Approx(0.5).margin(0.01));
    CHECK(take.events[1].event.type == ControlEvent::Type::Note);
    CHECK(static_cast<double>(take.events[1].time) / kFs == Approx(1.0).margin(0.01));
    CHECK(static_cast<double>(take.length) / kFs == Approx(2.0).margin(0.01));
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
