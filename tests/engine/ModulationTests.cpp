#include "support/AllocationGuard.h"

#include <engine/Engine.h>
#include <engine/mod/ModRouteManager.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace tf;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

struct Rig
{
    engine::Engine engine;
    engine::ModRouteManager routes { engine };
    std::vector<float> l = std::vector<float>(kBlock), r = std::vector<float>(kBlock), in = std::vector<float>(kBlock);
    double phase = 0.0;

    Rig() { engine.prepare(kFs, kBlock); }

    template <typename Fn>
    void run(double seconds, float inputAmplitude, Fn&& onFrame)
    {
        float* outs[2] = { l.data(), r.data() };
        const float* ins[1] = { in.data() };
        engine::TelemetryFrame f;
        for (int b = 0; b < static_cast<int>(seconds * kFs / kBlock); ++b)
        {
            for (auto& x : in)
            {
                x = inputAmplitude * static_cast<float>(std::sin(phase));
                phase += 2.0 * 3.141592653589793 * 220.0 / kFs;
            }
            engine.process(ins, 1, outs, 2, kBlock);
            routes.tick();
            while (engine.popTelemetry(f))
                onFrame(f);
        }
    }
};
}

TEST_CASE("An LFO route swings its target both ways by the route's depth", "[mod]")
{
    Rig rig;
    rig.engine.setParam(engine::P::ModLfo1Rate, 4.0f);
    REQUIRE(rig.routes.add(engine::ModSource::Lfo1, engine::idx(engine::P::DroneCutoff), 0.4f) == 0);
    float lo = 1.0f, hi = -1.0f;
    rig.run(1.5, 0.0f, [&](const engine::TelemetryFrame& f) {
        const float m = f.paramMod[engine::idx(engine::P::DroneCutoff)];
        lo = std::min(lo, m);
        hi = std::max(hi, m);
    });
    CHECK(hi > 0.3f);
    CHECK(lo < -0.3f);
    CHECK(hi <= 0.4001f);
    CHECK(lo >= -0.4001f);
}

TEST_CASE("A route at zero depth or a removed route leaves its target alone", "[mod]")
{
    Rig rig;
    rig.engine.setParam(engine::P::ModLfo1Rate, 4.0f);
    rig.routes.add(engine::ModSource::Lfo1, engine::idx(engine::P::DroneCutoff), 0.0f);
    float most = 0.0f;
    rig.run(0.5, 0.0f, [&](const engine::TelemetryFrame& f) { most = std::max(most, std::abs(f.paramMod[engine::idx(engine::P::DroneCutoff)])); });
    CHECK(most == 0.0f);

    rig.routes.add(engine::ModSource::Lfo1, engine::idx(engine::P::BloomTone), 0.5f);
    rig.run(0.3, 0.0f, [](const engine::TelemetryFrame&) {});
    rig.routes.remove(1);
    most = 0.0f;
    rig.run(0.5, 0.0f, [&](const engine::TelemetryFrame& f) { most = std::max(most, std::abs(f.paramMod[engine::idx(engine::P::BloomTone)])); });
    CHECK(most < 1.0e-6f);
}

TEST_CASE("The input follower rises with a played input and falls back in silence", "[mod]")
{
    Rig rig;
    rig.routes.add(engine::ModSource::InputLevel, engine::idx(engine::P::DroneCutoff), 0.5f);
    float loud = 0.0f;
    rig.run(1.0, 0.3f, [&](const engine::TelemetryFrame& f) { loud = f.modValue[static_cast<std::size_t>(engine::ModSource::InputLevel)]; });
    CHECK(loud > 0.6f);
    float quiet = 1.0f;
    rig.run(3.0, 0.0f, [&](const engine::TelemetryFrame& f) { quiet = f.modValue[static_cast<std::size_t>(engine::ModSource::InputLevel)]; });
    CHECK(quiet < 0.05f);
}

TEST_CASE("Notes set the velocity and pitch sources", "[mod]")
{
    Rig rig;
    rig.engine.noteOn(84, 0.9f);
    float vel = 0.0f, pitch = 0.0f;
    rig.run(0.1, 0.0f, [&](const engine::TelemetryFrame& f) {
        vel = f.modValue[static_cast<std::size_t>(engine::ModSource::Velocity)];
        pitch = f.modValue[static_cast<std::size_t>(engine::ModSource::NotePitch)];
    });
    CHECK(vel > 0.89f);
    CHECK(pitch > 0.8f);
}

