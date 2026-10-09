#pragma once

#include "../control/ControlEvent.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace tf::engine {
enum class ModSource : std::uint8_t
{
    Lfo1,
    Lfo2,
    Lfo3,
    Lfo4,
    Random1,
    Random2,
    InputLevel,
    InputBrightness,
    MixLevel,
    Velocity,
    NotePitch,
    ModWheel,
    Pressure,
    TerrainX,
    TerrainY,
    Count
};

inline constexpr int kNumModSources = static_cast<int>(ModSource::Count);
inline constexpr int kMaxModRoutes = 16;
inline constexpr int kNumLfos = 4;
inline constexpr int kNumRandoms = 2;

struct ModSourceInfo
{
    const char* id;
    const char* name;
    bool bipolar;
};

inline constexpr std::array<ModSourceInfo, kNumModSources> kModSources { {
    { "lfo1", "LFO 1", true },
    { "lfo2", "LFO 2", true },
    { "lfo3", "LFO 3", true },
    { "lfo4", "LFO 4", true },
    { "random1", "Random 1", true },
    { "random2", "Random 2", true },
    { "inputLevel", "Input level", false },
    { "inputBrightness", "Input brightness", false },
    { "mixLevel", "Mix level", false },
    { "velocity", "Velocity", false },
    { "notePitch", "Note pitch", false },
    { "modWheel", "Mod wheel", false },
    { "pressure", "Pressure", false },
    { "terrainX", "Terrain X", false },
    { "terrainY", "Terrain Y", false },
} };

inline constexpr std::array<const char*, 5> kLfoShapeNames { "Sine", "Triangle", "Ramp", "Square", "Steps" };

inline int modSourceFromId(std::string_view id) noexcept
{
    for (int s = 0; s < kNumModSources; ++s)
        if (id == kModSources[static_cast<std::size_t>(s)].id)
            return s;
    return -1;
}

struct ModRoute
{
    ModSource source = ModSource::Lfo1;
    ParamIndex param = 0;
};

inline constexpr int kNumMacros = 8;
inline constexpr int kMaxMacroTargets = 8;

struct MacroTarget
{
    ParamIndex param = 0;
    float from = 0.0f;
    float to = 0.5f;
};

struct MacroSet
{
    std::array<std::array<MacroTarget, kMaxMacroTargets>, kNumMacros> targets {};
    std::array<int, kNumMacros> count {};
    std::uint64_t version = 0;
};

struct ModRouteSet
{
    int count = 0;
    std::array<ModRoute, kMaxModRoutes> routes {};
    std::array<std::uint8_t, kMaxModRoutes> slot {};
    std::uint64_t version = 0;
};
}
