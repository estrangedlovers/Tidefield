#pragma once

#include "SceneSet.h"
#include "TerrainPath.h"
#include "TerrainMath.h"

#include <dsp/core/MathUtil.h>
#include <dsp/core/Random.h>
#include <dsp/mod/Drift.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace tf::engine {
class Wander
{
public:
    enum class Style : int { Drift = 0, Orbit = 1, TidePool = 2, Journey = 3, Path = 4 };

    void setSeed(std::uint64_t seed) noexcept
    {
        rng.setSeed(seed);
        radiusDrift.setSeed(seed + 1);
        speedDrift.setSeed(seed + 2);
        offset = { 0.0f, 0.0f };
        angle = 0.0f;
        journeyTo = -1;
        pathPhase = 0.0f;
    }

    void reset() noexcept
    {
        offset = { 0.0f, 0.0f };
        journeyTo = -1;
    }

    Point2 update(Point2 cursor, float amount, float rateHz, Style style, const SceneSet* scenes, float dt,
                  const TerrainPath* path = nullptr) noexcept
    {
        rateHz = std::max(rateHz, 0.0001f);
        const float reach = 0.45f * std::clamp(amount, 0.0f, 1.0f);

        if (style == Style::Path && path != nullptr && path->count >= 2)
        {
            if (path->version != pathVersion)
            {
                pathVersion = path->version;
                const Point2 here { cursor.x + offset.x * reach, cursor.y + offset.y * reach };
                float best = 1.0e9f;
                for (int i = 0; i < path->count; ++i)
                {
                    const auto& q = path->points[static_cast<std::size_t>(i)];
                    const float d = (q.x - here.x) * (q.x - here.x) + (q.y - here.y) * (q.y - here.y);
                    if (d < best)
                    {
                        best = d;
                        pathPhase = static_cast<float>(i) / static_cast<float>(path->count);
                    }
                }
            }
            pathPhase += dt * rateHz;
            pathPhase -= std::floor(pathPhase);
            const Point2 at = path->at(pathPhase);
            const float a = std::clamp(amount, 0.0f, 1.0f);
            const Point2 pos { cursor.x + (at.x - cursor.x) * a, cursor.y + (at.y - cursor.y) * a };
            if (reach > 0.0f)
                offset = { std::clamp((pos.x - cursor.x) / reach, -1.5f, 1.5f), std::clamp((pos.y - cursor.y) / reach, -1.5f, 1.5f) };
            return { std::clamp(pos.x, 0.0f, 1.0f), std::clamp(pos.y, 0.0f, 1.0f) };
        }
        if (style == Style::Path)
            style = Style::Drift;

        if (style == Style::Journey && scenes != nullptr && scenes->numScenes >= 2)
        {
            const Point2 here { cursor.x + offset.x * reach, cursor.y + offset.y * reach };
            const Point2 at = stepJourney(here, rateHz, scenes, dt);
            const float a = std::clamp(amount, 0.0f, 1.0f);
            const Point2 pos { cursor.x + (at.x - cursor.x) * a, cursor.y + (at.y - cursor.y) * a };
            if (reach > 0.0f)
                offset = { std::clamp((pos.x - cursor.x) / reach, -1.5f, 1.5f), std::clamp((pos.y - cursor.y) / reach, -1.5f, 1.5f) };
            return { std::clamp(pos.x, 0.0f, 1.0f), std::clamp(pos.y, 0.0f, 1.0f) };
        }
        journeyTo = -1;

        switch (style)
        {
            case Style::Drift: stepOu(rateHz, dt, Point2 { 0.0f, 0.0f }, 0.0f); break;
            case Style::Orbit: stepOrbit(rateHz, dt); break;
            case Style::Journey:
            case Style::Path:
                stepOu(rateHz, dt, Point2 { 0.0f, 0.0f }, 0.0f);
                break;
            case Style::TidePool:
            {
                Point2 pull { 0.0f, 0.0f };
                float strength = 0.0f;
                if (scenes != nullptr && scenes->numScenes > 0 && reach > 0.0f)
                {
                    const Point2 here { cursor.x + offset.x * reach, cursor.y + offset.y * reach };
                    const int n = terrain::nearestScene(*scenes, here);
                    const auto target = scenes->positions[static_cast<std::size_t>(n)];
                    pull = { (target.x - cursor.x) / reach, (target.y - cursor.y) / reach };
                    strength = 1.5f;
                }
                stepOu(rateHz, dt, pull, strength);
                break;
            }
        }

        return { std::clamp(cursor.x + offset.x * reach, 0.0f, 1.0f), std::clamp(cursor.y + offset.y * reach, 0.0f, 1.0f) };
    }

