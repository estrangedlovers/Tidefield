#include "support/AllocationGuard.h"

#include <engine/Engine.h>
#include <engine/control/SnapshotChannel.h>
#include <engine/scene/SceneManager.h>
#include <engine/scene/TerrainMath.h>
#include <engine/scene/Wander.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numeric>
#include <vector>

using namespace tf::engine;
using Catch::Approx;

namespace {

/** Runs the engine for `seconds`, draining queues like the UI would, and returns the
    last telemetry frame. */
TelemetryFrame run(Engine& engine, double seconds, SceneManager* scenes = nullptr, int block = 256)
{
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    float* ptrs[2] = { l.data(), r.data() };
    TelemetryFrame last;
    const int blocks = static_cast<int>(seconds * engine.getSampleRate() / block);
    for (int i = 0; i < blocks; ++i)
    {
        engine.process(nullptr, 0, ptrs, 2, block);
        TelemetryFrame f;
        while (engine.popTelemetry(f))
            last = f;
        EngineNotice n;
        while (engine.popNotice(n)) {}
        if (scenes != nullptr)
            scenes->tick();
        else
            engine.collectGarbage();
    }
    return last;
}

Scene makeScene(const ParamRegistry& reg, float x, float y, float cutoff, float density)
{
    Scene s;
    s.position = { x, y };
    s.values[*reg.find("drone.cutoff")] = cutoff;
    s.values[*reg.find("drone.density")] = density;
    return s;
}

float target(const TelemetryFrame& f, P p) { return f.paramTargets[idx(p)]; }

} // namespace

TEST_CASE("SnapshotChannel hands over, retires and bounds in-flight snapshots")
{
    SnapshotChannel<int> ch(2);
    REQUIRE(ch.current() == nullptr);
    REQUIRE(ch.publish(std::make_unique<int>(1)));
    REQUIRE(ch.publish(std::make_unique<int>(2)));
    REQUIRE_FALSE(ch.publish(std::make_unique<int>(3))); // two in flight, capacity 2

    {
        const tf::test::ScopedAllocationCounter counter;
        REQUIRE(ch.acquire());
        REQUIRE(counter.count() == 0);
    }
    REQUIRE(*ch.current() == 2); // newest wins, 1 retired

    ch.collectGarbage();          // frees 1
    REQUIRE(ch.publish(std::make_unique<int>(3)));
    REQUIRE(ch.acquire());
    REQUIRE(*ch.current() == 3);
    REQUIRE_FALSE(ch.acquire());  // nothing new
}

TEST_CASE("IDW weights sum to one and favour the nearest scene")
{
    SceneSet set;
    set.numScenes = 3;
    set.positions[0] = { 0.0f, 0.0f };
    set.positions[1] = { 1.0f, 0.0f };
    set.positions[2] = { 0.5f, 1.0f };
    std::array<float, kMaxScenes> w {};

    terrain::computeWeights(set, { 0.3f, 0.4f }, 2.5f, w.data());
    REQUIRE(w[0] + w[1] + w[2] == Approx(1.0f));

    terrain::computeWeights(set, { 0.0f, 0.0f }, 2.5f, w.data());
    REQUIRE(w[0] > 0.999f);

    terrain::computeWeights(set, { 0.5f, 0.0f }, 2.5f, w.data());
    REQUIRE(w[0] == Approx(w[1]));

    // Higher focus makes a scene's island larger.
    std::array<float, kMaxScenes> soft {}, sharp {};
    terrain::computeWeights(set, { 0.2f, 0.1f }, 1.0f, soft.data());
    terrain::computeWeights(set, { 0.2f, 0.1f }, 6.0f, sharp.data());
    REQUIRE(sharp[0] > soft[0]);
}

TEST_CASE("Log columns interpolate evenly in pitch; discrete columns never blend")
{
    SceneSet set;
    set.numScenes = 2;
    set.positions[0] = { 0.0f, 0.5f };
    set.positions[1] = { 1.0f, 0.5f };
    set.columns = { { 0, SceneSet::Blend::Log }, { 1, SceneSet::Blend::Discrete } };
    set.values = { std::log(100.0f), 0.0f, std::log(1600.0f), 2.0f };

    std::array<float, kMaxScenes> w {};
    terrain::computeWeights(set, { 0.5f, 0.5f }, 2.0f, w.data());
    REQUIRE(terrain::blendColumn(set, 0, w.data()) == Approx(400.0f).epsilon(0.001)); // geometric mean
    terrain::computeWeights(set, { 0.6f, 0.5f }, 2.0f, w.data());
    REQUIRE(terrain::blendColumn(set, 1, w.data()) == 2.0f);
}

