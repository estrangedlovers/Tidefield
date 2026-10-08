#include <dsp/core/Denormal.h>
#include <dsp/filters/DcBlocker.h>
#include <dsp/filters/OnePole.h>
#include <dsp/filters/Svf.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tf::dsp;

TEST_CASE("Svf decays to exact zero after an impulse (denormal flush)")
{
    Svf f;
    f.prepare(48000.0);
    f.setCutoff(200.0f, 0.95f);
    f.process(1.0f);
    float y = 1.0f;
    for (int i = 0; i < 48000 * 20; ++i)
        y = f.processLow(0.0f);
    REQUIRE(y == 0.0f);
}

TEST_CASE("Svf stays finite under extreme fast modulation")
{
    Svf f;
    f.prepare(44100.0);
    float y = 0.0f;
    for (int i = 0; i < 200000; ++i)
    {
        f.setCutoff((i % 2) ? 20.0f : 21000.0f, 0.99f);
        y = f.processLow((i % 64) < 32 ? 1.0f : -1.0f);
        REQUIRE(std::isfinite(y));
    }
}

TEST_CASE("DcBlocker removes a constant offset")
{
    DcBlocker dc;
    dc.prepare(48000.0);
    float y = 1.0f;
    for (int i = 0; i < 48000 * 2; ++i)
        y = dc.process(0.5f);
    REQUIRE(std::fabs(y) < 1.0e-3f);
}

TEST_CASE("OnePole decays to exact zero")
{
    OnePole p;
    p.prepare(48000.0);
    p.setCutoff(5.0f);
    p.processLow(1.0f);
    float y = 1.0f;
    for (int i = 0; i < 48000 * 60; ++i)
        y = p.processLow(0.0f);
    REQUIRE(y == 0.0f);
}

TEST_CASE("ScopedFlushDenormals flushes subnormal arithmetic")
{
    volatile float tiny = 1.0e-38f;
    float result = 0.0f;
    {
        const ScopedFlushDenormals guard;
        result = tiny * 1.0e-3f;
    }
    REQUIRE(result == 0.0f);
}
