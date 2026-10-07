#pragma once

#include "../control/ControlEvent.h"

namespace tf::engine {

enum class Taper : unsigned char { Linear, Log, Decibel };
enum class Smoothing : unsigned char { Linear, Exponential, LogExponential };

/** Flags describing how a parameter participates in the rest of the system. */
namespace ParamFlag {
inline constexpr unsigned kNone = 0;
inline constexpr unsigned kTerrainBound = 1u << 0; // scenes and the terrain may set it
inline constexpr unsigned kMidiLearnable = 1u << 1;
inline constexpr unsigned kTideScaled = 1u << 2;    // a rate that follows Tide
inline constexpr unsigned kPerformance = 1u << 3;   // shown in the performance view
inline constexpr unsigned kDiscrete = 1u << 4;      // integer choice; scenes pick, never blend
} // namespace ParamFlag

// clang-format off
// X(enumName, "stable.id", "Display Name", min, max, default, Taper, Smoothing, smoothSeconds, "unit", flags)
// IDs are persisted in session files: never rename one without adding a migration.
#define TF_PARAM_LIST(X)                                                                                                         \
    X(MasterLevel,      "master.level",       "Master Level",    -60.0f,   6.0f,    0.0f, Decibel, Linear,         0.05f, "dB", kMidiLearnable | kPerformance) \
    X(MasterFadeSecs,   "master.fadeSeconds", "Fade Length",       0.5f, 120.0f,    8.0f, Log,     Linear,         0.0f,  "s",  kMidiLearnable | kPerformance) \
    X(MasterCeiling,    "master.ceiling",     "Limiter Ceiling",  -12.0f,  0.0f,   -1.0f, Linear,  Linear,         0.05f, "dB", kNone) \
    X(TerrainX,         "terrain.x",          "Terrain X",         0.0f,   1.0f,    0.5f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kPerformance) \
    X(TerrainY,         "terrain.y",          "Terrain Y",         0.0f,   1.0f,    0.5f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kPerformance) \
    X(TerrainGlide,     "terrain.glide",      "Glide",             0.05f, 30.0f,    1.5f, Log,     Linear,         0.0f,  "s",  kMidiLearnable | kPerformance) \
    X(TerrainFocus,     "terrain.focus",      "Focus",             1.0f,   6.0f,    2.5f, Linear,  Exponential,    0.3f,  "",   kMidiLearnable) \
    X(TerrainWander,    "terrain.wander",     "Wander",            0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.5f,  "",   kMidiLearnable | kPerformance) \
    X(TerrainWanderRate,"terrain.wanderRate", "Wander Rate",       0.002f, 0.5f,    0.03f, Log,    LogExponential, 0.5f,  "Hz", kMidiLearnable | kTideScaled) \
    X(TerrainWanderStyle,"terrain.wanderStyle","Wander Style",     0.0f,   2.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(DroneLevel,       "drone.level",        "Drone Level",     -60.0f,   6.0f,    0.0f, Decibel, Linear,         0.05f, "dB", kTerrainBound | kMidiLearnable | kPerformance) \
    X(DronePan,         "drone.pan",          "Drone Pan",        -1.0f,   1.0f,    0.0f, Linear,  Linear,         0.05f, "",   kTerrainBound | kMidiLearnable) \
    X(DroneWidth,       "drone.width",        "Drone Width",       0.0f,   2.0f,    1.0f, Linear,  Linear,         0.05f, "",   kTerrainBound | kMidiLearnable) \
    X(DroneRoot,        "drone.root",         "Root Note",        24.0f,  72.0f,   38.0f, Linear,  Exponential,    0.8f,  "st", kTerrainBound | kMidiLearnable) \
    X(DroneDetune,      "drone.detune",       "Detune",            0.0f,  50.0f,    8.0f, Linear,  Exponential,    0.3f,  "ct", kTerrainBound | kMidiLearnable) \
    X(DroneShape,       "drone.shape",        "Shape",             0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.3f,  "",   kTerrainBound | kMidiLearnable) \
    X(DroneCutoff,      "drone.cutoff",       "Brightness",       60.0f, 12000.0f, 900.0f, Log,    LogExponential, 0.3f,  "Hz", kTerrainBound | kMidiLearnable | kPerformance) \
    X(DroneResonance,   "drone.resonance",    "Resonance",         0.0f,   0.95f,   0.2f, Linear,  Exponential,    0.2f,  "",   kTerrainBound | kMidiLearnable) \
    X(DroneNoise,       "drone.noise",        "Breath",            0.0f,   1.0f,    0.1f, Linear,  Exponential,    0.2f,  "",   kTerrainBound | kMidiLearnable) \
    X(DroneDriftDepth,  "drone.driftDepth",   "Drift Depth",       0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.5f,  "",   kTerrainBound | kMidiLearnable) \
    X(DroneDriftRate,   "drone.driftRate",    "Drift Rate",        0.005f, 2.0f,    0.05f, Log,    LogExponential, 0.5f,  "Hz", kTerrainBound | kMidiLearnable | kTideScaled) \
    X(DroneDensity,     "drone.density",      "Density",           1.0f,   6.0f,    3.0f, Linear,  Exponential,    0.5f,  "",   kTerrainBound | kMidiLearnable | kPerformance) \
    X(DroneEvolve,      "drone.evolve",       "Evolve",            0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.5f,  "",   kTerrainBound | kMidiLearnable) \
    X(DroneSpread,      "drone.spread",       "Spread",            0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.3f,  "",   kTerrainBound | kMidiLearnable)
// clang-format on

enum class P : ParamIndex
{
#define TF_PARAM_ENUM(name, ...) name,
    TF_PARAM_LIST(TF_PARAM_ENUM)
#undef TF_PARAM_ENUM
    Count
};

inline constexpr ParamIndex kNumParams = static_cast<ParamIndex>(P::Count);

constexpr ParamIndex idx(P p) noexcept { return static_cast<ParamIndex>(p); }

} // namespace tf::engine
