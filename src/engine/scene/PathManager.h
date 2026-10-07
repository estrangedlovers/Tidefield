#pragma once

#include "TerrainPath.h"

#include <vector>

namespace tf::engine {

class Engine;

/** Message-thread owner of the drawn path: keeps the stroke as drawn (for sessions
    and the UI) and publishes the resampled loop to the engine, retrying from tick()
    if the channel is full. */
class PathManager
{
public:
    explicit PathManager(Engine& engine);

    void set(std::vector<Point2> stroke);
    void clear() { set({}); }
    const std::vector<Point2>& getStroke() const noexcept { return stroke; }
    bool hasPath() const noexcept { return ! stroke.empty(); }
    /** Changes whenever the path does (for views that cache it). */
    std::uint64_t getVersion() const noexcept { return version; }
    void tick();

private:
    void publish();

    Engine& engine;
    std::vector<Point2> stroke;
    std::uint64_t version = 0;
    bool dirty = false;
};

} // namespace tf::engine
