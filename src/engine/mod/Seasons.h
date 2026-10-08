#pragma once

#include "../control/ControlEvent.h"

#include <array>
#include <cstdint>

namespace tf::engine {
struct Season
{
    enum class Shape : std::uint8_t { Sine = 0, Triangle = 1, Drift = 2 };
    ParamIndex param = 0;
    float depth = 0.2f;
    float periodSeconds = 300.0f;
    Shape shape = Shape::Sine;
    float phase = 0.0f;
};

inline constexpr int kMaxSeasons = 8;

struct SeasonSet
{
    int count = 0;
    std::array<Season, kMaxSeasons> seasons {};
    std::uint64_t version = 0;
};
}
