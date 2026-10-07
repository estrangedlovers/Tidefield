#include <engine/params/ParamRegistry.h>
#include <engine/params/ParamState.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tf::engine;
using Catch::Approx;

TEST_CASE("Registry ids are unique and resolvable")
{
    ParamRegistry r;
    REQUIRE(r.size() == kNumParams);
    for (std::size_t i = 0; i < r.size(); ++i)
    {
        const auto& s = r.spec(static_cast<ParamIndex>(i));
        REQUIRE(r.find(s.id) == static_cast<ParamIndex>(i));
        REQUIRE(s.minValue < s.maxValue);
        REQUIRE(s.defaultValue >= s.minValue);
        REQUIRE(s.defaultValue <= s.maxValue);
    }
    REQUIRE_FALSE(r.find("no.such.param").has_value());
}

TEST_CASE("Normalised mapping round-trips, including log tapers")
{
    ParamRegistry r;
    for (const auto& s : r.all())
        for (float n : { 0.0f, 0.25f, 0.5f, 1.0f })
            REQUIRE(s.toNormalised(s.fromNormalised(n)) == Approx(n).margin(1e-4));
}

TEST_CASE("ParamState smooths toward clamped targets")
{
    ParamRegistry r;
    ParamState st;
    st.prepare(r, 48000.0);

    st.setTarget(idx(P::DroneCutoff), 1.0e6f); // clamped to max
    st.advance(32);
    const float first = st.current(P::DroneCutoff);
    REQUIRE(first > r.spec(P::DroneCutoff).defaultValue);
    REQUIRE(first < r.spec(P::DroneCutoff).maxValue); // smoothed, not jumped

    for (int i = 0; i < 48000 / 32 * 5; ++i)
        st.advance(32);
    REQUIRE(st.current(P::DroneCutoff) == Approx(r.spec(P::DroneCutoff).maxValue));
}
