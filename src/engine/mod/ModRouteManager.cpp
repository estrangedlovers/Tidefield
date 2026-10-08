#include "ModRouteManager.h"

#include "../Engine.h"

#include <algorithm>

namespace tf::engine {
ModRouteManager::ModRouteManager(Engine& e) : engine(e) {}

bool ModRouteManager::canModulate(ParamIndex param) const
{
    if (param >= kNumParams)
        return false;
    const auto& spec = engine.getRegistry().spec(param);
    if ((spec.flags & ParamFlag::kDiscrete) != 0)
        return false;
    const auto first = idx(P::ModRoute1Depth);
    return param < first || param >= static_cast<ParamIndex>(first + kMaxModRoutes);
}

int ModRouteManager::freeSlot() const
{
    for (int s = 0; s < kMaxModRoutes; ++s)
        if (std::none_of(list.begin(), list.end(), [s](const Route& r) { return r.slot == s; }))
            return s;
    return -1;
}

int ModRouteManager::add(ModSource source, ParamIndex param, float depth)
{
    if (static_cast<int>(source) >= kNumModSources || ! canModulate(param))
        return -1;
    const int slot = freeSlot();
    if (slot < 0)
        return -1;
    list.push_back({ source, param, slot });
    engine.post(ControlEvent::setParam(static_cast<ParamIndex>(idx(P::ModRoute1Depth) + slot), std::clamp(depth, -1.0f, 1.0f)));
    publish();
    return static_cast<int>(list.size()) - 1;
}

void ModRouteManager::remove(int index)
{
    if (index < 0 || index >= static_cast<int>(list.size()))
        return;
    const int slot = list[static_cast<std::size_t>(index)].slot;
    list.erase(list.begin() + index);
    engine.post(ControlEvent::setParam(static_cast<ParamIndex>(idx(P::ModRoute1Depth) + slot), 0.0f));
    publish();
}

void ModRouteManager::replaceAll(const std::vector<Route>& routes)
{
    list.clear();
    for (const auto& r : routes)
    {
        const bool slotTaken = std::any_of(list.begin(), list.end(), [&r](const Route& x) { return x.slot == r.slot; });
        if (list.size() < static_cast<std::size_t>(kMaxModRoutes) && r.slot >= 0 && r.slot < kMaxModRoutes && ! slotTaken
            && static_cast<int>(r.source) < kNumModSources && canModulate(r.param))
            list.push_back(r);
    }
    publish();
}

void ModRouteManager::clear()
{
    list.clear();
    publish();
}

void ModRouteManager::tick()
{
    if (dirty)
        publish();
}

void ModRouteManager::publish()
{
    auto set = std::make_unique<ModRouteSet>();
    set->count = static_cast<int>(list.size());
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        set->routes[i] = { list[i].source, list[i].param };
        set->slot[i] = static_cast<std::uint8_t>(list[i].slot);
    }
    set->version = ++version;
    dirty = ! engine.publishModRoutes(std::move(set));
}
}
