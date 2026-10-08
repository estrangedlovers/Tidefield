#include "support/AllocationGuard.h"

#include <dsp/core/MathUtil.h>
#include <dsp/sources/bloom/BloomSampler.h>
#include <engine/Engine.h>
#include <engine/capture/CatchManager.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::engine;
using tf::dsp::BloomSampler;
using Catch::Approx;

namespace {
constexpr double kFs = 48000.0;

std::shared_ptr<tf::dsp::SampleBuffer> sine(float hz, float seconds, float decay = 0.0f)
{
    auto b = std::make_shared<tf::dsp::SampleBuffer>();
    b->sampleRate = kFs;
    b->left.resize(static_cast<size_t>(seconds * kFs));
    for (size_t i = 0; i < b->left.size(); ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kFs);
        b->left[i] = 0.6f * std::sin(tf::dsp::kTwoPi * hz * t) * std::exp(-decay * t);
    }
    return b;
}

struct Rig
{
    Engine engine;
    CatchManager catcher { engine };
    TelemetryFrame last;
    std::vector<std::string> rejected;
    std::vector<int> caughtInto;

    Rig()
    {
        engine.prepare(kFs, 256);
        engine.setParam(P::MasterFadeSecs, 0.5f);
        engine.command(Command::FadeIn);
        catcher.onRejected = [this](const std::string& r) { rejected.push_back(r); };
        catcher.onCaught = [this](int cloud, const std::string&) { caughtInto.push_back(cloud); };
    }

    std::vector<float> run(double seconds, const std::vector<float>* input = nullptr)
    {
        const int total = static_cast<int>(seconds * kFs);
        std::vector<float> outL(static_cast<size_t>(total)), outR(static_cast<size_t>(total)), in(256);
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min(256, total - pos);
            float* outs[2] = { outL.data() + pos, outR.data() + pos };
            const float* ins[2] = { in.data(), in.data() };
            if (input != nullptr)
                for (int i = 0; i < n; ++i)
                    in[static_cast<size_t>(i)] = (*input)[static_cast<size_t>(pos + i) % input->size()];
            engine.process(input != nullptr ? ins : nullptr, input != nullptr ? 2 : 0, outs, 2, n);
            TelemetryFrame f;
            while (engine.popTelemetry(f))
                last = f;
            EngineNotice note;
            while (engine.popNotice(note))
                catcher.handle(note);
            engine.collectGarbage();
        }
        return outL;
    }
};

float peakOf(const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max(p, std::fabs(v));
    return p;
}
}

TEST_CASE("Catch captures the master into the first empty cloud, normalised and faded")
{
    Rig rig;
    rig.run(7.0);
    rig.engine.setParam(P::CatchSeconds, 5.0f);
    rig.engine.command(Command::Catch);
    rig.run(0.2);

    REQUIRE(rig.caughtInto == std::vector<int> { 0 });
    const auto caught = rig.engine.getCloudSample(0);
    REQUIRE(caught != nullptr);
    REQUIRE(caught->isStereo());
    REQUIRE(static_cast<double>(caught->size()) == Approx(5.0 * kFs).margin(1));
    REQUIRE(caught->name == "Catch 1");
    REQUIRE(caught->left.front() == 0.0f);
    REQUIRE(caught->left.back() == 0.0f);
    float peak = std::max(peakOf(caught->left), peakOf(caught->right));
    REQUIRE(peak == Approx(tf::dsp::dbToGain(CatchManager::kTargetPeakDb)).epsilon(0.01));

    rig.engine.command(Command::Catch);
    rig.run(0.2);
    rig.engine.setParam(P::CatchTarget, 4.0f);
    rig.engine.command(Command::Catch);
    rig.run(0.2);
    REQUIRE(rig.caughtInto == std::vector<int> { 0, 1, 3 });
    REQUIRE(rig.last.cloudLoaded[0]);
}

TEST_CASE("Catching silence is refused, and the live input can be caught")
{
    Rig rig;
    for (const auto& s : kStrips)
        rig.engine.setParam(s.level, -60.0f);
    rig.engine.setParam(P::BusALevel, -60.0f);
    rig.engine.setParam(P::BusBLevel, -60.0f);
    rig.engine.setParam(P::ResRain, 0.0f);
    rig.run(12.0);
    rig.engine.setParam(P::CatchSeconds, 5.0f);
    rig.engine.command(Command::Catch);
    rig.run(0.2);
    REQUIRE(rig.caughtInto.empty());
    REQUIRE(rig.rejected.size() == 1);

    std::vector<float> input(4800);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.2f * std::sin(tf::dsp::kTwoPi * 220.0f * static_cast<float>(i) / static_cast<float>(kFs));
    rig.run(6.0, &input);
    rig.engine.setParam(P::CatchSource, 1.0f);
    rig.engine.command(Command::Catch);
    rig.run(0.2, &input);
    REQUIRE(rig.caughtInto.size() == 1);
    const auto caught = rig.engine.getCloudSample(rig.caughtInto[0]);
    REQUIRE_FALSE(caught->isStereo());
    REQUIRE(caught->name == "Catch 1 (input)");
}

