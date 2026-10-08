#include "support/AllocationGuard.h"

#include <engine/Engine.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::engine;

namespace {
struct Render
{
    std::vector<float> left, right;
};

Render renderEngine(Engine& engine, int totalSamples, int blockSize)
{
    Render out;
    out.left.assign(static_cast<size_t>(totalSamples), 0.0f);
    out.right.assign(static_cast<size_t>(totalSamples), 0.0f);
    int pos = 0;
    while (pos < totalSamples)
    {
        const int n = std::min(blockSize, totalSamples - pos);
        float* ptrs[2] = { out.left.data() + pos, out.right.data() + pos };
        engine.process(nullptr, 0, ptrs, 2, n);
        pos += n;
        TelemetryFrame f;
        while (engine.popTelemetry(f)) {}
        EngineNotice note;
        while (engine.popNotice(note)) {}
    }
    return out;
}
}

TEST_CASE("Engine::process never allocates")
{
    Engine engine;
    engine.prepare(48000.0, 512);
    engine.setParam(P::MasterFadeSecs, 0.5f);
    engine.command(Command::FadeIn);
    engine.setParam(P::DroneDensity, 6.0f);
    engine.setParam(P::DroneEvolve, 1.0f);

    std::vector<float> l(512), r(512);
    float* ptrs[2] = { l.data(), r.data() };

    std::size_t allocations = 0;
    {
        const tf::test::ScopedAllocationCounter counter;
        for (int i = 0; i < 48000 * 5 / 512; ++i)
        {
            if (i == 200)
                engine.post(ControlEvent::makeCommand(Command::Panic));
            engine.process(nullptr, 0, ptrs, 2, 512);
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}

TEST_CASE("Engine output is independent of host block size")
{
    Engine a, b;
    a.prepare(48000.0, 512);
    b.prepare(48000.0, 512);
    for (Engine* e : { &a, &b })
    {
        e->setParam(P::MasterFadeSecs, 0.5f);
        e->command(Command::FadeIn);
    }
    const auto ra = renderEngine(a, 48000 * 3, 512);
    const auto rb = renderEngine(b, 48000 * 3, 67);
    REQUIRE(ra.left == rb.left);
    REQUIRE(ra.right == rb.right);
}

TEST_CASE("Engine is silent before fade-in and audible after")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    const auto silent = renderEngine(engine, 48000, 256);
    for (float x : silent.left)
        REQUIRE(x == 0.0f);

    engine.setParam(P::MasterFadeSecs, 0.5f);
    engine.command(Command::FadeIn);
    const auto loud = renderEngine(engine, 48000 * 2, 256);
    double sumSq = 0.0;
    for (std::size_t i = 48000; i < loud.left.size(); ++i)
        sumSq += static_cast<double>(loud.left[i]) * loud.left[i];
    REQUIRE(sumSq / 48000.0 > 1.0e-5);
}

TEST_CASE("Engine panic silences output and resume brings it back")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    engine.setParam(P::MasterFadeSecs, 0.5f);
    engine.command(Command::FadeIn);
    renderEngine(engine, 48000, 256);

    engine.command(Command::Panic);
    const auto afterPanic = renderEngine(engine, 48000, 256);
    for (std::size_t i = 48000 / 10; i < afterPanic.left.size(); ++i)
        REQUIRE(afterPanic.left[i] == 0.0f);

    engine.command(Command::ResumeFromPanic);
    const auto resumed = renderEngine(engine, 48000 * 2, 256);
    float peak = 0.0f;
    for (float x : resumed.left)
        peak = std::max(peak, std::fabs(x));
    REQUIRE(peak > 0.01f);
}

TEST_CASE("Telemetry frames arrive at roughly the configured rate")
{
    Engine engine;
    engine.prepare(48000.0, 512);
    std::vector<float> l(512), r(512);
    float* ptrs[2] = { l.data(), r.data() };
    int frames = 0;
    for (int i = 0; i < 48000 / 512; ++i)
    {
        engine.process(nullptr, 0, ptrs, 2, 512);
        TelemetryFrame f;
        while (engine.popTelemetry(f))
            ++frames;
    }
    REQUIRE(frames >= 55);
    REQUIRE(frames <= 62);
}
