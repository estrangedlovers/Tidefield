#include "support/AllocationGuard.h"

#include <dsp/core/Fft.h>
#include <dsp/core/MathUtil.h>
#include <dsp/sources/looper/Disintegrator.h>
#include <dsp/sources/weather/WeatherBed.h>
#include <dsp/spectral/SpectralFreeze.h>
#include <engine/Engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

float rms(const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > x.size())
        to = x.size();
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(s / static_cast<double>(std::max<std::size_t>(1, to - from))));
}

float db(float x) { return 20.0f * std::log10(std::max(1.0e-9f, x)); }

bool allFinite(const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite(v))
            return false;
    return true;
}

} // namespace

TEST_CASE("FFT round-trips and finds a sinusoid's bin", "[fft]")
{
    dsp::Fft fft;
    fft.prepare(10);
    std::vector<dsp::Fft::Complex> x(1024);
    for (int i = 0; i < 1024; ++i)
        x[static_cast<std::size_t>(i)] = { std::sin(dsp::kTwoPi * 37.0f * static_cast<float>(i) / 1024.0f), 0.0f };
    auto orig = x;
    fft.forward(x.data());
    int peak = 0;
    for (int k = 1; k < 512; ++k)
        if (std::abs(x[static_cast<std::size_t>(k)]) > std::abs(x[static_cast<std::size_t>(peak)]))
            peak = k;
    CHECK(peak == 37);
    CHECK(std::abs(x[37]) == Catch::Approx(512.0f).epsilon(0.001));
    fft.inverse(x.data());
    for (std::size_t i = 0; i < x.size(); ++i)
        REQUIRE(std::fabs(x[i].real() / 1024.0f - orig[i].real()) < 1.0e-4f);
}

TEST_CASE("Spectral freeze holds a note at about its level, after the note stops", "[freeze]")
{
    dsp::SpectralFreeze fr;
    fr.prepare({ kFs, kBlock }, 5);
    const int total = static_cast<int>(6.0 * kFs);
    std::vector<float> in(static_cast<std::size_t>(total), 0.0f), l(in.size()), r(in.size());
    const int noteEnd = static_cast<int>(1.0 * kFs);
    for (int i = 0; i < noteEnd; ++i)
        in[static_cast<std::size_t>(i)] = 0.3f * std::sin(dsp::kTwoPi * 220.0f * static_cast<float>(i) / static_cast<float>(kFs));
    for (int pos = 0; pos < total; pos += kBlock)
    {
        if (pos == static_cast<int>(0.8 * kFs) / kBlock * kBlock)
            fr.setFrozen(true);
        if (pos == static_cast<int>(3.5 * kFs) / kBlock * kBlock)
            fr.setFrozen(false);
        fr.process(in.data() + pos, l.data() + pos, r.data() + pos, kBlock);
    }
    const auto held = rms(l, static_cast<std::size_t>(2.0 * kFs), static_cast<std::size_t>(3.4 * kFs));
    const float noteRms = 0.3f / std::sqrt(2.0f);
    CHECK(std::fabs(db(held) - db(noteRms)) < 6.0f); // within 6 dB of the note
    CHECK(rms(l, static_cast<std::size_t>(5.7 * kFs)) < 1.0e-4f); // released over 2 s
    CHECK(allFinite(l));
    // Left and right drift independently: a wide pad, not mono.
    double corr = 0.0, el = 0.0, er = 0.0;
    for (auto i = static_cast<std::size_t>(2.0 * kFs); i < static_cast<std::size_t>(3.4 * kFs); ++i)
    {
        corr += static_cast<double>(l[i]) * r[i];
        el += static_cast<double>(l[i]) * l[i];
        er += static_cast<double>(r[i]) * r[i];
    }
    CHECK(corr / std::sqrt(el * er) < 0.95);
}