TEST_CASE("Choice parameters and route depths cannot be modulation targets", "[mod]")
{
    engine::Engine e;
    engine::ModRouteManager routes(e);
    CHECK_FALSE(routes.canModulate(engine::idx(engine::P::BloomTransform)));
    CHECK_FALSE(routes.canModulate(engine::idx(engine::P::ModRoute3Depth)));
    CHECK(routes.canModulate(engine::idx(engine::P::ModRandom1Smooth)));
    for (int k = 0; k < engine::kMaxModRoutes; ++k)
        CHECK(routes.add(engine::ModSource::Random1, engine::idx(engine::P::DroneCutoff), 0.1f) == k);
    CHECK(routes.add(engine::ModSource::Random1, engine::idx(engine::P::DroneCutoff), 0.1f) < 0);
}

TEST_CASE("Every modulation source runs on the audio thread without allocating", "[mod][rt]")
{
    Rig rig;
    for (int s = 0; s < engine::kNumModSources; ++s)
        rig.routes.add(static_cast<engine::ModSource>(s), engine::idx(engine::P::DroneCutoff), 0.05f);
    rig.run(0.2, 0.2f, [](const engine::TelemetryFrame&) {});
    float* outs[2] = { rig.l.data(), rig.r.data() };
    const float* ins[1] = { rig.in.data() };
    test::ScopedAllocationCounter counter;
    for (int b = 0; b < 200; ++b)
        rig.engine.process(ins, 1, outs, 2, kBlock);
    CHECK(counter.count() == 0);
    for (float x : rig.l)
        CHECK(std::isfinite(x));
}

TEST_CASE("A macro moves each of its targets across its own range", "[mod][macro]")
{
    Rig rig;
    REQUIRE(rig.routes.addMacroTarget(0, engine::idx(engine::P::DroneCutoff), 0.0f, -0.6f));
    REQUIRE(rig.routes.addMacroTarget(0, engine::idx(engine::P::ResDecay), 0.1f, 0.5f));
    const auto modAt = [&rig](float amount) {
        rig.engine.setParam(engine::P::Macro1, amount);
        float cutoff = 0.0f, decay = 0.0f;
        rig.run(0.6, 0.0f, [&](const engine::TelemetryFrame& f) {
            cutoff = f.paramMod[engine::idx(engine::P::DroneCutoff)];
            decay = f.paramMod[engine::idx(engine::P::ResDecay)];
        });
        return std::make_pair(cutoff, decay);
    };
    const auto low = modAt(0.0f);
    const auto mid = modAt(0.5f);
    const auto high = modAt(1.0f);
    CHECK(std::abs(low.first) < 0.01f);
    CHECK(std::abs(low.second - 0.1f) < 0.01f);
    CHECK(std::abs(mid.first + 0.3f) < 0.02f);
    CHECK(std::abs(high.first + 0.6f) < 0.02f);
    CHECK(std::abs(high.second - 0.5f) < 0.02f);
}

TEST_CASE("Macros never target themselves or discrete controls and allow eight targets each", "[mod][macro]")
{
    Rig rig;
    CHECK_FALSE(rig.routes.addMacroTarget(0, engine::idx(engine::P::Macro2)));
    CHECK_FALSE(rig.routes.addMacroTarget(0, engine::idx(engine::P::DroneWave)));
    CHECK_FALSE(rig.routes.addMacroTarget(8, engine::idx(engine::P::DroneCutoff)));
    const engine::P targets[] = { engine::P::DroneCutoff, engine::P::DroneResonance, engine::P::DroneNoise, engine::P::DroneDetune,
                                  engine::P::DroneSpread, engine::P::DroneTilt, engine::P::DroneSub, engine::P::DroneDrive, engine::P::DroneVibrato };
    int added = 0;
    for (auto p : targets)
        added += rig.routes.addMacroTarget(3, engine::idx(p)) ? 1 : 0;
    CHECK(added == engine::kMaxMacroTargets);
    CHECK(rig.routes.macroFor(engine::idx(engine::P::DroneCutoff)) == 3);
    rig.routes.removeFromMacros(engine::idx(engine::P::DroneCutoff));
    CHECK(rig.routes.macroFor(engine::idx(engine::P::DroneCutoff)) == -1);
}

TEST_CASE("A macro at rest leaves the knob's own value untouched when its range starts at zero", "[mod][macro]")
{
    Rig rig;
    rig.engine.setParam(engine::P::DroneCutoff, 2000.0f);
    REQUIRE(rig.routes.addMacroTarget(1, engine::idx(engine::P::DroneCutoff), 0.0f, 0.8f));
    float target = 0.0f, mod = 1.0f;
    rig.run(0.6, 0.0f, [&](const engine::TelemetryFrame& f) {
        target = f.paramTargets[engine::idx(engine::P::DroneCutoff)];
        mod = f.paramMod[engine::idx(engine::P::DroneCutoff)];
    });
    CHECK(std::abs(target - 2000.0f) < 0.01f);
    CHECK(std::abs(mod) < 1.0e-6f);
}
