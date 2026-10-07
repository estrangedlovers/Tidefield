#include "support/AllocationGuard.h"

#include <dsp/core/MathUtil.h>
#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
#include <engine/scene/SceneManager.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::engine;
using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;

struct Rig
{
    Engine engine;
    FxManager fx { engine };

    explicit Rig(int block = 256)
    {
        engine.prepare(kFs, block);
        engine.setParam(P::MasterFadeSecs, 0.5f);
        engine.command(Command::FadeIn);
    }

    /** Renders `seconds`, optionally feeding `input` as the live input. Returns left. */
    std::vector<float> run(double seconds, const std::vector<float>* input = nullptr, TelemetryFrame* last = nullptr, int block = 256)
    {
        const int total = static_cast<int>(seconds * kFs);
        std::vector<float> outL(static_cast<size_t>(total)), outR(static_cast<size_t>(total));
        std::vector<float> inBuf(static_cast<size_t>(block));
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min(block, total - pos);
            float* outs[2] = { outL.data() + pos, outR.data() + pos };
            const float* ins[2] = { inBuf.data(), inBuf.data() };
            if (input != nullptr)
                for (int i = 0; i < n; ++i)
                    inBuf[static_cast<size_t>(i)] = (*input)[static_cast<size_t>(pos + i) % input->size()];
            engine.process(input != nullptr ? ins : nullptr, input != nullptr ? 2 : 0, outs, 2, n);
            TelemetryFrame f;
            while (engine.popTelemetry(f))
                if (last != nullptr)
                    *last = f;
            EngineNotice note;
            while (engine.popNotice(note)) {}
            fx.tick();
            engine.collectGarbage();
        }
        return outL;
    }

    void muteAllStrips()
    {
        for (const auto& s : kStrips)
            engine.setParam(s.level, -60.0f);
    }
};

float rms(const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(s / static_cast<double>(to - from)));
}

std::unique_ptr<tf::dsp::SampleBuffer> tone(float hz, float seconds)
{
    auto b = std::make_unique<tf::dsp::SampleBuffer>();
    b->sampleRate = kFs;
    b->left.resize(static_cast<size_t>(seconds * kFs));
    for (size_t i = 0; i < b->left.size(); ++i)
        b->left[i] = 0.5f * std::sin(tf::dsp::kTwoPi * hz * static_cast<float>(i) / static_cast<float>(kFs));
    return b;
}

} // namespace

TEST_CASE("Default FX layout: reverb on bus A, delay on bus B")
{
    Rig rig;
    rig.fx.loadDefaultLayout();
    REQUIRE(rig.fx.getType(kBusASlot) == "tf.reverb");
    REQUIRE(rig.fx.getType(kBusBSlot) == "tf.delay");
    REQUIRE(rig.fx.getInfo(kBusASlot) != nullptr);
    REQUIRE(rig.fx.getType(0).empty());
    REQUIRE(FxManager::findSlot("busB.fx1") == kBusBSlot);
}

TEST_CASE("Reverb send keeps sounding after the source stops")
{
    Rig rig;
    rig.fx.loadDefaultLayout();
    rig.engine.setParam(P::ResLevel, -60.0f);
    rig.engine.setParam(P::DroneSendA, 0.0f); // full send
    rig.run(4.0);
    rig.engine.setParam(P::DroneLevel, -60.0f); // dry gone, sends are post-fader...
    auto withSend = rig.run(0.3);

    Rig dry;
    dry.engine.setParam(P::ResLevel, -60.0f);
    dry.engine.setParam(P::DroneSendA, -60.0f);
    dry.run(4.0);
    dry.engine.setParam(P::DroneLevel, -60.0f);
    auto withoutSend = dry.run(0.3);

    // Post-fader sends: muting the strip also stops new input to the bus, but the
    // reverb tail continues while the dry signal is gone.
    REQUIRE(rms(withSend, 9600, withSend.size()) > rms(withoutSend, 9600, withoutSend.size()) * 10.0f);
}

