#pragma once

#include "ModMatrix.h"

#include <array>
#include <string>
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

    struct Macro
    {
        std::string name;
        std::vector<MacroTarget> targets;
    };
    const std::array<Macro, kNumMacros>& getMacros() const noexcept { return macros; }
    bool addMacroTarget(int macro, ParamIndex param, float from = 0.0f, float to = 0.5f);
    void removeMacroTarget(int macro, int index);
    void removeFromMacros(ParamIndex param);
    void setMacroRange(int macro, int index, float from, float to);
    void setMacroName(int macro, const std::string& name);
    void replaceMacros(const std::array<Macro, kNumMacros>& all);
    int macroFor(ParamIndex param) const;
    static std::string defaultMacroName(int macro);

private:
    void publish();
    void publishMacros();
    std::array<Macro, kNumMacros> macros;
    std::uint64_t macroVersion = 0;
    bool macrosDirty = false;

    Engine& engine;
    std::vector<Route> list;
    std::uint64_t version = 0;
    bool dirty = false;
};
}
