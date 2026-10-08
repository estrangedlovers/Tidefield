#pragma once

#include "Seasons.h"

#include <vector>

namespace tf::engine {
class Engine;

class SeasonManager
{
public:
    explicit SeasonManager(Engine& engine);

    const std::vector<Season>& getSeasons() const noexcept { return list; }

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
}
