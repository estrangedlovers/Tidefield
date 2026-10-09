#include "ModRouteManager.h"

#include "../Engine.h"

#include <algorithm>

namespace tf::engine {
ModRouteManager::ModRouteManager(Engine& e) : engine(e)
{
    for (int m = 0; m < kNumMacros; ++m)
        macros[static_cast<std::size_t>(m)].name = defaultMacroName(m);
}

std::string ModRouteManager::defaultMacroName(int macro) { return "Macro " + std::to_string(macro + 1); }

bool ModRouteManager::canModulate(ParamIndex param) const
{
    if (param >= kNumParams)
        return false;
    const auto& spec = engine.getRegistry().spec(param);
    if ((spec.flags & ParamFlag::kDiscrete) != 0)
        return false;
    const auto first = idx(P::ModRoute1Depth);
    const auto firstMacro = idx(P::Macro1);
    return (param < first || param >= static_cast<ParamIndex>(first + kMaxModRoutes))
           && (param < firstMacro || param >= static_cast<ParamIndex>(firstMacro + kNumMacros));
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
    std::array<Macro, kNumMacros> empty;
    replaceMacros(empty);
}

void ModRouteManager::tick()
{
    if (dirty)
        publish();
    if (macrosDirty)
        publishMacros();
}

bool ModRouteManager::addMacroTarget(int macro, ParamIndex param, float from, float to)
{
    if (macro < 0 || macro >= kNumMacros || ! canModulate(param))
        return false;
    auto& targets = macros[static_cast<std::size_t>(macro)].targets;
    for (auto& t : targets)
        if (t.param == param)
        {
            t.from = std::clamp(from, -1.0f, 1.0f);
            t.to = std::clamp(to, -1.0f, 1.0f);
            publishMacros();
            return true;
        }
    if (static_cast<int>(targets.size()) >= kMaxMacroTargets)
        return false;
    targets.push_back({ param, std::clamp(from, -1.0f, 1.0f), std::clamp(to, -1.0f, 1.0f) });
    publishMacros();
    return true;
}

void ModRouteManager::removeMacroTarget(int macro, int index)
{
    if (macro < 0 || macro >= kNumMacros)
        return;
    auto& targets = macros[static_cast<std::size_t>(macro)].targets;
    if (index < 0 || index >= static_cast<int>(targets.size()))
        return;
    targets.erase(targets.begin() + index);
    publishMacros();
}

void ModRouteManager::removeFromMacros(ParamIndex param)
{
    for (auto& m : macros)
        m.targets.erase(std::remove_if(m.targets.begin(), m.targets.end(), [param](const MacroTarget& t) { return t.param == param; }),
                        m.targets.end());
    publishMacros();
}

void ModRouteManager::setMacroRange(int macro, int index, float from, float to)
{
    if (macro < 0 || macro >= kNumMacros)
        return;
    auto& targets = macros[static_cast<std::size_t>(macro)].targets;
    if (index < 0 || index >= static_cast<int>(targets.size()))
        return;
    targets[static_cast<std::size_t>(index)].from = std::clamp(from, -1.0f, 1.0f);
    targets[static_cast<std::size_t>(index)].to = std::clamp(to, -1.0f, 1.0f);
    publishMacros();
}

void ModRouteManager::setMacroName(int macro, const std::string& name)
{
    if (macro >= 0 && macro < kNumMacros)
        macros[static_cast<std::size_t>(macro)].name = name.empty() ? defaultMacroName(macro) : name;
}

void ModRouteManager::replaceMacros(const std::array<Macro, kNumMacros>& all)
{
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto& dst = macros[static_cast<std::size_t>(m)];
        const auto& src = all[static_cast<std::size_t>(m)];
        dst.name = src.name.empty() ? defaultMacroName(m) : src.name;
        dst.targets.clear();
        for (const auto& t : src.targets)
            if (static_cast<int>(dst.targets.size()) < kMaxMacroTargets && canModulate(t.param))
                dst.targets.push_back({ t.param, std::clamp(t.from, -1.0f, 1.0f), std::clamp(t.to, -1.0f, 1.0f) });
    }
    publishMacros();
}

int ModRouteManager::macroFor(ParamIndex param) const
{
    for (int m = 0; m < kNumMacros; ++m)
        for (const auto& t : macros[static_cast<std::size_t>(m)].targets)
            if (t.param == param)
                return m;
    return -1;
}

void ModRouteManager::publishMacros()
{
    auto set = std::make_unique<MacroSet>();
    for (int m = 0; m < kNumMacros; ++m)
    {
        const auto& targets = macros[static_cast<std::size_t>(m)].targets;
        set->count[static_cast<std::size_t>(m)] = static_cast<int>(targets.size());
        for (std::size_t k = 0; k < targets.size(); ++k)
            set->targets[static_cast<std::size_t>(m)][k] = targets[k];
    }
    set->version = ++macroVersion;
    macrosDirty = ! engine.publishMacros(std::move(set));
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
