#pragma once

#include "Seasons.h"

#include <vector>

namespace tf::engine {

class Engine;

/** Message-thread owner of the seasons: edits a plain list and publishes an
    immutable SeasonSet to the engine (retrying from tick() if the channel is full). */
class SeasonManager
{
public:
    explicit SeasonManager(Engine& engine);

    const std::vector<Season>& getSeasons() const noexcept { return list; }

    /** Replaces season `index`, or appends when index == size(). False when full,
        out of range, or the parameter cannot be modulated (discrete). */
    bool set(int index, const Season& season);
    void remove(int index);
    void replaceAll(std::vector<Season> seasons);
    void tick();

private:
    bool valid(const Season& s) const;
    void publish();

    Engine& engine;
    std::vector<Season> list;
    std::uint64_t version = 0;
    bool dirty = false;
};

} // namespace tf::engine
