#pragma once

#include "../control/ControlEvent.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tf::engine {

inline constexpr int kMaxScenes = 32;

struct Point2 { float x = 0.5f; float y = 0.5f; };

/** Immutable, audio-thread-ready form of the terrain: scene positions plus a dense
    value matrix for every terrain-bound, unpinned parameter. Built on the message
    thread by SceneManager and handed over through a SnapshotChannel. */
struct SceneSet
{
    enum class Blend : std::uint8_t { Linear, Log, Discrete };

    struct Column
    {
        ParamIndex param = 0;
        Blend blend = Blend::Linear;
    };

    std::uint64_t version = 0;
    int numScenes = 0;
    std::array<Point2, kMaxScenes> positions {};

    /** Parameters the terrain drives, in column order. */
    std::vector<Column> columns;

    /** numScenes rows of columns.size() values. Log columns hold log(value) so the
        weighted sum interpolates evenly in pitch/frequency. */
    std::vector<float> values;

    /** Same shape as values: 1 where the scene defines that parameter. A scene that
        does not mention a parameter has no opinion on it; only parameters at least one
        scene defines become columns at all. */
    std::vector<std::uint8_t> defined;

    float value(int scene, std::size_t column) const noexcept
    {
        return values[static_cast<std::size_t>(scene) * columns.size() + column];
    }

    bool isDefined(int scene, std::size_t column) const noexcept
    {
        return defined.empty() || defined[static_cast<std::size_t>(scene) * columns.size() + column] != 0;
    }
};

} // namespace tf::engine
