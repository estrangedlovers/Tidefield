#pragma once

#include "SceneSet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tf::engine {
struct TerrainPath
{
    static constexpr int kPoints = 128;

    int count = 0;
    std::array<Point2, kPoints> points {};
    std::uint64_t version = 0;

    Point2 at(float phase) const noexcept
    {
        if (count == 0)
            return { 0.5f, 0.5f };
        phase -= std::floor(phase);
        const float f = phase * static_cast<float>(count);
        const int i = std::min(static_cast<int>(f), count - 1);
        const float t = f - static_cast<float>(i);
        const auto& a = points[static_cast<std::size_t>(i)];
        const auto& b = points[static_cast<std::size_t>((i + 1) % count)];
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    }

    static TerrainPath build(const std::vector<Point2>& stroke, std::uint64_t version)
    {
        TerrainPath p;
        p.version = version;
        std::vector<Point2> pts;
        for (const auto& q : stroke)
            if (pts.empty() || std::hypot(q.x - pts.back().x, q.y - pts.back().y) > 1.0e-4f)
                pts.push_back({ std::clamp(q.x, 0.0f, 1.0f), std::clamp(q.y, 0.0f, 1.0f) });
        if (pts.size() < 2)
            return p;

        for (int pass = 0; pass < 2; ++pass)
        {
            const auto src = pts;
            const auto n = src.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto& a = src[(i + n - 1) % n];
                const auto& b = src[(i + 1) % n];
                pts[i] = { 0.25f * a.x + 0.5f * src[i].x + 0.25f * b.x, 0.25f * a.y + 0.5f * src[i].y + 0.25f * b.y };
            }
        }

        const auto n = pts.size();
        std::vector<float> cum(n + 1, 0.0f);
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto& a = pts[i];
            const auto& b = pts[(i + 1) % n];
            cum[i + 1] = cum[i] + std::hypot(b.x - a.x, b.y - a.y);
        }
        const float total = cum[n];
        if (total < 1.0e-3f)
            return p;
        std::size_t seg = 0;
        for (int k = 0; k < kPoints; ++k)
        {
            const float s = total * static_cast<float>(k) / static_cast<float>(kPoints);
            while (seg + 1 < n && cum[seg + 1] < s)
                ++seg;
            const float len = cum[seg + 1] - cum[seg];
            const float t = len > 0.0f ? (s - cum[seg]) / len : 0.0f;
            const auto& a = pts[seg];
            const auto& b = pts[(seg + 1) % n];
            p.points[static_cast<std::size_t>(k)] = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
        }
        p.count = kPoints;
        return p;
    }
};
}
