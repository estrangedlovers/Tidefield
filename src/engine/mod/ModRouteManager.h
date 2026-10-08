#pragma once

#include "ModMatrix.h"

#include <vector>

namespace tf::engine {
class Engine;

class ModRouteManager
{
public:
    struct Route
    {
        ModSource source = ModSource::Lfo1;
        ParamIndex param = 0;
        int slot = 0;
    };

    explicit ModRouteManager(Engine& engine);

    const std::vector<Route>& getRoutes() const noexcept { return list; }

    int add(ModSource source, ParamIndex param, float depth);
    void remove(int index);
    void replaceAll(const std::vector<Route>& routes);
    void clear();
    void tick();
    bool canModulate(ParamIndex param) const;
    int freeSlot() const;

private:
    void publish();

    Engine& engine;
    std::vector<Route> list;
    std::uint64_t version = 0;
    bool dirty = false;
};
}