TEST_CASE("A catch handled too late is detected instead of reading torn audio")
{
    Engine engine;
    engine.prepare(kFs, 512);
    engine.command(Command::FadeIn);
    std::vector<float> l(512), r(512);
    float* outs[2] = { l.data(), r.data() };
    for (int i = 0; i < 48000 * 6 / 512; ++i)
        engine.process(nullptr, 0, outs, 2, 512);
    engine.setParam(P::CatchSeconds, 5.0f);
    engine.command(Command::Catch);
    engine.process(nullptr, 0, outs, 2, 512);
    EngineNotice notice, n;
    while (engine.popNotice(n))
        if (n.type == EngineNotice::Type::CatchReady)
            notice = n;
    REQUIRE(notice.type == EngineNotice::Type::CatchReady);

    tf::dsp::SampleBuffer ok;
    REQUIRE(engine.copyCatch(notice, ok));

    for (int i = 0; i < 48000 * 40 / 512; ++i)
        engine.process(nullptr, 0, outs, 2, 512);
    tf::dsp::SampleBuffer late;
    REQUIRE_FALSE(engine.copyCatch(notice, late));
}

TEST_CASE("Every Bloom transform sounds, stays bounded, and ends")
{
    const auto sample = sine(440.0f, 2.0f, 1.5f);
    tf::dsp::HarmonicGravity h;
    h.snapTo({ tf::dsp::kScaleTypes[0].mask, 0 });
    for (int t = 0; t < BloomSampler::kNumTransforms; ++t)
    {
        BloomSampler b;
        b.prepare({ kFs, 512 }, 3);
        b.setHarmony(&h);
        b.setBuffer(sample.get());
        BloomSampler::Params p;
        p.transform = static_cast<BloomSampler::Transform>(t);
        p.lengthSeconds = 3.0f;
        p.releaseSeconds = 1.0f;
        p.rootNote = 69.0f;
        b.setParams(p);
        b.noteOn(69, 1.0f);
        b.noteOff(69);

        std::vector<float> l(512), r(512);
        double energy = 0.0;
        float peak = 0.0f;
        int blocks = 0;
        while (! b.isSilent() && blocks < 48000 * 20 / 512)
        {
            b.process(l.data(), r.data(), 512, 1.0f);
            for (size_t i = 0; i < 512; ++i)
            {
                REQUIRE(std::isfinite(l[i]));
                energy += static_cast<double>(l[i]) * l[i];
                peak = std::max(peak, std::fabs(l[i]));
            }
            ++blocks;
        }
        INFO(BloomSampler::transformName(static_cast<BloomSampler::Transform>(t)));
        REQUIRE(energy > 1.0);
        REQUIRE(peak < 2.0f);
        REQUIRE(b.isSilent());
        REQUIRE(blocks * 512 > 48000);
    }
}

TEST_CASE("Bloom Tape plays an octave up at twice the speed")
{
    const auto sample = sine(220.0f, 2.0f);
    BloomSampler b;
    b.prepare({ kFs, 512 }, 4);
    b.setBuffer(sample.get());
    BloomSampler::Params p;
    p.transform = BloomSampler::Transform::Tape;
    p.amount = 0.0f;
    p.rootNote = 57.0f;
    p.gravity = 0.0f;
    p.random = 0.0f;
    p.tone = 1.0f;
    b.setParams(p);
    b.noteOn(69, 1.0f);
    std::vector<float> l(24000), r(24000);
    b.process(l.data(), r.data(), 24000, 1.0f);
    int crossings = 0;
    for (size_t i = 4800; i < 24000; ++i)
        crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
    REQUIRE(crossings / 2.0 / 0.4 == Approx(440.0).margin(5.0));
}

TEST_CASE("Bloom steals voices beyond eight and never allocates")
{
    const auto sample = sine(330.0f, 1.0f);
    BloomSampler b;
    b.prepare({ kFs, 512 }, 5);
    b.setBuffer(sample.get());
    BloomSampler::Params p;
    p.transform = BloomSampler::Transform::Freeze;
    p.lengthSeconds = 10.0f;
    b.setParams(p);
    std::vector<float> l(512), r(512);
    std::size_t allocations = 0;
    {
        const tf::test::ScopedAllocationCounter counter;
        for (int n = 0; n < 20; ++n)
        {
            b.noteOn(48 + n, 0.8f);
            b.process(l.data(), r.data(), 512, 1.0f);
            REQUIRE(b.getActiveVoices() <= BloomSampler::kMaxVoices);
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
    REQUIRE(b.getActiveVoices() == BloomSampler::kMaxVoices);
}

TEST_CASE("Notes posted to the engine play Bloom through its strip")
{
    Rig rig;
    for (const auto& s : kStrips)
        rig.engine.setParam(s.level, -60.0f);
    rig.engine.setParam(P::BloomLevel, 0.0f);
    rig.engine.setParam(P::ResRain, 0.0f);
    REQUIRE(rig.engine.loadBloomSample(sine(440.0f, 2.0f, 1.0f)));
    rig.run(0.5);
    REQUIRE(rig.last.bloomLoaded);
    rig.engine.noteOn(60, 1.0f);
    const auto out = rig.run(2.0);
    REQUIRE(peakOf(out) > 0.01f);
    int active = 0;
    for (const auto& v : rig.last.bloomVoices)
        active += v.active ? 1 : 0;
    REQUIRE(active == 1);
}