TEST_CASE("Moving the cursor morphs every terrain-bound parameter between scenes")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    SceneManager scenes(engine);
    const auto& reg = engine.getRegistry();
    scenes.addScene(makeScene(reg, 0.0f, 0.5f, 200.0f, 1.0f));
    scenes.addScene(makeScene(reg, 1.0f, 0.5f, 8000.0f, 6.0f));

    engine.setParam(P::TerrainGlide, 0.05f);
    engine.setParam(P::TerrainX, 0.0f);
    auto f = run(engine, 1.0);
    REQUIRE(f.numScenes == 2);
    REQUIRE(target(f, P::DroneCutoff) == Approx(200.0f).epsilon(0.01));
    REQUIRE(target(f, P::DroneDensity) == Approx(1.0f).epsilon(0.01));

    engine.setParam(P::TerrainX, 1.0f);
    f = run(engine, 1.0);
    REQUIRE(target(f, P::DroneCutoff) == Approx(8000.0f).epsilon(0.01));

    engine.setParam(P::TerrainX, 0.5f);
    f = run(engine, 1.0);
    REQUIRE(target(f, P::DroneCutoff) == Approx(1264.9f).epsilon(0.01)); // sqrt(200 * 8000)
    REQUIRE(target(f, P::DroneDensity) == Approx(3.5f).epsilon(0.01));
}

TEST_CASE("Glide makes the cursor travel, not jump")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    SceneManager scenes(engine);
    scenes.addScene(makeScene(engine.getRegistry(), 0.0f, 0.5f, 200.0f, 1.0f));
    scenes.addScene(makeScene(engine.getRegistry(), 1.0f, 0.5f, 8000.0f, 6.0f));
    engine.setParam(P::TerrainGlide, 2.0f);
    engine.setParam(P::TerrainX, 1.0f);
    const auto f = run(engine, 0.5);
    REQUIRE(f.cursor.x > 0.5f);   // moving toward 1 from the default 0.5
    REQUIRE(f.cursor.x < 0.75f);  // but not there yet after a quarter of the glide time
}

TEST_CASE("Live layer overrides the terrain until released")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    SceneManager scenes(engine);
    scenes.addScene(makeScene(engine.getRegistry(), 0.0f, 0.5f, 200.0f, 1.0f));
    scenes.addScene(makeScene(engine.getRegistry(), 1.0f, 0.5f, 8000.0f, 6.0f));
    engine.setParam(P::TerrainGlide, 0.05f);
    engine.setParam(P::TerrainX, 0.0f);
    run(engine, 0.5);

    engine.setParam(P::DroneCutoff, 3000.0f); // performer grabs the knob
    auto f = run(engine, 0.2);
    REQUIRE(f.live[idx(P::DroneCutoff)] == 1);
    REQUIRE(target(f, P::DroneCutoff) == Approx(3000.0f));

    engine.setParam(P::TerrainX, 1.0f); // terrain moves; the held knob does not
    f = run(engine, 1.0);
    REQUIRE(target(f, P::DroneCutoff) == Approx(3000.0f));
    REQUIRE(target(f, P::DroneDensity) == Approx(6.0f).epsilon(0.01)); // others still morph

    scenes.releaseLiveLayer();
    f = run(engine, 0.5);
    REQUIRE(f.live[idx(P::DroneCutoff)] == 0);
    REQUIRE(target(f, P::DroneCutoff) == Approx(8000.0f).epsilon(0.01));
}

TEST_CASE("Commit writes the live layer into a scene; capture stores the current sound")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    SceneManager scenes(engine);
    scenes.addScene(makeScene(engine.getRegistry(), 0.0f, 0.5f, 200.0f, 1.0f));
    engine.setParam(P::DroneCutoff, 5000.0f);
    auto f = run(engine, 0.2);
    scenes.commitLiveLayer(0, f);
    f = run(engine, 0.5);
    REQUIRE(f.live[idx(P::DroneCutoff)] == 0);
    REQUIRE(target(f, P::DroneCutoff) == Approx(5000.0f).epsilon(0.001));

    const int captured = scenes.captureScene("", { 0.8f, 0.8f }, f);
    REQUIRE(captured == 1);
    REQUIRE(scenes.getScenes()[1].name == "Scene 1");
    REQUIRE(scenes.getScenes()[1].values.at(idx(P::DroneCutoff)) == Approx(5000.0f).epsilon(0.001));
}