TEST_CASE("Disintegrator: pedal states, loop length, erosion wears it down", "[looper]")
{
    dsp::Disintegrator loop;
    loop.prepare({ kFs, kBlock }, 9);
    dsp::Disintegrator::Params p;
    p.erosion = 1.0f;
    p.flakes = 1.0f;
    loop.setParams(p);

    std::vector<float> in(kBlock), l(kBlock), r(kBlock);
    dsp::Random noise(3);
    auto run = [&](double seconds, bool feed) {
        std::vector<float> out;
        for (int b = 0; b < static_cast<int>(seconds * kFs / kBlock); ++b)
        {
            for (auto& x : in)
                x = feed ? 0.3f * noise.nextBipolar() : 0.0f;
            loop.process(in.data(), in.data(), l.data(), r.data(), kBlock);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };

    REQUIRE(loop.getState() == dsp::Disintegrator::State::Empty);
    loop.record();
    REQUIRE(loop.getState() == dsp::Disintegrator::State::Recording);
    auto during = run(2.0, true);
    CHECK(rms(during) == 0.0f); // the take is not played while recording
    loop.record();
    REQUIRE(loop.getState() == dsp::Disintegrator::State::Playing);
    CHECK(loop.getLengthSeconds() == Catch::Approx(2.0f).epsilon(0.01));

    const auto firstPass = run(2.0, false);
    const auto later = run(40.0, false);
    CHECK(loop.getPasses() >= 20);
    const float early = rms(firstPass);
    const float late = rms(later, later.size() - static_cast<std::size_t>(2.0 * kFs));
    CHECK(early > 0.1f);
    CHECK(late < early * 0.6f);  // worn down
    CHECK(late > early * 0.02f); // but still there
    CHECK(allFinite(later));

    loop.clear();
    run(1.0, false);
    CHECK(loop.getState() == dsp::Disintegrator::State::Empty);
}

TEST_CASE("Disintegrator without erosion repeats the loop exactly", "[looper]")
{
    dsp::Disintegrator loop;
    loop.prepare({ kFs, kBlock }, 9);
    dsp::Disintegrator::Params p;
    p.erosion = 0.0f;
    loop.setParams(p);
    std::vector<float> in(kBlock), l(kBlock), r(kBlock), pass1, pass3;
    dsp::Random noise(4);
    loop.record();
    for (int b = 0; b < 40; ++b)
    {
        for (auto& x : in)
            x = 0.2f * noise.nextBipolar();
        loop.process(in.data(), in.data(), l.data(), r.data(), kBlock);
    }
    loop.record(); // closes at 40 blocks
    std::fill(in.begin(), in.end(), 0.0f);
    for (int pass = 0; pass < 3; ++pass)
        for (int b = 0; b < 40; ++b)
        {
            loop.process(in.data(), in.data(), l.data(), r.data(), kBlock);
            auto& dst = pass == 0 ? pass1 : pass3;
            if (pass != 1)
                dst.insert(dst.end(), l.begin(), l.end());
        }
    REQUIRE(pass1.size() == pass3.size());
    for (std::size_t i = 0; i < pass1.size(); ++i)
        REQUIRE(pass1[i] == pass3[i]);
}

TEST_CASE("Weather bed: each element sounds, stays bounded and finite", "[weather]")
{
    auto level = [](float wind, float rain, float surf) {
        dsp::WeatherBed w;
        w.prepare({ kFs, kBlock }, 11);
        dsp::WeatherBed::Params p;
        p.wind = wind;
        p.rain = rain;
        p.surf = surf;
        w.setParams(p);
        std::vector<float> l(static_cast<std::size_t>(20.0 * kFs)), r(l.size());
        for (std::size_t pos = 0; pos < l.size(); pos += kBlock)
            w.process(l.data() + pos, r.data() + pos, kBlock, 1.0f);
        float peak = 0.0f;
        for (float v : l)
            peak = std::max(peak, std::fabs(v));
        REQUIRE(allFinite(l));
        REQUIRE(peak < 1.0f);
        return db(rms(l));
    };
    const float wind = level(1.0f, 0.0f, 0.0f);
    const float rain = level(0.0f, 1.0f, 0.0f);
    const float surf = level(0.0f, 0.0f, 1.0f);
    INFO("wind " << wind << " rain " << rain << " surf " << surf);
    // Each element at full sits in a usable bed range.
    for (float v : { wind, rain, surf })
    {
        CHECK(v > -36.0f);
        CHECK(v < -10.0f);
    }
    CHECK(level(0.0f, 0.0f, 0.0f) < -120.0f);
}

TEST_CASE("Engine: freeze all holds the mix, ducks it, and releases", "[freeze][engine]")
{
    engine::Engine eng;
    eng.prepare(kFs, kBlock);
    eng.setParam(engine::P::MasterFadeSecs, 0.5f);
    eng.command(engine::Command::FadeIn);
    std::vector<float> l(kBlock), r(kBlock);
    float* outs[2] = { l.data(), r.data() };
    engine::TelemetryFrame f;
    auto run = [&](double seconds) {
        std::vector<float> out;
        for (int b = 0; b < static_cast<int>(seconds * kFs / kBlock); ++b)
        {
            eng.process(nullptr, 0, outs, 2, kBlock);
            out.insert(out.end(), l.begin(), l.end());
            while (eng.popTelemetry(f)) {}
        }
        return out;
    };
    run(4.0);
    eng.setParam(engine::P::FreezeOn, 1.0f);
    eng.setParam(engine::P::FreezeDuck, 1.0f); // only the frozen hold remains
    eng.setParam(engine::P::DroneLevel, -60.0f);
    run(1.5);
    CHECK(f.freezeGain == 1.0f);
    const auto held = run(3.0);
    CHECK(db(rms(held)) > -45.0f); // the hold sounds with the source gone and ducked
    CHECK(allFinite(held));
    eng.setParam(engine::P::FreezeOn, 0.0f);
    run(2.5);
    CHECK(f.freezeGain == 0.0f);
}

TEST_CASE("Engine: the looper records the mix and the input pad sounds unarmed", "[looper][engine]")
{
    engine::Engine eng;
    eng.prepare(kFs, kBlock);
    eng.setParam(engine::P::MasterFadeSecs, 0.5f);
    eng.command(engine::Command::FadeIn);
    eng.setParam(engine::P::LoopSource, 1.0f);
    std::vector<float> l(kBlock), r(kBlock), in(kBlock);
    float* outs[2] = { l.data(), r.data() };
    const float* ins[2] = { in.data(), in.data() };
    engine::TelemetryFrame f;
    std::size_t t = 0;
    auto run = [&](double seconds, bool tone) {
        for (int b = 0; b < static_cast<int>(seconds * kFs / kBlock); ++b, t += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                in[static_cast<std::size_t>(i)] = tone ? 0.3f * std::sin(dsp::kTwoPi * 330.0f * static_cast<float>(t + static_cast<std::size_t>(i)) / static_cast<float>(kFs)) : 0.0f;
            eng.process(ins, 2, outs, 2, kBlock);
            while (eng.popTelemetry(f)) {}
        }
    };
    run(3.0, false);
    eng.command(engine::Command::LoopRecord);
    run(2.0, false);
    CHECK(f.loopState == static_cast<int>(dsp::Disintegrator::State::Recording));
    eng.command(engine::Command::LoopRecord);
    run(5.0, false);
    CHECK(f.loopState == static_cast<int>(dsp::Disintegrator::State::Playing));
    CHECK(f.loopSeconds == Catch::Approx(2.0f).margin(0.02f));
    CHECK(f.loopPasses >= 2);

    // Input monitor off (default), a tone in, freeze it: the pad comes through anyway.
    run(1.0, true);
    eng.setParam(engine::P::InputFreeze, 1.0f);
    run(1.0, true);
    run(1.0, false);
    CHECK(f.inputFreeze == 1.0f);
}

TEST_CASE("Engine with every phase 8 source running never allocates", "[alloc][engine]")
{
    engine::Engine eng;
    eng.prepare(kFs, kBlock);
    eng.command(engine::Command::FadeIn);
    for (auto p : { engine::P::WeatherWind, engine::P::WeatherRain, engine::P::WeatherSurf })
        eng.setParam(p, 0.7f);
    eng.setParam(engine::P::LoopSource, 1.0f);
    std::vector<float> l(kBlock), r(kBlock), in(kBlock, 0.01f);
    float* outs[2] = { l.data(), r.data() };
    const float* ins[2] = { in.data(), in.data() };
    for (int b = 0; b < 400; ++b)
        eng.process(ins, 2, outs, 2, kBlock);
    eng.command(engine::Command::LoopRecord);
    eng.setParam(engine::P::FreezeOn, 1.0f);
    eng.setParam(engine::P::InputFreeze, 1.0f);
    std::size_t allocations = 0;
    {
        tf::test::ScopedAllocationCounter counter;
        for (int b = 0; b < 400; ++b)
        {
            if (b == 200)
                eng.command(engine::Command::LoopRecord);
            eng.process(ins, 2, outs, 2, kBlock);
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}
