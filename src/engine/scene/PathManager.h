#pragma once

#include "TerrainPath.h"

#include <vector>

namespace tf::engine {
class Engine;

class PathManager
{
public:
    explicit PathManager(Engine& engine);

    void set(std::vector<Point2> stroke);
    void clear() { set({}); }
    const std::vector<Point2>& getStroke() const noexcept { return stroke; }
    bool hasPath() const noexcept { return ! stroke.empty(); }
    std::uint64_t getVersion() const noexcept { return version; }
    void tick();

private:
    void publish();

    Engine& engine;
    std::vector<Point2> stroke;
    std::uint64_t version = 0;
    bool dirty = false;
};
}
