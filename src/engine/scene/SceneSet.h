#pragma once

#include "../control/ControlEvent.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tf::engine {
inline constexpr int kMaxScenes = 32;

struct Point2 { float x = 0.5f; float y = 0.5f; };

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

    std::vector<Column> columns;

    std::vector<float> values;

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
}
