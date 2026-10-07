#include "SeasonManager.h"

#include "../Engine.h"

#include <algorithm>

namespace tf::engine {

SeasonManager::SeasonManager(Engine& e) : engine(e) {}

bool SeasonManager::valid(const Season& s) const
{
    if (s.param >= kNumParams)
        return false;
    return (engine.getRegistry().spec(s.param).flags & ParamFlag::kDiscrete) == 0;
}

bool SeasonManager::set(int index, const Season& season)
{
    if (index < 0 || index > static_cast<int>(list.size()) || ! valid(season))
        return false;
    auto s = season;
    s.depth = std::clamp(s.depth, -1.0f, 1.0f);
    s.periodSeconds = std::clamp(s.periodSeconds, 20.0f, 3600.0f);
    s.phase = std::clamp(s.phase, 0.0f, 1.0f);
    if (index == static_cast<int>(list.size()))
    {
        if (list.size() >= static_cast<std::size_t>(kMaxSeasons))
            return false;
        list.push_back(s);
    }
    else
    {
        list[static_cast<std::size_t>(index)] = s;
    }
    publish();
    return true;
}

void SeasonManager::remove(int index)
{
    if (index < 0 || index >= static_cast<int>(list.size()))
        return;
    list.erase(list.begin() + index);
    publish();
}

void SeasonManager::replaceAll(std::vector<Season> seasons)
{
    list.clear();
    for (const auto& s : seasons)
        if (list.size() < static_cast<std::size_t>(kMaxSeasons) && valid(s))
            list.push_back(s);
    publish();
}

void SeasonManager::tick()
{
    if (dirty)
        publish();
}

void SeasonManager::publish()
{
    auto set = std::make_unique<SeasonSet>();
    set->count = static_cast<int>(list.size());
    std::copy(list.begin(), list.end(), set->seasons.begin());
    set->version = ++version;
    dirty = ! engine.publishSeasons(std::move(set));
}

} // namespace tf::engine
