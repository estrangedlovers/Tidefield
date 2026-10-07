#pragma once

#include "SceneSet.h"
#include "TerrainMath.h"

#include <dsp/core/MathUtil.h>
#include <dsp/core/Random.h>
#include <dsp/mod/Drift.h>

#include <algorithm>
#include <cmath>

namespace tf::engine {

/** Autonomous movement of the terrain cursor. Produces an offset that is added to the
    performer's cursor, scaled by `amount`, so the performer always stays in charge.

    Styles:
      Drift     - Ornstein-Uhlenbeck walk: random, but always pulled back toward the
                  performer's cursor, so it wanders without escaping.
      Orbit     - slow ellipse around the cursor with drifting radius and speed.
      TidePool  - drift that is attracted toward the nearest scene, so the sound tends
                  to settle into scenes, linger, then get washed out again. */
class Wander
{
public:
    enum class Style : int { Drift = 0, Orbit = 1, TidePool = 2 };

    void setSeed(std::uint64_t seed) noexcept
    {
        rng.setSeed(seed);
        radiusDrift.setSeed(seed + 1);
        speedDrift.setSeed(seed + 2);
        offset = {};
        angle = 0.0f;
    }

    void reset() noexcept
    {
        offset = {};
    }

    /** Advances by dt seconds of (tide-scaled) time and returns the effective position. */
    Point2 update(Point2 cursor, float amount, float rateHz, Style style, const SceneSet* scenes, float dt) noexcept
    {
        rateHz = std::max(rateHz, 0.0001f);
        const float reach = 0.45f * std::clamp(amount, 0.0f, 1.0f);

        switch (style)
        {
            case Style::Drift: stepOu(rateHz, dt, Point2 { 0.0f, 0.0f }, 0.0f); break;
            case Style::Orbit: stepOrbit(rateHz, dt); break;
            case Style::TidePool:
            {
                Point2 pull { 0.0f, 0.0f };
                float strength = 0.0f;
                if (scenes != nullptr && scenes->numScenes > 0 && reach > 0.0f)
                {
                    const Point2 here { cursor.x + offset.x * reach, cursor.y + offset.y * reach };
                    const int n = terrain::nearestScene(*scenes, here);
                    const auto target = scenes->positions[static_cast<std::size_t>(n)];
                    // Target offset (in unit-offset space) that would put us on the scene.
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
        // Irwin-Hall approximation: sum of 4 uniforms, unit variance.
        const float s = rng.nextFloat() + rng.nextFloat() + rng.nextFloat() + rng.nextFloat();
        return (s - 2.0f) * 1.7320508f;
    }

    /** OU process on a unit-scale offset, optionally attracted to `pull`. */
    void stepOu(float rateHz, float dt, Point2 pull, float pullStrength) noexcept
    {
        const float theta = dsp::kTwoPi * rateHz;           // mean reversion
        const float sigma = std::sqrt(2.0f * theta) * 0.6f; // stationary std ~0.6
        const float sq = std::sqrt(std::max(dt, 0.0f));
        offset.x += theta * dt * (-offset.x + pullStrength * (pull.x - offset.x)) + sigma * sq * gaussian();
        offset.y += theta * dt * (-offset.y + pullStrength * (pull.y - offset.y)) + sigma * sq * gaussian();
        offset.x = std::clamp(offset.x, -1.5f, 1.5f);
        offset.y = std::clamp(offset.y, -1.5f, 1.5f);
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
        // Ease toward the orbit point so switching style never jumps.
        const float k = std::min(1.0f, dt * 2.0f);
        offset.x += k * (radius * std::cos(angle) - offset.x);
        offset.y += k * (0.7f * radius * std::sin(angle) - offset.y);
    }

    dsp::Random rng;
    dsp::Drift radiusDrift, speedDrift;
    Point2 offset { 0.0f, 0.0f };
    float angle = 0.0f;
};

} // namespace tf::engine
