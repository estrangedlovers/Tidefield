#include <engine/Engine.h>
#include <engine/mod/SeasonManager.h>
#include <engine/scene/Wander.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <iostream>
#include <vector>

using namespace tf::engine;
using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

struct Rig
{
    Engine engine;
    TelemetryFrame f;
    std::vector<float> l = std::vector<float>(kBlock), r = std::vector<float>(kBlock);

    Rig()
    {
        engine.prepare(kFs, kBlock);
        engine.command(Command::FadeIn);
    }

    void run(double seconds)
    {
        float* outs[2] = { l.data(), r.data() };
        for (int b = 0; b < static_cast<int>(seconds * kFs / kBlock); ++b)
        {
            engine.process(nullptr, 0, outs, 2, kBlock);
            while (engine.popTelemetry(f)) {}
        }
    }
};

} // namespace

TEST_CASE("ParamState modulation offsets the value, not the target, and clears", "[mod]")
{
    ParamRegistry reg;
    ParamState ps;
    ps.prepare(reg, kFs);
    const auto cutoff = idx(P::DroneCutoff);
    const float base = ps.current(cutoff);
    ps.addModulation(cutoff, 0.2f);
    ps.advance(32);
    const auto& spec = reg.spec(cutoff);
    CHECK(ps.current(cutoff) == Approx(spec.fromNormalised(spec.toNormalised(base) + 0.2f)).epsilon(1e-4));
    CHECK(ps.target(cutoff) == Approx(base));
    ps.clearModulation();
    ps.advance(32);
    CHECK(ps.current(cutoff) == Approx(base));
    ps.addModulation(idx(P::MediumType), 0.5f); // discrete: ignored
    ps.advance(32);
    CHECK(ps.current(idx(P::MediumType)) == Approx(0.0f));
    ps.addModulation(cutoff, 5.0f); // clamps at the top of the range
    ps.advance(32);
    CHECK(ps.current(cutoff) == Approx(spec.maxValue));
}

TEST_CASE("Swell rises while held, ebbs after, and leaves targets alone", "[swell]")
{
    Rig rig;
    rig.engine.setParam(P::SwellAttack, 1.0f);
    rig.engine.setParam(P::SwellRelease, 2.0f);
    rig.run(0.5);
    const float sendTarget = rig.f.paramTargets[idx(P::DroneSendA)];
    rig.engine.setParam(P::SwellHold, 1.0f);
    rig.run(0.3);
    const float early = rig.f.swell;
    rig.run(1.5);
    CHECK(early > 0.1f);
    CHECK(early < 0.9f);
    CHECK(rig.f.swell > 0.95f);
    CHECK(rig.f.paramTargets[idx(P::DroneSendA)] == Approx(sendTarget)); // the knob does not move
    rig.engine.setParam(P::SwellHold, 0.0f);
    rig.run(3.0);
    CHECK(rig.f.swell < 0.05f);
}

TEST_CASE("Seasons sweep their parameter on their period and persist through the manager", "[seasons]")
{
    Rig rig;
    SeasonManager seasons(rig.engine);
    Season s;
    s.param = idx(P::DroneCutoff);
    s.depth = 0.4f;
    s.periodSeconds = 20.0f;
    s.shape = Season::Shape::Sine;
    REQUIRE(seasons.set(0, s));
    Season bad;
    bad.param = idx(P::MediumType);
    CHECK_FALSE(seasons.set(1, bad)); // discrete parameters cannot be swept
    rig.run(5.0); // a quarter cycle: sin peaks
    CHECK(rig.f.seasonValue[0] == Approx(1.0f).margin(0.02));
    rig.run(10.0); // three quarters: trough
    CHECK(rig.f.seasonValue[0] == Approx(-1.0f).margin(0.02));

    rig.engine.setParam(P::TideRate, 2.0f); // Tide speeds seasons up
    rig.run(4.0);
    const float v1 = rig.f.seasonValue[0];
    rig.run(2.5); // a quarter cycle at double speed
    CHECK(std::fabs(rig.f.seasonValue[0] - v1) > 0.3f);

    seasons.remove(0);
    CHECK(seasons.getSeasons().empty());
}

