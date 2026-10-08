#include "PathManager.h"

#include "../Engine.h"

#include <memory>

namespace tf::engine {
PathManager::PathManager(Engine& e) : engine(e) {}

void PathManager::set(std::vector<Point2> s)
{
    stroke = std::move(s);
    if (stroke.size() > 1024)
        stroke.resize(1024);
    publish();
}

void PathManager::tick()
{
    if (dirty)
        publish();
}

void PathManager::publish()
{
    auto path = std::make_unique<TerrainPath>(TerrainPath::build(stroke, ++version));
    dirty = ! engine.publishPath(std::move(path));
}
}
