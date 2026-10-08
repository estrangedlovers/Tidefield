#include <engine/Engine.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/Wander.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::engine;
using Catch::Approx;

namespace {
std::vector<Point2> circle(float cx, float cy, float r, int n, float wobble = 0.0f)
{
    std::vector<Point2> pts;
    for (int i = 0; i < n; ++i)
    {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
        const float warp = a + 0.4f * std::sin(a);
        pts.push_back({ cx + r * std::cos(warp) + wobble * std::sin(37.0f * a), cy + r * std::sin(warp) });
    }
    return pts;
}

float distanceToLoop(const TerrainPath& p, Point2 q)
{
    float best = 1.0e9f;
    for (int i = 0; i < p.count; ++i)
    {
        const auto& a = p.points[static_cast<std::size_t>(i)];
        best = std::min(best, std::hypot(a.x - q.x, a.y - q.y));
    }
    return best;
}
}

TEST_CASE("A drawn path is resampled evenly, closed and kept on the terrain", "[path]")
{
    const auto p = TerrainPath::build(circle(0.5f, 0.5f, 0.3f, 90, 0.004f), 1);
    REQUIRE(p.count == TerrainPath::kPoints);
    float minStep = 1.0e9f, maxStep = 0.0f;
    for (int i = 0; i < p.count; ++i)
    {
        const auto& a = p.points[static_cast<std::size_t>(i)];
        const auto& b = p.points[static_cast<std::size_t>((i + 1) % p.count)];
        const float d = std::hypot(b.x - a.x, b.y - a.y);
        minStep = std::min(minStep, d);
        maxStep = std::max(maxStep, d);
    }
    CHECK(maxStep < minStep * 1.6f);
    CHECK(p.at(1.25f).x == Approx(p.at(0.25f).x));

    const auto clamped = TerrainPath::build({ { -1.0f, 0.5f }, { 2.0f, 0.5f }, { 0.5f, 3.0f } }, 2);
    for (int i = 0; i < clamped.count; ++i)
    {
        CHECK(clamped.points[static_cast<std::size_t>(i)].x >= 0.0f);
        CHECK(clamped.points[static_cast<std::size_t>(i)].y <= 1.0f);
    }
    CHECK(TerrainPath::build({ { 0.3f, 0.3f } }, 3).count == 0);
}

TEST_CASE("Path wander travels the loop at a steady pace and starts where the sound is", "[path]")
{
    const auto path = TerrainPath::build(circle(0.5f, 0.5f, 0.3f, 64), 7);
    Wander w;
    w.setSeed(5);
    const Point2 cursor { 0.8f, 0.5f };
    const float rate = 0.1f;
    const float dt = 0.01f;

    auto first = w.update(cursor, 1.0f, rate, Wander::Style::Path, nullptr, dt, &path);
    CHECK(std::hypot(first.x - cursor.x, first.y - cursor.y) < 0.03f);

    float maxOff = 0.0f;
    Point2 pos = first;
    for (int i = 0; i < 1000; ++i)
    {
        pos = w.update(cursor, 1.0f, rate, Wander::Style::Path, nullptr, dt, &path);
        maxOff = std::max(maxOff, distanceToLoop(path, pos));
    }
    CHECK(maxOff < 0.01f);
    CHECK(std::hypot(pos.x - first.x, pos.y - first.y) < 0.03f);

    for (int i = 0; i < 500; ++i)
        pos = w.update(cursor, 1.0f, rate, Wander::Style::Path, nullptr, dt, &path);
    CHECK(pos.x < 0.25f);

    const auto still = w.update(cursor, 0.0f, rate, Wander::Style::Path, nullptr, dt, &path);
    CHECK(still.x == Approx(cursor.x));
    CHECK(still.y == Approx(cursor.y));

    Wander d;
    d.setSeed(5);
    Point2 last {};
    for (int i = 0; i < 300; ++i)
        last = d.update({ 0.5f, 0.5f }, 1.0f, 0.2f, Wander::Style::Path, nullptr, dt, nullptr);
    CHECK((std::abs(last.x - 0.5f) > 1.0e-4f || std::abs(last.y - 0.5f) > 1.0e-4f));
}

TEST_CASE("The engine follows a published path in Path style", "[path]")
{
    Engine engine;
    engine.prepare(48000.0, 256);
    PathManager paths(engine);
    paths.set(circle(0.5f, 0.5f, 0.35f, 48));
    engine.setParam(P::TerrainWanderStyle, 4.0f);
    engine.setParam(P::TerrainWander, 1.0f);
    engine.setParam(P::TerrainWanderRate, 0.25f);
    engine.setParam(P::TerrainX, 0.85f);
    engine.setParam(P::TerrainY, 0.5f);

    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    TelemetryFrame f;
    const auto loop = TerrainPath::build(paths.getStroke(), 0);
    float minX = 1.0f, maxX = 0.0f, maxOff = 0.0f;
    for (int b = 0; b < 48000 * 8 / 256; ++b)
    {
        engine.process(nullptr, 0, outs, 2, 256);
        while (engine.popTelemetry(f)) {}
        if (b > 48000 * 3 / 256)
        {
            minX = std::min(minX, f.position.x);
            maxX = std::max(maxX, f.position.x);
            maxOff = std::max(maxOff, distanceToLoop(loop, f.position));
        }
        engine.collectGarbage();
    }
    CHECK(maxOff < 0.02f);
    CHECK(maxX - minX > 0.6f);

    paths.clear();
    engine.process(nullptr, 0, outs, 2, 256);
    engine.collectGarbage();
}