TEST_CASE("Pinned parameters ignore the terrain")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    SceneManager scenes(engine);
    scenes.setPinned(idx(P::DroneDensity), true);
    scenes.addScene(makeScene(engine.getRegistry(), 0.0f, 0.5f, 200.0f, 6.0f));
    const auto f = run(engine, 0.5);
    REQUIRE(target(f, P::DroneDensity) == Approx(3.0f)); // default, untouched
    REQUIRE(target(f, P::DroneCutoff) == Approx(200.0f).epsilon(0.01));
}

TEST_CASE("Wander stays inside the terrain and returns to the cursor at zero amount")
{
    for (auto style : { Wander::Style::Drift, Wander::Style::Orbit, Wander::Style::TidePool })
    {
        SceneSet set;
        set.numScenes = 2;
        set.positions[0] = { 0.1f, 0.1f };
        set.positions[1] = { 0.9f, 0.8f };
        Wander w;
        w.setSeed(3);
        float travelled = 0.0f;
        Point2 last { 0.5f, 0.5f };
        for (int i = 0; i < 20000; ++i)
        {
            const auto p = w.update({ 0.5f, 0.5f }, 1.0f, 0.2f, style, &set, 0.01f);
            REQUIRE(p.x >= 0.0f);
            REQUIRE(p.x <= 1.0f);
            REQUIRE(p.y >= 0.0f);
            REQUIRE(p.y <= 1.0f);
            travelled += std::hypot(p.x - last.x, p.y - last.y);
            last = p;
        }
        REQUIRE(travelled > 1.0f); // it actually moves

        const auto still = w.update({ 0.3f, 0.7f }, 0.0f, 0.2f, style, &set, 0.01f);
        REQUIRE(still.x == Approx(0.3f));
        REQUIRE(still.y == Approx(0.7f));
    }
}

TEST_CASE("Tide pool wander spends more time near scenes than plain drift")
{
    SceneSet set;
    set.numScenes = 2;
    set.positions[0] = { 0.2f, 0.5f };
    set.positions[1] = { 0.8f, 0.5f };
    auto nearShare = [&](Wander::Style style) {
        Wander w;
        w.setSeed(11);
        int near = 0;
        constexpr int kSteps = 60000;
        for (int i = 0; i < kSteps; ++i)
        {
            const auto p = w.update({ 0.5f, 0.5f }, 1.0f, 0.1f, style, &set, 0.02f);
            const int n = terrain::nearestScene(set, p);
            const auto q = set.positions[static_cast<size_t>(n)];
            near += std::hypot(p.x - q.x, p.y - q.y) < 0.12f;
        }
        return static_cast<float>(near) / kSteps;
    };
    REQUIRE(nearShare(Wander::Style::TidePool) > nearShare(Wander::Style::Drift) * 1.5f);
}

TEST_CASE("Engine with a moving, wandering terrain never allocates")
{
    Engine engine;
    engine.prepare(48000.0, 512);
    SceneManager scenes(engine);
    for (int i = 0; i < kMaxScenes; ++i)
        scenes.addScene(makeScene(engine.getRegistry(), static_cast<float>(i % 6) / 5.0f, static_cast<float>(i / 6) / 5.0f,
                                  100.0f + 300.0f * static_cast<float>(i), 1.0f + static_cast<float>(i % 6)));
    REQUIRE(scenes.isFull());
    REQUIRE(scenes.hasPendingPublish()); // 32 rapid edits outran the snapshot queue
    engine.setParam(P::TerrainWander, 1.0f);
    engine.setParam(P::TerrainWanderRate, 0.5f);
    engine.command(Command::FadeIn);

    const auto settled = run(engine, 0.1, &scenes, 512); // message-thread ticks publish the rest
    REQUIRE_FALSE(scenes.hasPendingPublish());
    REQUIRE(settled.numScenes == kMaxScenes);

    std::vector<float> l(512), r(512);
    float* ptrs[2] = { l.data(), r.data() };

    std::size_t allocations = 0;
    {
        const tf::test::ScopedAllocationCounter counter;
        for (int i = 0; i < 48000 * 3 / 512; ++i)
        {
            engine.process(nullptr, 0, ptrs, 2, 512);
            TelemetryFrame f;
            while (engine.popTelemetry(f)) {}
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}
