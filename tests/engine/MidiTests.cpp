#include "support/AllocationGuard.h"

#include <engine/Engine.h>
#include <engine/midi/MidiManager.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace tf::engine;
using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;

RawMidi cc(int channel, int number, int value) { return { static_cast<std::uint8_t>(0xb0 | channel), static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value), 0 }; }
RawMidi noteOn(int channel, int note, int vel) { return { static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(vel), 0 }; }
RawMidi noteOff(int channel, int note) { return { static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(note), 0, 0 }; }

struct Rig
{
    Engine engine;
    MidiManager midi { engine };
    TelemetryFrame last;
    std::vector<EngineNotice> notices;
    std::vector<RawMidi> monitored;

    Rig() { engine.prepare(kFs, 256); }

    void run(int blocks = 4)
    {
        std::vector<float> l(256), r(256);
        float* outs[2] = { l.data(), r.data() };
        for (int i = 0; i < blocks; ++i)
        {
            engine.process(nullptr, 0, outs, 2, 256);
            TelemetryFrame f;
            while (engine.popTelemetry(f))
                last = f;
            EngineNotice n;
            while (engine.popNotice(n))
                notices.push_back(n);
            RawMidi m;
            while (engine.popMidiMonitor(m))
            {
                monitored.push_back(m);
                midi.handleMonitor(m);
            }
            midi.tick();
            engine.collectGarbage();
        }
    }

    /** Runs long enough for at least one telemetry frame. */
    void settle() { run(12); }

    float target(P p) const { return last.paramTargets[idx(p)]; }
    void send(const RawMidi& m) { engine.postMidi(0, m); }
};

MidiBinding bind(int number, P p, bool pickup = true)
{
    MidiBinding b;
    b.cc = number;
    b.param = idx(p);
    b.pickup = pickup;
    return b;
}

} // namespace

TEST_CASE("Soft takeover waits for the controller to reach the value, then follows")
{
    Rig rig;
    rig.midi.addBinding(bind(21, P::TerrainX)); // terrain.x starts at 0.5
    rig.settle();

    rig.send(cc(0, 21, 0)); // controller far below
    rig.settle();
    REQUIRE(rig.target(P::TerrainX) == Approx(0.5f));
    REQUIRE(rig.last.midiPickup[idx(P::TerrainX)] == -1);

    rig.send(cc(0, 21, 40));
    rig.settle();
    REQUIRE(rig.target(P::TerrainX) == Approx(0.5f)); // still below, still waiting

    rig.send(cc(0, 21, 80)); // crosses 0.5 -> caught
    rig.settle();
    REQUIRE(rig.target(P::TerrainX) == Approx(80.0f / 127.0f));
    REQUIRE(rig.last.midiPickup[idx(P::TerrainX)] == 0);

    rig.send(cc(0, 21, 20)); // now follows freely
    rig.settle();
    REQUIRE(rig.target(P::TerrainX) == Approx(20.0f / 127.0f));

    // Something else moves the parameter: the controller must pick up again.
    rig.engine.setParam(P::TerrainX, 0.9f);
    rig.settle();
    rig.send(cc(0, 21, 30));
    rig.settle();
    REQUIRE(rig.target(P::TerrainX) == Approx(0.9f));
    REQUIRE(rig.last.midiPickup[idx(P::TerrainX)] == -1);
}

TEST_CASE("Without pickup, ranges and curves apply immediately")
{
    Rig rig;
    auto b = bind(22, P::TerrainY, false);
    b.low = 0.2f;
    b.high = 0.6f;
    rig.midi.addBinding(b);
    auto c = bind(23, P::TideRate, false);
    c.curve = 1.0f; // slow start: pow(v, 4)
    rig.midi.addBinding(c);
    rig.settle();

    rig.send(cc(0, 22, 127));
    rig.send(cc(0, 23, 64));
    rig.settle();
    REQUIRE(rig.target(P::TerrainY) == Approx(0.6f));
    const auto& tide = rig.engine.getRegistry().spec(P::TideRate);
    REQUIRE(rig.target(P::TideRate) == Approx(tide.fromNormalised(std::pow(64.0f / 127.0f, 4.0f))).epsilon(0.001));
}

TEST_CASE("Channel-specific and any-channel bindings coexist on one CC")
{
    Rig rig;
    auto a = bind(30, P::TerrainX, false);
    a.channel = -1;
    auto b = bind(30, P::TerrainY, false);
    b.channel = 2;
    rig.midi.setBindings({ a, b });
    rig.settle();
    rig.send(cc(0, 30, 127)); // channel 1: only the any-channel binding
    rig.settle();
    REQUIRE(rig.target(P::TerrainX) == Approx(1.0f));
    REQUIRE(rig.target(P::TerrainY) == Approx(0.5f));
    rig.send(cc(2, 30, 0)); // channel 3: both
    rig.settle();
    REQUIRE(rig.target(P::TerrainX) == Approx(0.0f));
    REQUIRE(rig.target(P::TerrainY) == Approx(0.0f));
}

