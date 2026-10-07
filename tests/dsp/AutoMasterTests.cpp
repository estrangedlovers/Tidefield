#include <dsp/core/MathUtil.h>
#include <dsp/core/Random.h>
#include <dsp/filters/OnePole.h>
#include <dsp/master/AutoMaster.h>
#include <engine/Engine.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::dsp;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

/** Feeds `seconds` of noise (optionally low-passed) at `gain` and returns the state. */
AutoMaster::State run(AutoMaster& am, double seconds, float gain, float lowpassHz = 0.0f, std::uint64_t seed = 1)
{
    Random rng(seed);
    OnePole lpL, lpR;
    lpL.prepare(kFs);
    lpR.prepare(kFs);
    if (lowpassHz > 0.0f)
    {
        lpL.setCutoff(lowpassHz);
        lpR.setCutoff(lowpassHz);
    }
    std::vector<float> l(kBlock), r(kBlock);
    for (int b = 0; b < static_cast<int>(seconds * kFs / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            float a = rng.nextBipolar() * gain, c = rng.nextBipolar() * gain;
            if (lowpassHz > 0.0f)
            {
                a = lpL.processLow(a) * 3.0f;
                c = lpR.processLow(c) * 3.0f;
            }
            l[static_cast<std::size_t>(i)] = a;
            r[static_cast<std::size_t>(i)] = c;
        }
        am.process(l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            REQUIRE(std::isfinite(l[static_cast<std::size_t>(i)]));
    }
    return am.getState();
}

} // namespace

TEST_CASE("Auto master brings a quiet mix to the loudness target", "[automaster]")
{
    AutoMaster am;
    am.prepare({ kFs, kBlock });
    AutoMaster::Params p;
    p.enabled = true;
    p.targetLufs = -16.0f;
    am.setParams(p);
    const auto s = run(am, 30.0, 0.08f);
    INFO("loudness " << s.loudness << " gain " << s.gainDb);
    CHECK(s.loudness < -20.0f);                                 // the input really is quiet
    CHECK(std::fabs(s.loudness + s.gainDb - (-16.0f)) < 1.5f);  // and leaves at the target
    CHECK(s.mix == 1.0f);
}

TEST_CASE("Auto master tames a bright mix and opens a dark one", "[automaster]")
{
    AutoMaster bright, dark;
    AutoMaster::Params p;
    p.enabled = true;
    p.amount = 1.0f;
    for (auto* am : { &bright, &dark })
    {
        am->prepare({ kFs, kBlock });
        am->setParams(p);
    }
    const auto b = run(bright, 25.0, 0.1f);            // white noise: very bright for ambient
    const auto d = run(dark, 25.0, 0.1f, 600.0f);      // muffled
    INFO("bright high " << b.highDb << " dark high " << d.highDb);
    CHECK(b.highDb < -2.0f);
    CHECK(d.highDb > 2.0f);
    CHECK(d.highDb <= 4.0f); // boosts are capped: never drags up what is not there
}

TEST_CASE("Auto master never raises silence and passes audio untouched when off", "[automaster]")
{
    AutoMaster am;
    am.prepare({ kFs, kBlock });
    AutoMaster::Params p;
    p.enabled = true;
    am.setParams(p);
    const auto s = run(am, 15.0, 1.0e-6f); // hiss at -120 dBFS
    CHECK(std::fabs(s.gainDb) < 0.01f);

    AutoMaster off;
    off.prepare({ kFs, kBlock });
    Random rng(3);
    std::vector<float> l(kBlock), r(kBlock), l0, r0;
    for (int i = 0; i < kBlock; ++i)
    {
        l[static_cast<std::size_t>(i)] = rng.nextBipolar();
        r[static_cast<std::size_t>(i)] = rng.nextBipolar();
    }
    l0 = l;
    r0 = r;
    off.process(l.data(), r.data(), kBlock);
    CHECK(l == l0);
    CHECK(r == r0);
}

TEST_CASE("Switching the auto master crossfades without a jump", "[automaster]")
{
    AutoMaster am;
    am.prepare({ kFs, kBlock });
    AutoMaster::Params p;
    p.enabled = false;
    am.setParams(p);
    std::vector<float> l(kBlock), r(kBlock), out;
    auto sine = [&](int b) {
        for (int i = 0; i < kBlock; ++i)
        {
            const float t = static_cast<float>(b * kBlock + i) / static_cast<float>(kFs);
            l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = 0.05f * std::sin(kTwoPi * 220.0f * t);
        }
    };
    for (int b = 0; b < 400; ++b)
    {
        if (b == 100)
        {
            p.enabled = true;
            am.setParams(p);
        }
        sine(b);
        am.process(l.data(), r.data(), kBlock);
        out.insert(out.end(), l.begin(), l.end());
    }
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < out.size(); ++i)
        maxStep = std::max(maxStep, std::fabs(out[i] - out[i - 1]));
    // A 220 Hz sine at its final level moves at most 2*pi*220/fs*amp per sample.
    float peak = 0.0f;
    for (float v : out)
        peak = std::max(peak, std::fabs(v));
    CHECK(maxStep < 1.2f * kTwoPi * 220.0f / static_cast<float>(kFs) * peak);
}

TEST_CASE("Engine with the auto master on stays under the ceiling", "[automaster][engine]")
{
    tf::engine::Engine e;
    e.prepare(kFs, kBlock);
    e.setParam(tf::engine::P::MasterAuto, 1.0f);
    e.setParam(tf::engine::P::MasterAutoTarget, 2.0f); // loud
    e.setParam(tf::engine::P::MasterFadeSecs, 0.5f);
    e.command(tf::engine::Command::FadeIn);
    std::vector<float> l(kBlock), r(kBlock);
    float* outs[2] = { l.data(), r.data() };
    float peak = 0.0f;
    tf::engine::TelemetryFrame f;
    for (int b = 0; b < static_cast<int>(20.0 * kFs / kBlock); ++b)
    {
        e.process(nullptr, 0, outs, 2, kBlock);
        for (int i = 0; i < kBlock; ++i)
            peak = std::max({ peak, std::fabs(l[static_cast<std::size_t>(i)]), std::fabs(r[static_cast<std::size_t>(i)]) });
        while (e.popTelemetry(f)) {}
    }
    CHECK(peak <= tf::dsp::dbToGain(-1.0f) + 1.0e-6f);
    CHECK(f.autoMaster[7] == 1.0f);   // fully on
    CHECK(f.autoMaster[1] > 3.0f);    // it had to lift the default drone toward -14 LUFS
}

TEST_CASE("Auto master glue only rounds off peaks of a steady mix", "[automaster]")
{
    AutoMaster am;
    am.prepare({ kFs, kBlock });
    AutoMaster::Params p;
    p.enabled = true;
    am.setParams(p);
    float maxGr = 0.0f;
    for (int k = 0; k < 20; ++k)
    {
        const auto s = run(am, 1.0, 0.1f, 0.0f, static_cast<std::uint64_t>(k + 9));
        if (k >= 5)
            maxGr = std::max(maxGr, s.reductionDb); // after the meters settle
        CHECK(s.reductionDb <= 6.0f);              // never more than the cap, even at the start
    }
    CHECK(maxGr < 2.0f); // steady noise is not squashed
}
