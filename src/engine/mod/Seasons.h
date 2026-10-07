#pragma once

#include "../control/ControlEvent.h"

#include <array>
#include <cstdint>

namespace tf::engine {

/** A season: a very slow macro curve on one parameter (minutes per cycle). Offsets
    are in the parameter's normalised range, so depth 0.3 sweeps 30 % of the knob
    either way of where the performer left it. Time follows Tide. */
struct Season
{
    enum class Shape : std::uint8_t { Sine = 0, Triangle = 1, Drift = 2 };
    ParamIndex param = 0;
    float depth = 0.2f;           // -1..1, normalised
    float periodSeconds = 300.0f; // 20 s .. 1 hour
    Shape shape = Shape::Sine;
    float phase = 0.0f;           // 0..1 starting point
};

inline constexpr int kMaxSeasons = 8;

/** Immutable audio-thread form, published through SnapshotChannel. */
struct SeasonSet
{
    int count = 0;
    std::array<Season, kMaxSeasons> seasons {};
    std::uint64_t version = 0;
};

} // namespace tf::engine