TEST_CASE("Buttons and pads fire actions once per press")
{
    Rig rig;
    rig.run(48000 * 6 / 256); // some audio in the catch ring
    MidiBinding button;
    button.cc = 40;
    button.action = MidiAction::Catch;
    MidiBinding pad;
    pad.source = MidiBinding::Source::Note;
    pad.cc = 36;
    pad.action = MidiAction::CaptureScene;
    rig.midi.setBindings({ button, pad });
    rig.settle();
    rig.notices.clear();

    rig.send(cc(0, 40, 127));
    rig.send(cc(0, 40, 127)); // held: no second catch
    rig.send(cc(0, 40, 0));
    rig.send(cc(0, 40, 127)); // pressed again
    rig.send(noteOn(9, 36, 100));
    rig.settle();
    int catches = 0, captures = 0;
    for (const auto& n : rig.notices)
    {
        catches += n.type == EngineNotice::Type::CatchReady;
        captures += n.type == EngineNotice::Type::CaptureSceneRequest;
    }
    REQUIRE(catches == 2);
    REQUIRE(captures == 1);
}

TEST_CASE("Notes play Bloom, respect the note channel and the sustain pedal")
{
    Rig rig;
    auto sample = std::make_shared<tf::dsp::SampleBuffer>();
    sample->sampleRate = kFs;
    sample->left.assign(48000, 0.0f);
    for (size_t i = 0; i < sample->left.size(); ++i)
        sample->left[i] = 0.3f * std::sin(0.05f * static_cast<float>(i));
    rig.engine.loadBloomSample(sample);
    rig.engine.setParam(P::BloomLength, 0.5f);
    rig.engine.setParam(P::BloomRelease, 0.1f);
    rig.engine.setParam(P::BloomTransform, 2.0f); // Freeze: sustains while held
    rig.midi.setNoteChannel(1);
    rig.settle();

    auto active = [&] {
        int n = 0;
        for (const auto& v : rig.last.bloomVoices)
            n += v.active ? 1 : 0;
        return n;
    };

    rig.send(noteOn(0, 60, 100)); // wrong channel
    rig.settle();
    REQUIRE(active() == 0);

    rig.send(cc(1, 64, 127)); // pedal down
    rig.send(noteOn(1, 60, 100));
    rig.send(noteOff(1, 60));
    rig.run(48000 * 2 / 256); // well past length + release
    REQUIRE(rig.last.sustainPedal);
    REQUIRE(active() == 1); // held by the pedal

    rig.send(cc(1, 64, 0));
    rig.run(48000 * 1 / 256);
    REQUIRE(active() == 0);
}

TEST_CASE("Notes can set the drone root")
{
    Rig rig;
    rig.midi.setNotesToDrone(true);
    rig.settle();
    rig.send(noteOn(0, 67, 90)); // G4 folds to G3 (55)
    rig.settle();
    REQUIRE(rig.target(P::DroneRoot) == Approx(55.0f));
}

TEST_CASE("Learn binds the next moved control and replaces its old target")
{
    Rig rig;
    std::string learned;
    rig.midi.onLearned = [&](const std::string& d) { learned = d; };
    rig.midi.learnParam(idx(P::TideRate));
    REQUIRE(rig.midi.isLearning());
    rig.send(cc(3, 64, 10)); // sustain pedal is ignored by learn
    rig.send(cc(3, 74, 10));
    rig.settle();
    REQUIRE_FALSE(rig.midi.isLearning());
    REQUIRE(rig.midi.getBindings().size() == 1);
    REQUIRE(rig.midi.getBindings()[0].cc == 74);
    REQUIRE(rig.midi.getBindings()[0].channel == 3);
    REQUIRE(learned == "CC 74 (ch 4) -> Tide");
    REQUIRE(rig.midi.describe(bind(5, P::Cloud2Level)) == "CC 5 (any ch) -> Cloud2 Level");

    rig.midi.learnParam(idx(P::TerrainWander));
    rig.send(cc(3, 74, 10));
    rig.settle();
    REQUIRE(rig.midi.getBindings().size() == 1);
    REQUIRE(rig.midi.getBindings()[0].param == idx(P::TerrainWander));
}

TEST_CASE("The default layout maps eight knobs and keeps master level below 0 dB")
{
    Rig rig;
    rig.midi.loadDefaultLayout();
    REQUIRE(rig.midi.getBindings().size() == 8);
    rig.settle();
    rig.engine.setParam(P::MasterLevel, -60.0f); // start low so pickup catches on the way up
    rig.settle();
    for (int v = 0; v <= 127; v += 8)
        rig.send(cc(5, 28, v));
    rig.send(cc(5, 28, 127));
    rig.settle();
    REQUIRE(rig.target(P::MasterLevel) == Approx(0.0f).margin(0.01));
}

TEST_CASE("MIDI handling is allocation-free on the audio thread")
{
    Rig rig;
    rig.midi.loadDefaultLayout();
    rig.settle();
    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    std::size_t allocations = 0;
    {
        const tf::test::ScopedAllocationCounter counter;
        for (int i = 0; i < 200; ++i)
        {
            for (int k = 0; k < 8; ++k)
                rig.engine.postMidi(k % kMaxMidiPorts, cc(0, 21 + k, (i * 7 + k) % 128));
            rig.engine.postMidi(1, noteOn(0, 48 + i % 24, 90));
            rig.engine.process(nullptr, 0, outs, 2, 256);
            RawMidi m;
            while (rig.engine.popMidiMonitor(m)) {}
            TelemetryFrame f;
            while (rig.engine.popTelemetry(f)) {}
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}
