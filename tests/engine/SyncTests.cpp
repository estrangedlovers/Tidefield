#include <engine/Engine.h>
#include <engine/midi/MidiManager.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace tf;
using Catch::Approx;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 128;

engine::RawMidi raw(std::uint8_t status, std::uint8_t d1 = 0, std::uint8_t d2 = 0, double time = 0.0)
{
    engine::RawMidi m;
    m.status = status;
    m.data1 = d1;
    m.data2 = d2;
    m.time = time;
    return m;
}

struct Rig
{
    engine::Engine engine;
    engine::MidiManager midi { engine };
    std::vector<float> l = std::vector<float>(kBlock), r = std::vector<float>(kBlock);
    double now = 0.0;
    engine::TelemetryFrame last;

    Rig() { engine.prepare(kFs, kBlock); }

    void block()
    {
        float* outs[2] = { l.data(), r.data() };
        engine.process(nullptr, 0, outs, 2, kBlock);
        now += kBlock / kFs;
        while (engine.popTelemetry(last)) {}
    }
};

std::shared_ptr<dsp::SampleBuffer> sine(float hz)
{
    auto b = std::make_shared<dsp::SampleBuffer>();
    b->sampleRate = kFs;
    b->left.resize(static_cast<std::size_t>(kFs * 4));
    for (std::size_t i = 0; i < b->left.size(); ++i)
        b->left[i] = 0.5f * std::sin(2.0f * 3.14159265f * hz * static_cast<float>(i) / static_cast<float>(kFs));
    return b;
}

float zeroCrossingRate(Rig& rig, double seconds)
{
    int crossings = 0;
    float prev = 0.0f;
    const int blocks = static_cast<int>(seconds * kFs / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        rig.block();
        for (float x : rig.l)
        {
            if ((x >= 0.0f) != (prev >= 0.0f))
                ++crossings;
            prev = x;
        }
    }
    return static_cast<float>(crossings) / static_cast<float>(2.0 * seconds);
}
}

TEST_CASE("MIDI clock sets the tempo and the beat when Follow is on MIDI clock", "[sync]")
{
    Rig rig;
    rig.engine.setParam(engine::P::SyncOn, 1.0f);
    rig.engine.setParam(engine::P::SyncSource, 1.0f);
    rig.block();
    const double tick = 60.0 / (126.0 * 24.0);
    double nextTick = 1.0;
    rig.engine.postMidi(0, raw(0xfa, 0, 0, nextTick));
    for (int b = 0; b < static_cast<int>(3.0 * kFs / kBlock); ++b)
    {
        const double blockEnd = 1.0 + rig.now;
        while (nextTick < blockEnd)
        {
            rig.engine.postMidi(0, raw(0xf8, 0, 0, nextTick));
            nextTick += tick;
        }
        rig.block();
    }
    CHECK(rig.last.bpm == Approx(126.0f).margin(0.5f));
    CHECK(rig.last.hostTempo);

    rig.engine.setParam(engine::P::SyncSource, 0.0f);
    for (int b = 0; b < 20; ++b)
        rig.block();
    CHECK_FALSE(rig.last.hostTempo);
    CHECK(rig.last.bpm == Approx(rig.engine.getRegistry().spec(engine::P::SyncBpm).defaultValue));
}

TEST_CASE("Clock messages are ignored while Follow is on the internal tempo", "[sync]")
{
    Rig rig;
    rig.engine.setParam(engine::P::SyncOn, 1.0f);
    rig.block();
    for (int k = 0; k < 200; ++k)
        rig.engine.postMidi(0, raw(0xf8, 0, 0, 1.0 + k * 0.01));
    rig.block();
    rig.block();
    CHECK_FALSE(rig.last.hostTempo);
}

