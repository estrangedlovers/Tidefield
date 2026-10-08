#pragma once

#include "SceneSet.h"

#include <algorithm>
#include <cmath>

namespace tf::engine::terrain {
inline void computeWeights(const SceneSet& set, Point2 p, float focus, float* weights) noexcept
{
    constexpr float kEpsilon = 1.0e-5f;
    const int n = set.numScenes;
    float total = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float dx = p.x - set.positions[static_cast<std::size_t>(i)].x;
        const float dy = p.y - set.positions[static_cast<std::size_t>(i)].y;
        const float d2 = dx * dx + dy * dy + kEpsilon;
        const float w = std::pow(d2, -0.5f * focus);
        weights[i] = w;
        total += w;
    }
    const float inv = total > 0.0f ? 1.0f / total : 0.0f;
    for (int i = 0; i < n; ++i)
        weights[i] *= inv;
}

inline float blendColumn(const SceneSet& set, std::size_t column, const float* weights) noexcept
{
    const auto blend = set.columns[column].blend;
    if (blend == SceneSet::Blend::Discrete)
    {
        int best = -1;
        for (int i = 0; i < set.numScenes; ++i)
            if (set.isDefined(i, column) && (best < 0 || weights[i] > weights[best]))
                best = i;
        return set.value(std::max(best, 0), column);
    }

    float sum = 0.0f, total = 0.0f;
    for (int i = 0; i < set.numScenes; ++i)
        if (set.isDefined(i, column))
        {
            sum += weights[i] * set.value(i, column);
            total += weights[i];
        }
    const float v = total > 0.0f ? sum / total : 0.0f;
    return blend == SceneSet::Blend::Log ? std::exp(v) : v;
}

inline int nearestScene(const SceneSet& set, Point2 p) noexcept
{
    int best = -1;
    float bestD = 1.0e9f;
    for (int i = 0; i < set.numScenes; ++i)
    {
        const float dx = p.x - set.positions[static_cast<std::size_t>(i)].x;
        const float dy = p.y - set.positions[static_cast<std::size_t>(i)].y;
        const float d = dx * dx + dy * dy;
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}
}
