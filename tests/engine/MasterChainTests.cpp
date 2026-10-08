#include <engine/master/MasterChain.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace tf::engine;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 480;

void fill(std::vector<float>& l, std::vector<float>& r, float value)
{
    std::fill(l.begin(), l.end(), value);
    std::fill(r.begin(), r.end(), value);
}
}

TEST_CASE("Master starts silent and fades in over the configured time")
{
    MasterChain m;
    m.prepare({ kFs, kBlock });
    m.setFadeSeconds(1.0f);
    REQUIRE(m.getFadeState() == FadeState::Silent);

    m.fadeIn();
    std::vector<float> l(kBlock), r(kBlock);
    bool completed = false;
    int blocks = 0;
    while (! completed && blocks < 200)
    {
        fill(l, r, 0.1f);
        completed = m.process(l.data(), r.data(), kBlock, 1.0f, 1.0f).fadeInCompleted;
        ++blocks;
    }
    REQUIRE(completed);
    REQUIRE(blocks == 100);
    REQUIRE(m.getFadeState() == FadeState::Open);
}

TEST_CASE("Panic reaches silence within 60 ms and stays silent until resumed")
{
    MasterChain m;
    m.prepare({ kFs, kBlock });
    m.setFadeSeconds(0.01f);
    m.fadeIn();
    std::vector<float> l(kBlock), r(kBlock);
    for (int i = 0; i < 10; ++i)
    {
        fill(l, r, 0.5f);
        m.process(l.data(), r.data(), kBlock, 1.0f, 1.0f);
    }

    m.panic();
    bool silentReported = false;
    for (int i = 0; i < 6; ++i)
    {
        fill(l, r, 0.5f);
        silentReported = m.process(l.data(), r.data(), kBlock, 1.0f, 1.0f).panicReachedSilence || silentReported;
    }
    REQUIRE(silentReported);

    for (int i = 0; i < 50; ++i)
    {
        fill(l, r, 0.9f);
        m.process(l.data(), r.data(), kBlock, 1.0f, 1.0f);
        for (int s = 0; s < kBlock; ++s)
            REQUIRE(l[static_cast<size_t>(s)] == 0.0f);
    }

    m.resumeFromPanic();
    REQUIRE(m.getFadeState() == FadeState::FadingIn);
}

TEST_CASE("Non-finite input trips the guard and output stays finite")
{
    MasterChain m;
    m.prepare({ kFs, kBlock });
    m.setFadeSeconds(0.01f);
    m.fadeIn();
    std::vector<float> l(kBlock, 0.2f), r(kBlock, 0.2f);
    m.process(l.data(), r.data(), kBlock, 1.0f, 1.0f);

    fill(l, r, 0.2f);
    l[100] = std::numeric_limits<float>::quiet_NaN();
    r[200] = std::numeric_limits<float>::infinity();
    const auto ev = m.process(l.data(), r.data(), kBlock, 1.0f, 1.0f);
    REQUIRE(ev.guardTripped);
    REQUIRE(m.getGuardTrips() == 1);

    for (int b = 0; b < 20; ++b)
    {
        fill(l, r, 0.2f);
        m.process(l.data(), r.data(), kBlock, 1.0f, 1.0f);
        for (float x : l)
            REQUIRE(std::isfinite(x));
    }
}

TEST_CASE("Master output never exceeds the ceiling even when driven hard")
{
    MasterChain m;
    m.prepare({ kFs, kBlock });
    m.setCeilingDb(-1.0f);
    m.setFadeSeconds(0.01f);
    m.fadeIn();
    const float ceiling = std::pow(10.0f, -1.0f / 20.0f);
    std::vector<float> l(kBlock), r(kBlock);
    for (int b = 0; b < 100; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = 8.0f * std::sin(0.01f * static_cast<float>(b * kBlock + i));
        m.process(l.data(), r.data(), kBlock, 2.0f, 2.0f);
        for (int i = 0; i < kBlock; ++i)
            REQUIRE(std::fabs(l[static_cast<size_t>(i)]) <= ceiling);
    }
}