    Point2 getOffset() const noexcept { return offset; }

private:
    float gaussian() noexcept
    {
        const float s = rng.nextFloat() + rng.nextFloat() + rng.nextFloat() + rng.nextFloat();
        return (s - 2.0f) * 1.7320508f;
    }

    void stepOu(float rateHz, float dt, Point2 pull, float pullStrength) noexcept
    {
        const float theta = dsp::kTwoPi * rateHz;
        const float sigma = std::sqrt(2.0f * theta) * 0.6f;
        const float sq = std::sqrt(std::max(dt, 0.0f));
        offset.x += theta * dt * (-offset.x + pullStrength * (pull.x - offset.x)) + sigma * sq * gaussian();
        offset.y += theta * dt * (-offset.y + pullStrength * (pull.y - offset.y)) + sigma * sq * gaussian();
        offset.x = std::clamp(offset.x, -1.5f, 1.5f);
        offset.y = std::clamp(offset.y, -1.5f, 1.5f);
    }

    Point2 stepJourney(Point2 here, float rateHz, const SceneSet* scenes, float dt) noexcept
    {
        const int n = scenes->numScenes;
        auto pos = [&](int i) { return scenes->positions[static_cast<std::size_t>(i)]; };
        if (journeyTo < 0 || journeyTo >= n)
        {
            journeyFrom = here;
            journeyTo = terrain::nearestScene(*scenes, here);
            journeyPhase = 0.0f;
            journeyDwell = 0.0f;
        }
        if (journeyDwell > 0.0f)
        {
            journeyDwell -= dt * rateHz * 2.0f;
            return journeyFrom;
        }
        journeyPhase += dt * rateHz * 2.0f;
        if (journeyPhase >= 1.0f)
        {
            const int at = journeyTo;
            journeyFrom = pos(at);
            float total = 0.0f;
            std::array<float, kMaxScenes> w {};
            for (int i = 0; i < n; ++i)
            {
                if (i == at)
                    continue;
                const float dx = pos(i).x - pos(at).x, dy = pos(i).y - pos(at).y;
                w[static_cast<std::size_t>(i)] = 1.0f / (std::sqrt(dx * dx + dy * dy) + 0.15f);
                total += w[static_cast<std::size_t>(i)];
            }
            float pick = rng.nextFloat() * total;
            int next = at == 0 ? 1 : 0;
            for (int i = 0; i < n; ++i)
            {
                if (i == at)
                    continue;
                pick -= w[static_cast<std::size_t>(i)];
                if (pick <= 0.0f)
                {
                    next = i;
                    break;
                }
            }
            journeyTo = next;
            journeyPhase = 0.0f;
            journeyDwell = 1.0f;
            return journeyFrom;
        }
        const float t = dsp::smoothstep(journeyPhase);
        const Point2 to = pos(journeyTo);
        return { journeyFrom.x + (to.x - journeyFrom.x) * t, journeyFrom.y + (to.y - journeyFrom.y) * t };
    }

    void stepOrbit(float rateHz, float dt) noexcept
    {
        radiusDrift.setRate(rateHz * 0.5f);
        speedDrift.setRate(rateHz * 0.3f);
        const float radius = 0.65f + 0.35f * radiusDrift.advance(dt);
        const float speed = 1.0f + 0.4f * speedDrift.advance(dt);
        angle += dsp::kTwoPi * rateHz * speed * dt;
        if (angle > dsp::kTwoPi)
            angle -= dsp::kTwoPi;
        const float k = std::min(1.0f, dt * 2.0f);
        offset.x += k * (radius * std::cos(angle) - offset.x);
        offset.y += k * (0.7f * radius * std::sin(angle) - offset.y);
    }

    dsp::Random rng;
    dsp::Drift radiusDrift, speedDrift;
    Point2 offset { 0.0f, 0.0f };
    float angle = 0.0f;
    Point2 journeyFrom { 0.5f, 0.5f };
    int journeyTo = -1;
    float journeyPhase = 0.0f, journeyDwell = 0.0f;
    float pathPhase = 0.0f;
    std::uint64_t pathVersion = 0;
};
}