TEST_CASE("Pitch bend moves Bloom; in MPE mode each channel bends only its own note", "[mpe]")
{
    Rig rig;
    rig.engine.loadBloomSample(sine(220.0f));
    rig.engine.setParam(engine::P::BloomTransform, 5.0f);
    rig.engine.setParam(engine::P::BloomAmount, 0.0f);
    rig.engine.setParam(engine::P::BloomGravity, 0.0f);
    rig.engine.setParam(engine::P::BloomRoot, 57.0f);
    rig.engine.setParam(engine::P::BloomAttack, 0.005f);
    rig.engine.setParam(engine::P::BloomTone, 1.0f);
    rig.engine.setParam(engine::P::BloomSendA, -60.0f);
    rig.engine.setParam(engine::P::BloomSendB, -60.0f);
    rig.engine.setParam(engine::P::ResLevel, -60.0f);
    rig.engine.setParam(engine::P::DroneLevel, -60.0f);
    rig.engine.setParam(engine::P::MasterFadeSecs, 0.5f);
    rig.engine.command(engine::Command::FadeIn);
    for (int b = 0; b < 400; ++b)
        rig.block();

    rig.engine.postMidi(0, raw(0x90, 57, 100));
    for (int b = 0; b < 40; ++b)
        rig.block();
    const float plain = zeroCrossingRate(rig, 0.3);
    CHECK(plain == Approx(220.0f).margin(8.0f));

    rig.engine.postMidi(0, raw(0xe0, 0x7f, 0x7f));
    for (int b = 0; b < 40; ++b)
        rig.block();
    const float bent = zeroCrossingRate(rig, 0.3);
    CHECK(bent / plain == Approx(std::exp2(2.0f / 12.0f)).margin(0.03f));
    rig.engine.postMidi(0, raw(0xe0, 0, 0x40));
    rig.engine.postMidi(0, raw(0x80, 57, 0));
    for (int b = 0; b < 1500; ++b)
        rig.block();

    rig.midi.setMpe(true);
    rig.block();
    rig.engine.postMidi(0, raw(0x91, 57, 100));
    rig.engine.postMidi(0, raw(0xe1, 0, 0x50));
    for (int b = 0; b < 40; ++b)
        rig.block();
    const float memberBent = zeroCrossingRate(rig, 0.3);
    CHECK(memberBent / plain == Approx(std::exp2(12.0f / 12.0f)).margin(0.05f));
}

TEST_CASE("Bloom plays each note from the sound whose root is nearest", "[bloom]")
{
    Rig rig;
    rig.engine.loadBloomZones({ { sine(110.0f), 45.0f }, { sine(440.0f), 69.0f } });
    rig.engine.setParam(engine::P::BloomTransform, 5.0f);
    rig.engine.setParam(engine::P::BloomAmount, 0.0f);
    rig.engine.setParam(engine::P::BloomGravity, 0.0f);
    rig.engine.setParam(engine::P::BloomTone, 1.0f);
    rig.engine.setParam(engine::P::BloomSendA, -60.0f);
    rig.engine.setParam(engine::P::BloomSendB, -60.0f);
    rig.engine.setParam(engine::P::ResLevel, -60.0f);
    rig.engine.setParam(engine::P::DroneLevel, -60.0f);
    rig.engine.setParam(engine::P::MasterFadeSecs, 0.5f);
    rig.engine.command(engine::Command::FadeIn);
    for (int b = 0; b < 400; ++b)
        rig.block();

    for (const auto& [note, hz] : { std::pair { 47, 110.0f * std::exp2(2.0f / 12.0f) }, std::pair { 67, 440.0f * std::exp2(-2.0f / 12.0f) } })
    {
        rig.engine.noteOn(note, 0.9f);
        for (int b = 0; b < 40; ++b)
            rig.block();
        CHECK(zeroCrossingRate(rig, 0.3) == Approx(hz).margin(hz * 0.03f));
        rig.engine.noteOff(note);
        for (int b = 0; b < 1500; ++b)
            rig.block();
    }
}