TEST_CASE("Swapping FX types mid-signal crossfades without clicks or NaN")
{
    Rig rig;
    rig.fx.loadDefaultLayout();
    rig.run(1.0);
    float worst = 0.0f, last = 0.0f;
    for (int round = 0; round < 6; ++round)
    {
        const char* types[] = { "tf.medium", "", "tf.delay", "tf.reverb", "tf.medium", "" };
        rig.fx.setType(0, types[round]); // drone insert 1
        rig.fx.setType(kMasterSlot, types[(round + 2) % 6]);
        const auto out = rig.run(0.25);
        for (float x : out)
        {
            REQUIRE(std::isfinite(x));
            worst = std::max(worst, std::fabs(x - last));
            last = x;
        }
    }
    REQUIRE(worst < 0.25f);
}

TEST_CASE("Clouds play a loaded sample and swap samples smoothly")
{
    Rig rig;
    rig.muteAllStrips();
    rig.engine.setParam(P::Cloud1Level, 0.0f);
    rig.engine.setParam(P::Cloud1Density, 30.0f);
    REQUIRE(rig.engine.loadCloudSample(0, tone(220.0f, 2.0f)));
    TelemetryFrame f;
    auto out = rig.run(2.0, nullptr, &f);
    REQUIRE(f.cloudLoaded[0]);
    REQUIRE_FALSE(f.cloudLoaded[1]);
    REQUIRE(f.cloudGrainCount[0] > 0);
    REQUIRE(f.cloudGrainViews[0] > 0);
    REQUIRE(rms(out, 48000, out.size()) > 0.01f);

    REQUIRE(rig.engine.loadCloudSample(0, tone(330.0f, 2.0f)));
    out = rig.run(1.0);
    float worst = 0.0f;
    for (size_t i = 1; i < out.size(); ++i)
        worst = std::max(worst, std::fabs(out[i] - out[i - 1]));
    REQUIRE(worst < 0.1f);
}

TEST_CASE("Live input is silent until armed but always excites the resonator")
{
    std::vector<float> input(4800);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.3f * std::sin(tf::dsp::kTwoPi * 196.0f * static_cast<float>(i) / static_cast<float>(kFs));

    Rig rig;
    rig.muteAllStrips();
    rig.engine.setParam(P::InputLevel, 0.0f);
    rig.engine.setParam(P::ResRain, 0.0f);
    TelemetryFrame f;
    auto out = rig.run(1.0, &input, &f);
    REQUIRE(rms(out, 24000, out.size()) < 1.0e-5f); // not armed
    REQUIRE(f.inputLevel > 0.2f);

    rig.engine.setParam(P::InputArmed, 1.0f);
    out = rig.run(1.0, &input);
    REQUIRE(rms(out, 24000, out.size()) > 0.05f);

    // Resonator fed from the input, with the input itself disarmed.
    rig.engine.setParam(P::InputArmed, 0.0f);
    rig.engine.setParam(P::ResLevel, 0.0f);
    rig.engine.setParam(P::ResExciteInput, 1.0f);
    out = rig.run(2.0, &input);
    REQUIRE(rms(out, 48000, out.size()) > 1.0e-3f);
}

TEST_CASE("Key changes migrate drone voices gradually into the new key")
{
    Rig rig;
    rig.engine.setParam(P::DroneDensity, 6.0f);
    rig.engine.setParam(P::DroneEvolve, 0.0f);
    rig.engine.setParam(P::HarmonyGravity, 1.0f);
    rig.engine.setParam(P::HarmonyScale, 10.0f); // fifths only: root and 5th
    rig.engine.setParam(P::HarmonyRoot, 2.0f);   // D
    rig.engine.setParam(P::HarmonyMorph, 10.0f);
    TelemetryFrame f;
    rig.run(4.0, nullptr, &f);
    auto inKey = [](float note, int root) {
        const int pc = ((static_cast<int>(std::lround(note)) - root) % 12 + 12) % 12;
        return pc == 0 || pc == 7;
    };
    for (int v = 0; v < 6; ++v)
        REQUIRE(inKey(f.droneVoiceNote[static_cast<size_t>(v)], 2));

    rig.engine.setParam(P::HarmonyRoot, 3.0f); // D#: every D-fifths note must move
    rig.run(5.0, nullptr, &f);
    REQUIRE(f.harmonyMorph > 0.3f);
    REQUIRE(f.harmonyMorph < 0.7f);
    int moved = 0;
    for (int v = 0; v < 6; ++v)
        moved += inKey(f.droneVoiceNote[static_cast<size_t>(v)], 3);
    REQUIRE(moved >= 1);
    REQUIRE(moved <= 5);

    rig.run(9.0, nullptr, &f);
    REQUIRE(f.harmonyMorph == 1.0f);
    for (int v = 0; v < 6; ++v)
        REQUIRE(inKey(f.droneVoiceNote[static_cast<size_t>(v)], 3));
}

