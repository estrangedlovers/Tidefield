#include <dsp/core/TempoSync.h>
#include <dsp/fx/delay/TapeDelay.h>
#include <engine/Engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf;
using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

/** Runs the engine and returns the beat positions (from the host's song position)
    at which any loop fired. */
std::vector<double> loopFires(engine::Engine& e, double fromPpq, double toPpq, double bpm, bool host)
{
    std::vector<float> l(kBlock), r(kBlock);
    float* outs[2] = { l.data(), r.data() };
    std::vector<double> fires;
    double ppq = fromPpq;
    std::array<float, 8> lastFlash {};
    engine::TelemetryFrame f;
    while (ppq < toPpq)
    {
        if (host)
            e.setHostTransport(bpm, ppq, true);
        e.process(nullptr, 0, outs, 2, kBlock);
        while (e.popTelemetry(f))
            for (std::size_t k = 0; k < 8; ++k)
            {
                // A flash jumps to 1 when a loop fires and decays with a 0.4 s time
                // constant, so its level tells how long ago that was.
                if (f.loopFlash[k] > lastFlash[k] + 0.05f)
                {
                    const double ago = std::log(1.0 / std::max(1.0e-3f, f.loopFlash[k])) * 0.4;
                    fires.push_back(ppq + kBlock / kFs * bpm / 60.0 - ago * bpm / 60.0);
                }
                lastFlash[k] = f.loopFlash[k];
            }
        ppq += kBlock / kFs * bpm / 60.0;
    }
    return fires;
}

void syncedLoops(engine::Engine& e)
{
    e.prepare(kFs, kBlock);
    e.setParam(engine::P::SyncOn, 1.0f);
    e.setParam(engine::P::LoopsOn, 1.0f);
    e.setParam(engine::P::LoopsCount, 8.0f);
    e.setParam(engine::P::LoopsDensity, 1.0f);
    e.setParam(engine::P::LoopsRate, 4.0f); // periods / 4: more fires to check
}

} // namespace

TEST_CASE("Beat divisions snap a free time to the nearest musical length", "[tempo]")
{
    const float beat = 0.5f; // 120 BPM
    CHECK(dsp::syncedSeconds(0.47f, beat, 2.0f) == Approx(0.5f));   // a quarter
    CHECK(dsp::syncedSeconds(0.26f, beat, 2.0f) == Approx(0.25f));  // an eighth
    CHECK(dsp::syncedSeconds(1.9f, beat, 2.0f) == Approx(2.0f));    // a bar
    CHECK(dsp::syncedSeconds(5.0f, beat, 2.0f) == Approx(2.0f));    // longest that fits
    CHECK(dsp::syncedSeconds(0.47f, 0.0f, 2.0f) == Approx(0.47f));  // not synced
}

TEST_CASE("A synced tape delay repeats on the beat", "[tempo]")
{
    dsp::TapeDelay d;
    d.prepare({ kFs, 512 });
    // Wobble off; the free time (0.47 s) snaps to a quarter at 120 BPM (0.5 s).
    dsp::ModContext ctx;
    ctx.beatSeconds = 0.5f;
    const float time01 = std::log(470.0f / 20.0f) / std::log(100.0f);
    for (int i = 0; i < 400; ++i) // let the time glide settle
    {
        d.setControls({ time01, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f }, ctx);
        std::vector<float> zl(512), zr(512);
        d.process(zl.data(), zr.data(), 512);
    }
    std::vector<float> l(static_cast<std::size_t>(kFs), 0.0f), r(l.size(), 0.0f);
    l[0] = r[0] = 1.0f;
    d.process(l.data(), r.data(), static_cast<int>(l.size()));
    std::size_t peak = 1;
    for (std::size_t i = 1; i < l.size(); ++i)
        if (std::abs(l[i]) > std::abs(l[peak]))
            peak = i;
    CHECK(static_cast<double>(peak) / kFs == Approx(0.5).margin(0.003));
}

TEST_CASE("Synced loops fire on whole beats and follow the host's song position", "[tempo]")
{
    engine::Engine a;
    syncedLoops(a);
    const auto fires = loopFires(a, 0.0, 200.0, 120.0, true);
    REQUIRE(fires.size() > 10);
    // Every note lands on a beat (within a block of 256 samples, 0.01 beat at 120 BPM).
    for (double p : fires)
        CHECK(std::abs(p - std::round(p)) < 0.02);

    // A second run that starts mid-song (the DAW's play head placed at beat 100)
    // plays the same notes from there on: the loops follow the song, not the clock.
    engine::Engine b;
    syncedLoops(b);
    const auto later = loopFires(b, 100.0, 200.0, 120.0, true);
    std::vector<double> expected;
    for (double p : fires)
        if (p > 100.5)
            expected.push_back(std::round(p));
    std::vector<double> got;
    for (double p : later)
        if (p > 100.5)
            got.push_back(std::round(p));
    CHECK(got == expected);
}

TEST_CASE("Without a host the Tempo parameter drives the beat clock", "[tempo]")
{
    engine::Engine e;
    syncedLoops(e);
    e.setParam(engine::P::SyncBpm, 150.0f);
    std::vector<float> l(kBlock), r(kBlock);
    float* outs[2] = { l.data(), r.data() };
    engine::TelemetryFrame f;
    for (int b = 0; b < 200; ++b)
    {
        e.process(nullptr, 0, outs, 2, kBlock);
        while (e.popTelemetry(f)) {}
    }
    CHECK(f.syncOn);
    CHECK_FALSE(f.hostTempo);
    CHECK(f.bpm == Approx(150.0f));
}