TEST_CASE("Incommensurate loops fire notes into Bloom on their own periods", "[loops]")
{
    Rig rig;
    auto glass = std::make_shared<tf::dsp::SampleBuffer>();
    glass->sampleRate = kFs;
    glass->left.resize(static_cast<std::size_t>(kFs));
    for (std::size_t i = 0; i < glass->left.size(); ++i)
        glass->left[i] = 0.5f * std::sin(0.06f * static_cast<float>(i)) * std::exp(-3.0f * static_cast<float>(i) / static_cast<float>(kFs));
    rig.engine.loadBloomSample(glass);
    rig.engine.setParam(P::BloomLength, 30.0f);
    rig.engine.setParam(P::LoopsOn, 1.0f);
    rig.engine.setParam(P::LoopsDensity, 1.0f);
    rig.engine.setParam(P::LoopsRate, 4.0f); // periods of ~4-10 s
    int fires = 0;
    float lastFlash = 0.0f;
    for (int step = 0; step < 300; ++step)
    {
        rig.run(0.1);
        float flash = 0.0f;
        for (int k = 0; k < 5; ++k)
            flash = std::max(flash, rig.f.loopFlash[static_cast<std::size_t>(k)]);
        if (flash > 0.9f && lastFlash <= 0.9f)
            ++fires;
        lastFlash = flash;
        for (int k = 5; k < 8; ++k)
            REQUIRE(rig.f.loopFlash[static_cast<std::size_t>(k)] == 0.0f); // count is 5
    }
    CHECK(fires >= 5); // 30 s at 4x: each of 5 loops fires at least once
    // Notes sit in the key (D minor by default: D E F G A Bb C).
    for (int k = 0; k < 5; ++k)
    {
        const int pc = static_cast<int>(std::lround(rig.f.loopNote[static_cast<std::size_t>(k)])) % 12;
        CHECK((pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 10 || pc == 0));
    }
    bool bloomSounding = false;
    for (const auto& v : rig.f.bloomVoices)
        bloomSounding = bloomSounding || v.active;
    CHECK(bloomSounding);
}

TEST_CASE("Journey travels between scenes and dwells at them", "[wander][journey]")
{
    SceneSet set;
    set.numScenes = 3;
    set.positions[0] = { 0.1f, 0.1f };
    set.positions[1] = { 0.9f, 0.2f };
    set.positions[2] = { 0.5f, 0.9f };
    Wander w;
    w.setSeed(4);
    const float rate = 0.1f; // a leg: 5 s travel + 5 s dwell
    int visits[3] = { 0, 0, 0 };
    int lastAt = -1;
    Point2 prev = w.update({ 0.5f, 0.5f }, 1.0f, rate, Wander::Style::Journey, &set, 0.0f);
    float maxStep = 0.0f;
    for (int step = 0; step < 6000; ++step) // 120 s at 20 ms
    {
        const auto p = w.update({ 0.5f, 0.5f }, 1.0f, rate, Wander::Style::Journey, &set, 0.02f);
        const float stepLen = std::hypot(p.x - prev.x, p.y - prev.y);
        maxStep = std::max(maxStep, stepLen);
        prev = p;
        for (int s = 0; s < 3; ++s)
            if (std::hypot(p.x - set.positions[static_cast<std::size_t>(s)].x, p.y - set.positions[static_cast<std::size_t>(s)].y) < 1.0e-4f && s != lastAt)
            {
                ++visits[s];
                lastAt = s;
            }
    }
    for (int s = 0; s < 3; ++s)
        CHECK(visits[s] >= 1); // every scene visited
    CHECK(visits[0] + visits[1] + visits[2] >= 8); // ~12 legs in 120 s
    CHECK(maxStep < 0.02f);                         // eased, no jumps

    // Wander 0 keeps the performer's cursor.
    const auto still = w.update({ 0.3f, 0.6f }, 0.0f, rate, Wander::Style::Journey, &set, 0.02f);
    CHECK(still.x == Approx(0.3f));
    CHECK(still.y == Approx(0.6f));
}

TEST_CASE("Hush sinks the sources and slow time eases Tide down and back", "[hush][slow]")
{
    Rig rig;
    rig.run(1.0);
    rig.engine.setParam(P::HushHold, 1.0f);
    rig.engine.setParam(P::SlowHold, 1.0f);
    rig.run(4.0);
    CHECK(rig.f.hush > 0.95f);
    CHECK(rig.f.tide == Approx(0.25f).epsilon(0.05)); // a quarter of Tide 1
    CHECK(rig.f.paramTargets[idx(P::TideRate)] == Approx(1.0f)); // the setting itself is untouched
    rig.engine.setParam(P::HushHold, 0.0f);
    rig.engine.setParam(P::SlowHold, 0.0f);
    rig.run(8.0);
    CHECK(rig.f.hush < 0.05f);
    CHECK(rig.f.tide == Approx(1.0f).epsilon(0.03));
}
