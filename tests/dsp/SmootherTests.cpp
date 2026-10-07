#include <dsp/core/Smoother.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tf::dsp;
using Catch::Approx;

TEST_CASE("LinearSmoother reaches the target exactly after the ramp, without overshoot")
{
    LinearSmoother s;
    s.prepare(48000.0, 0.01f); // 480 samples
    s.reset(0.0f);
    s.setTarget(1.0f);

    float last = 0.0f;
    for (int i = 0; i < 479; ++i)
    {
        const float v = s.next();
        REQUIRE(v >= last);
        REQUIRE(v <= 1.0f);
        last = v;
    }
    REQUIRE(s.next() == 1.0f);
    REQUIRE_FALSE(s.isSmoothing());
}

TEST_CASE("LinearSmoother skip matches stepping sample by sample")
{
    LinearSmoother a, b;
    a.prepare(48000.0, 0.02f);
    b.prepare(48000.0, 0.02f);
    a.reset(-3.0f);
    b.reset(-3.0f);
    a.setTarget(5.0f);
    b.setTarget(5.0f);
    for (int i = 0; i < 32; ++i)
        a.next();
    REQUIRE(b.skip(32) == Approx(a.getCurrent()).margin(1e-5));
}

TEST_CASE("OnePoleSmoother converges and snaps to the target")
{
    OnePoleSmoother s;
    s.prepare(48000.0, 0.05f, false);
    s.reset(0.0f);
    s.setTarget(1.0f);
    for (int i = 0; i < 48000; ++i)
        s.next();
    REQUIRE(s.getCurrent() == 1.0f);
    REQUIRE_FALSE(s.isSmoothing());
}

TEST_CASE("Log-domain smoother moves evenly in pitch")
{
    OnePoleSmoother s;
    s.prepare(48000.0, 0.1f, true);
    s.reset(100.0f);
    s.setTarget(1600.0f);
    // After one time constant the log-distance should be ~63% covered: 100 * 16^0.632.
    const float v = s.skip(4800);
    REQUIRE(v == Approx(100.0f * std::pow(16.0f, 0.632f)).epsilon(0.02));
}
