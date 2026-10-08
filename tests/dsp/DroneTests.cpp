#include <dsp/sources/drone/DroneGenerator.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::dsp;

namespace {
std::vector<float> render(std::uint64_t seed, int seconds, const DroneGenerator::Params& p, int block = 32)
{
    DroneGenerator d;
    d.prepare({ 48000.0, 512 }, seed);
    d.setParams(p);
    std::vector<float> out(static_cast<size_t>(48000 * seconds) * 2);
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    for (size_t pos = 0; pos < out.size() / 2; pos += static_cast<size_t>(block))
    {
        d.process(l.data(), r.data(), block, 1.0f);
        for (int i = 0; i < block; ++i)
        {
            out[(pos + static_cast<size_t>(i)) * 2] = l[static_cast<size_t>(i)];
            out[(pos + static_cast<size_t>(i)) * 2 + 1] = r[static_cast<size_t>(i)];
        }
    }
    return out;
}
}

TEST_CASE("Drone output is deterministic for a seed and differs across seeds")
{
    DroneGenerator::Params p;
    const auto a = render(5, 4, p);
    const auto b = render(5, 4, p);
    const auto c = render(6, 4, p);
    REQUIRE(a == b);
    REQUIRE(a != c);
}

TEST_CASE("Drone is finite, audible and bounded at extreme settings")
{
    DroneGenerator::Params p;
    p.density = 6.0f;
    p.resonance = 0.95f;
    p.cutoffHz = 12000.0f;
    p.rootNote = 72.0f;
    p.detuneCents = 50.0f;
    p.driftDepth = 1.0f;
    p.driftRate = 2.0f;
    p.evolve = 1.0f;
    p.noise = 1.0f;
    const auto out = render(1, 10, p);

    double sumSq = 0.0;
    float peak = 0.0f;
    for (float x : out)
    {
        REQUIRE(std::isfinite(x));
        peak = std::max(peak, std::fabs(x));
        sumSq += static_cast<double>(x) * x;
    }
    REQUIRE(sumSq / static_cast<double>(out.size()) > 1.0e-5);
    REQUIRE(peak < 4.0f);
}

TEST_CASE("Drone output does not depend on the caller's block size")
{
    DroneGenerator::Params p;
    p.evolve = 1.0f;
    REQUIRE(render(9, 3, p, 32) == render(9, 3, p, 8));
}

TEST_CASE("Drone evolves on its own when evolve is high")
{
    DroneGenerator d;
    d.prepare({ 48000.0, 512 }, 11);
    DroneGenerator::Params p;
    p.density = 6.0f;
    p.evolve = 1.0f;
    d.setParams(p);

    std::vector<float> before(6), l(512), r(512);
    for (int v = 0; v < 6; ++v)
        before[static_cast<size_t>(v)] = d.getVoiceInterval(v);

    for (int i = 0; i < 48000 * 120 / 512; ++i)
        d.process(l.data(), r.data(), 512, 1.0f);

    int changed = 0;
    for (int v = 0; v < 6; ++v)
        changed += d.getVoiceInterval(v) != before[static_cast<size_t>(v)];
    REQUIRE(changed >= 1);
    REQUIRE(d.getVoiceInterval(0) == before[0]);
}
