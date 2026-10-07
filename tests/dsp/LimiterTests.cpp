#include <dsp/core/MathUtil.h>
#include <dsp/core/Random.h>
#include <dsp/fx/Limiter.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::dsp;

TEST_CASE("Limiter never exceeds the ceiling on loud random bursts")
{
    Limiter lim;
    lim.prepare({ 48000.0, 512 });
    lim.setCeilingDb(-1.0f);
    const float ceiling = dbToGain(-1.0f);

    Random rng(42);
    std::vector<float> l(512), r(512);
    for (int block = 0; block < 2000; ++block)
    {
        const float level = (block % 7 == 0) ? 20.0f : rng.nextRange(0.1f, 4.0f);
        for (int i = 0; i < 512; ++i)
        {
            l[static_cast<size_t>(i)] = rng.nextBipolar() * level;
            r[static_cast<size_t>(i)] = (rng.chance(0.001f) ? 30.0f : rng.nextBipolar()) * level;
        }
        lim.process(l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i)
        {
            REQUIRE(std::fabs(l[static_cast<size_t>(i)]) <= ceiling);
            REQUIRE(std::fabs(r[static_cast<size_t>(i)]) <= ceiling);
        }
    }
}

TEST_CASE("Limiter gain reduction is smooth: no clipping on a single spike")
{
    // A lone spike must be absorbed by gain reduction, not by the hard clip.
    Limiter lim;
    lim.prepare({ 48000.0, 1024 });
    lim.setCeilingDb(0.0f);
    std::vector<float> l(1024, 0.0f), r(1024, 0.0f);
    l[500] = 4.0f;
    lim.process(l.data(), r.data(), 1024);
    const int latency = lim.getLatencySamples();
    REQUIRE(std::fabs(l[static_cast<size_t>(500 + latency)]) <= 1.0f);
    REQUIRE(std::fabs(l[static_cast<size_t>(500 + latency)]) > 0.95f);
}

TEST_CASE("Limiter passes quiet material unchanged apart from latency")
{
    Limiter lim;
    lim.prepare({ 48000.0, 256 });
    lim.setCeilingDb(-1.0f);
    const int latency = lim.getLatencySamples();
    std::vector<float> l(256), r(256), ref(256);
    for (int i = 0; i < 256; ++i)
        ref[static_cast<size_t>(i)] = l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = 0.3f * std::sin(0.05f * static_cast<float>(i));
    lim.process(l.data(), r.data(), 256);
    for (int i = latency; i < 256; ++i)
        REQUIRE(std::fabs(l[static_cast<size_t>(i)] - ref[static_cast<size_t>(i - latency)]) < 1.0e-6f);
}