TEST_CASE("Tide speeds up and slows down autonomous motion")
{
    auto travel = [](float tideRate) {
        Rig rig;
        SceneManager scenes(rig.engine);
        rig.engine.setParam(P::TideRate, tideRate);
        rig.engine.setParam(P::TerrainWander, 1.0f);
        rig.engine.setParam(P::TerrainWanderRate, 0.1f);
        rig.run(2.0); // tide smoothing settles
        float distance = 0.0f;
        TelemetryFrame f, prev;
        rig.run(0.05, nullptr, &prev);
        for (int i = 0; i < 100; ++i)
        {
            rig.run(0.05, nullptr, &f);
            distance += std::hypot(f.position.x - prev.position.x, f.position.y - prev.position.y);
            prev = f;
        }
        return distance;
    };
    REQUIRE(travel(4.0f) > travel(0.25f) * 3.0f);
}

TEST_CASE("Medium on the master is heard even when every source is silent")
{
    Rig rig;
    rig.muteAllStrips();
    rig.engine.setParam(P::ResRain, 0.0f);
    auto out = rig.run(1.0);
    REQUIRE(rms(out, 24000, out.size()) < 1.0e-6f);

    rig.engine.setParam(P::MediumType, 2.0f); // vinyl
    rig.engine.setParam(P::MediumNoise, 1.0f);
    rig.engine.setParam(P::MediumAge, 0.8f);
    out = rig.run(2.0);
    REQUIRE(rms(out, 48000, out.size()) > tf::dsp::dbToGain(-80.0f));
}

TEST_CASE("Full engine with every source and slot active never allocates")
{
    Rig rig(512);
    rig.fx.loadDefaultLayout();
    for (int s = 0; s < kNumFxSlots; ++s)
        if (s != kBusASlot && s != kBusBSlot)
            rig.fx.setType(s, s % 2 == 0 ? "tf.medium" : "tf.delay");
    for (int k = 0; k < kNumClouds; ++k)
        rig.engine.loadCloudSample(k, tone(110.0f * static_cast<float>(k + 1), 1.0f));
    rig.engine.setParam(P::InputArmed, 1.0f);
    rig.engine.setParam(P::ResExciteInput, 1.0f);
    rig.engine.setParam(P::ResExciteClouds, 1.0f);
    rig.engine.setParam(P::MediumType, 1.0f);
    rig.engine.setParam(P::TerrainWander, 1.0f);
    std::vector<float> input(4800, 0.1f);
    rig.run(1.0, &input, nullptr, 512); // let snapshots, processors and buffers land

    std::vector<float> l(512), r(512), in(512, 0.2f);
    float* outs[2] = { l.data(), r.data() };
    const float* ins[2] = { in.data(), in.data() };
    rig.engine.setParam(P::HarmonyRoot, 7.0f);
    std::size_t allocations = 0;
    {
        const tf::test::ScopedAllocationCounter counter;
        for (int i = 0; i < 48000 * 3 / 512; ++i)
        {
            rig.engine.process(ins, 2, outs, 2, 512);
            TelemetryFrame f;
            while (rig.engine.popTelemetry(f)) {}
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
    for (float x : l)
        REQUIRE(std::isfinite(x));
}
