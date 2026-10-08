#pragma once

#include "../control/ControlEvent.h"

namespace tf::engine {
enum class Taper : unsigned char { Linear, Log, Decibel };
enum class Smoothing : unsigned char { Linear, Exponential, LogExponential };

namespace ParamFlag {
inline constexpr unsigned kNone = 0;
inline constexpr unsigned kTerrainBound = 1u << 0;
inline constexpr unsigned kMidiLearnable = 1u << 1;
inline constexpr unsigned kTideScaled = 1u << 2;
inline constexpr unsigned kPerformance = 1u << 3;
inline constexpr unsigned kDiscrete = 1u << 4;
}

#define TF_TB_ML (kTerrainBound | kMidiLearnable)

#define TF_STRIP(X, Name, id, level, sendA, sendB, azimuth)                                                                 \
    X(Name##Level, id ".level", "Level",       -60.0f, 6.0f, level, Decibel, Linear, 0.05f, "dB", TF_TB_ML | kPerformance) \
    X(Name##Pan,   id ".pan",   "Pan",          -1.0f, 1.0f,  0.0f, Linear,  Linear, 0.05f, "",   TF_TB_ML)                 \
    X(Name##Width, id ".width", "Width",         0.0f, 2.0f,  1.0f, Linear,  Linear, 0.05f, "",   TF_TB_ML)                 \
    X(Name##SendA, id ".sendA", "Reverb Send", -60.0f, 0.0f, sendA, Decibel, Linear, 0.05f, "dB", TF_TB_ML)                 \
    X(Name##SendB, id ".sendB", "Delay Send",  -60.0f, 0.0f, sendB, Decibel, Linear, 0.05f, "dB", TF_TB_ML)                 \
    X(Name##Azimuth, id ".azimuth", "Direction", -180.0f, 180.0f, azimuth, Linear, Exponential, 0.3f, "deg", TF_TB_ML)

#define TF_FX_SLOT(X, Name, id)                                                                               \
    X(Name##P1,  id ".p1",  "Control 1", 0.0f, 1.0f, 0.5f, Linear, Exponential, 0.08f, "", TF_TB_ML)          \
    X(Name##P2,  id ".p2",  "Control 2", 0.0f, 1.0f, 0.5f, Linear, Exponential, 0.08f, "", TF_TB_ML)          \
    X(Name##P3,  id ".p3",  "Control 3", 0.0f, 1.0f, 0.5f, Linear, Exponential, 0.08f, "", TF_TB_ML)          \
    X(Name##P4,  id ".p4",  "Control 4", 0.0f, 1.0f, 0.5f, Linear, Exponential, 0.08f, "", TF_TB_ML)          \
    X(Name##P5,  id ".p5",  "Control 5", 0.0f, 1.0f, 0.5f, Linear, Exponential, 0.08f, "", TF_TB_ML)          \
    X(Name##P6,  id ".p6",  "Control 6", 0.0f, 1.0f, 0.5f, Linear, Exponential, 0.08f, "", TF_TB_ML)          \
    X(Name##Mix, id ".mix", "Mix",       0.0f, 1.0f, 1.0f, Linear, Linear,      0.05f, "", TF_TB_ML)

#define TF_CLOUD(X, Name, id)                                                                                                      \
    X(Name##Density,     id ".density",     "Density",      0.5f, 200.0f, 12.0f, Log,    LogExponential, 0.3f, "/s", TF_TB_ML | kPerformance) \
    X(Name##GrainMs,     id ".grainMs",     "Grain Size",  10.0f, 2000.0f, 180.0f, Log,  LogExponential, 0.3f, "ms", TF_TB_ML)  \
    X(Name##Position,    id ".position",    "Position",     0.0f, 1.0f,    0.3f, Linear, Exponential,    0.5f, "",   TF_TB_ML)  \
    X(Name##Spray,       id ".spray",       "Spray",        0.0f, 1.0f,    0.15f, Linear, Exponential,   0.3f, "",   TF_TB_ML)  \
    X(Name##Scan,        id ".scan",        "Scan",        -1.0f, 1.0f,    0.0f, Linear, Exponential,    0.5f, "",   TF_TB_ML | kTideScaled) \
    X(Name##Pitch,       id ".pitch",       "Pitch",      -24.0f, 24.0f,   0.0f, Linear, Exponential,    0.3f, "st", TF_TB_ML)  \
    X(Name##PitchSpread, id ".pitchSpread", "Detune",       0.0f, 1.0f,    0.1f, Linear, Exponential,    0.3f, "",   TF_TB_ML)  \
    X(Name##Harmonize,   id ".harmonize",   "Harmonize",    0.0f, 1.0f,    0.0f, Linear, Exponential,    0.3f, "",   TF_TB_ML)  \
    X(Name##Reverse,     id ".reverse",     "Reverse",      0.0f, 1.0f,    0.0f, Linear, Exponential,    0.3f, "",   TF_TB_ML)  \
    X(Name##Shape,       id ".shape",       "Envelope",     0.0f, 1.0f,    0.5f, Linear, Exponential,    0.3f, "",   TF_TB_ML)  \
    X(Name##Stereo,      id ".stereo",      "Stereo",       0.0f, 1.0f,    0.7f, Linear, Exponential,    0.3f, "",   TF_TB_ML)  \
    X(Name##Gravity,     id ".gravity",     "Gravity",      0.0f, 1.0f,    0.0f, Linear, Exponential,    0.3f, "",   TF_TB_ML)

#define TF_PARAM_LIST(X)                                                                                                         \
    X(MasterLevel,      "master.level",       "Master Level",    -60.0f,   6.0f,    0.0f, Decibel, Linear,         0.05f, "dB", kMidiLearnable | kPerformance) \
    X(MasterFadeSecs,   "master.fadeSeconds", "Fade Length",       0.5f, 120.0f,    8.0f, Log,     Linear,         0.0f,  "s",  kMidiLearnable | kPerformance) \
    X(MasterCeiling,    "master.ceiling",     "Limiter Ceiling",  -12.0f,  0.0f,   -1.0f, Linear,  Linear,         0.05f, "dB", kNone) \
    X(MasterAuto,       "master.auto",        "Auto Master",       0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(MasterAutoTarget, "master.autoTarget",  "Loudness",          0.0f,   2.0f,    1.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(MasterAutoAmount, "master.autoAmount",  "Correction",        0.0f,   1.0f,    0.6f, Linear,  Exponential,    0.5f,  "",   kMidiLearnable) \
    X(TerrainX,         "terrain.x",          "Terrain X",         0.0f,   1.0f,    0.5f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kPerformance) \
    X(TerrainY,         "terrain.y",          "Terrain Y",         0.0f,   1.0f,    0.5f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kPerformance) \
    X(TerrainGlide,     "terrain.glide",      "Glide",             0.05f, 30.0f,    1.5f, Log,     Linear,         0.0f,  "s",  kMidiLearnable | kPerformance) \
    X(TerrainFocus,     "terrain.focus",      "Focus",             1.0f,   6.0f,    2.5f, Linear,  Exponential,    0.3f,  "",   kMidiLearnable) \
    X(TerrainWander,    "terrain.wander",     "Wander",            0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.5f,  "",   kMidiLearnable | kPerformance) \
    X(TerrainWanderRate,"terrain.wanderRate", "Wander Rate",       0.002f, 0.5f,    0.03f, Log,    LogExponential, 0.5f,  "Hz", kMidiLearnable | kTideScaled) \
    X(TerrainWanderStyle,"terrain.wanderStyle","Wander Style",     0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(TideRate,         "tide.rate",          "Tide",              0.05f,  8.0f,    1.0f, Log,     LogExponential, 1.5f,  "x",  kMidiLearnable | kPerformance | kTerrainBound) \
    X(HarmonyRoot,      "harmony.root",       "Key",               0.0f,  11.0f,    2.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete | kPerformance) \
    X(HarmonyScale,     "harmony.scale",      "Scale",             0.0f,  11.0f,    1.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete | kPerformance) \
    X(HarmonyGravity,   "harmony.gravity",    "Gravity",           0.0f,   1.0f,    0.6f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML | kPerformance) \
    X(HarmonyMorph,     "harmony.morph",      "Key Morph",         0.5f,  60.0f,    8.0f, Log,     Linear,         0.0f,  "s",  kMidiLearnable) \
    X(MediumType,       "medium.type",        "Medium",            0.0f,   3.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete | kPerformance) \
    X(MediumAge,        "medium.age",         "Age",               0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(MediumNoise,      "medium.noise",       "Noise",             0.0f,   1.0f,    0.4f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(MediumWobble,     "medium.wobble",      "Wobble",            0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(MediumDrive,      "medium.drive",       "Drive",             0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(MediumMix,        "medium.mix",         "Medium Mix",        0.0f,   1.0f,    1.0f, Linear,  Linear,         0.05f, "",   TF_TB_ML) \
    X(CatchSeconds,     "catch.seconds",      "Catch Length",      5.0f,  30.0f,   20.0f, Linear,  Linear,         0.0f,  "s",  kMidiLearnable | kPerformance) \
    X(CatchSource,      "catch.source",       "Catch Source",      0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(CatchTarget,      "catch.target",       "Catch Into",        0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(SwellHold,        "swell.hold",         "Swell",             0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(SwellDepth,       "swell.depth",        "Swell Depth",       0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(SwellAttack,      "swell.attack",       "Swell Rise",        0.2f,  20.0f,    3.0f, Log,     Linear,         0.0f,  "s",  kMidiLearnable) \
    X(SwellRelease,     "swell.release",      "Swell Ebb",         0.5f,  60.0f,   10.0f, Log,     Linear,         0.0f,  "s",  kMidiLearnable) \
    X(PerformColour,    "perform.colour",     "Colour",           -1.0f,   1.0f,    0.0f, Linear,  Exponential,    0.15f, "",   TF_TB_ML | kPerformance) \
    X(PerformSpace,     "perform.space",      "Space",            -1.0f,   1.0f,    0.0f, Linear,  Exponential,    0.15f, "",   TF_TB_ML | kPerformance) \
    X(HushHold,         "hush.hold",          "Hush",              0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(HushDepth,        "hush.depth",         "Hush Depth",        0.0f,   1.0f,    0.6f, Linear,  Exponential,    0.2f,  "",   kMidiLearnable) \
    X(SlowHold,         "slow.hold",          "Slow Time",         0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(SeasonsDepth,     "seasons.depth",      "Seasons",           0.0f,   1.0f,    1.0f, Linear,  Exponential,    0.5f,  "",   kMidiLearnable | kPerformance) \
    X(LoopsOn,          "loops.on",           "Cycles",            0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(LoopsCount,       "loops.count",        "Voices",            1.0f,   8.0f,    5.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(LoopsRate,        "loops.rate",         "Pace",              0.25f,  4.0f,    1.0f, Log,     LogExponential, 0.5f,  "x",  TF_TB_ML | kTideScaled) \
    X(LoopsDensity,     "loops.density",      "Density",           0.0f,   1.0f,    0.85f, Linear, Exponential,    0.3f,  "",   TF_TB_ML) \
    X(LoopsRegister,    "loops.register",     "Register",         36.0f,  84.0f,   60.0f, Linear,  Linear,         0.0f,  "st", TF_TB_ML) \
    X(LoopsSpread,      "loops.spread",       "Spread",            0.0f,   3.0f,    1.5f, Linear,  Linear,         0.0f,  "oct", TF_TB_ML) \
    X(LoopsVelocity,    "loops.velocity",     "Velocity",          0.0f,   1.0f,    0.6f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(LoopsTarget,      "loops.target",       "Play Into",         0.0f,   2.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(SyncOn,           "sync.on",            "Tempo Sync",        0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(SyncBpm,          "sync.bpm",           "Tempo",            40.0f, 200.0f,   90.0f, Linear,  Linear,         0.0f,  "BPM", kMidiLearnable) \
    X(LoopsPattern,     "loops.pattern",      "Pattern",           0.0f,  99.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(BusALevel,        "busA.level",         "Reverb Return",   -60.0f,   6.0f,    0.0f, Decibel, Linear,         0.05f, "dB", TF_TB_ML | kPerformance) \
    X(BusBLevel,        "busB.level",         "Delay Return",    -60.0f,   6.0f,    0.0f, Decibel, Linear,         0.05f, "dB", TF_TB_ML | kPerformance) \
    X(DroneRoot,        "drone.root",         "Root Note",        24.0f,  72.0f,   38.0f, Linear,  Exponential,    0.8f,  "st", TF_TB_ML) \
    X(DroneDetune,      "drone.detune",       "Detune",            0.0f,  50.0f,    8.0f, Linear,  Exponential,    0.3f,  "ct", TF_TB_ML) \
    X(DroneShape,       "drone.shape",        "Shape",             0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(DroneCutoff,      "drone.cutoff",       "Brightness",       60.0f, 12000.0f, 900.0f, Log,    LogExponential, 0.3f,  "Hz", TF_TB_ML | kPerformance) \
    X(DroneResonance,   "drone.resonance",    "Resonance",         0.0f,   0.95f,   0.2f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(DroneNoise,       "drone.noise",        "Breath",            0.0f,   1.0f,    0.1f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(DroneDriftDepth,  "drone.driftDepth",   "Drift Depth",       0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML) \
    X(DroneDriftRate,   "drone.driftRate",    "Drift Rate",        0.005f, 2.0f,    0.05f, Log,    LogExponential, 0.5f,  "Hz", TF_TB_ML | kTideScaled) \
    X(DroneDensity,     "drone.density",      "Density",           1.0f,   6.0f,    3.0f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML | kPerformance) \
    X(DroneEvolve,      "drone.evolve",       "Evolve",            0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML) \
    X(DroneSpread,      "drone.spread",       "Spread",            0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(DroneGravity,     "drone.gravity",      "Gravity",           0.0f,   1.0f,    1.0f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(DroneWave,        "drone.wave",         "Wave",              0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(DroneChord,       "drone.chord",        "Chord",             0.0f,   7.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(DroneSub,         "drone.sub",          "Sub",               0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(DroneFmRatio,     "drone.fmRatio",      "FM Ratio",          0.5f,   8.0f,    2.0f, Log,     LogExponential, 0.3f,  "x",  TF_TB_ML) \
    X(DroneTilt,        "drone.tilt",         "Tilt",              0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(DroneFilterType,  "drone.filterType",   "Filter",            0.0f,   2.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(DroneKeyTrack,    "drone.keyTrack",     "Key Track",         0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(DroneVibrato,     "drone.vibrato",      "Vibrato",           0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(DroneVibratoRate, "drone.vibratoRate",  "Vibrato Rate",      0.05f,  9.0f,    4.5f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(DroneTremolo,     "drone.tremolo",      "Tremolo",           0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(DroneTremoloRate, "drone.tremoloRate",  "Tremolo Rate",      0.02f,  9.0f,    0.2f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(DroneGlide,       "drone.glide",        "Glide",             0.05f, 30.0f,    1.2f, Log,     LogExponential, 0.2f,  "s",  TF_TB_ML) \
    X(DroneRevoice,     "drone.revoice",      "Revoice Time",      0.5f,  30.0f,    4.0f, Log,     LogExponential, 0.2f,  "s",  TF_TB_ML) \
    X(DroneDrive,       "drone.drive",        "Drive",             0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(DroneBreathTone,  "drone.breathTone",   "Breath Tone",       0.0f,   1.0f,    1.0f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    TF_STRIP(X, Drone,     "drone",     0.0f,  -14.0f, -60.0f, 0.0f) \
    TF_CLOUD(X, Cloud1, "cloud1") \
    TF_CLOUD(X, Cloud2, "cloud2") \
    TF_CLOUD(X, Cloud3, "cloud3") \
    TF_CLOUD(X, Cloud4, "cloud4") \
    TF_STRIP(X, Cloud1,    "cloud1",    -3.0f, -10.0f, -24.0f, -60.0f) \
    TF_STRIP(X, Cloud2,    "cloud2",    -3.0f, -10.0f, -24.0f, 60.0f) \
    TF_STRIP(X, Cloud3,    "cloud3",    -3.0f, -10.0f, -24.0f, -120.0f) \
    TF_STRIP(X, Cloud4,    "cloud4",    -3.0f, -10.0f, -24.0f, 120.0f) \
    X(ResRoot,          "res.root",           "Root Note",        24.0f,  84.0f,   50.0f, Linear,  Exponential,    0.8f,  "st", TF_TB_ML) \
    X(ResModes,         "res.modes",          "Modes",             1.0f,  24.0f,   16.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(ResStructure,     "res.structure",      "Structure",         0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.8f,  "",   TF_TB_ML | kPerformance) \
    X(ResDecay,         "res.decay",          "Decay",             0.1f,  60.0f,    6.0f, Log,     LogExponential, 0.5f,  "s",  TF_TB_ML) \
    X(ResBrightness,    "res.brightness",     "Brightness",        0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(ResRain,          "res.rain",           "Rain",              0.0f,   1.0f,    0.25f, Linear, Exponential,    0.5f,  "",   TF_TB_ML | kTideScaled | kPerformance) \
    X(ResRainColour,    "res.rainColour",     "Rain Colour",       0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(ResSpread,        "res.spread",         "Spread",            0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(ResGravity,       "res.gravity",        "Gravity",           0.0f,   1.0f,    1.0f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(ResExciteInput,   "res.exciteInput",    "From Input",        0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(ResExciteDrone,   "res.exciteDrone",    "From Drone",        0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(ResExciteClouds,  "res.exciteClouds",   "From Clouds",       0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    TF_STRIP(X, Res,       "res",       -4.0f,  -8.0f, -30.0f, 180.0f) \
    X(InputChannel,     "input.channel",      "Channel",           0.0f,   2.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kDiscrete) \
    X(InputGain,        "input.gain",         "Input Gain",      -24.0f,  24.0f,    0.0f, Linear,  Linear,         0.05f, "dB", kMidiLearnable) \
    X(InputHighPass,    "input.highPass",     "Low Cut",          20.0f, 400.0f,   40.0f, Log,     LogExponential, 0.1f,  "Hz", kMidiLearnable) \
    X(InputGate,        "input.gate",         "Gate",            -90.0f, -20.0f,  -70.0f, Linear,  Linear,         0.05f, "dB", kMidiLearnable) \
    X(InputArmed,       "input.armed",        "Monitor",           0.0f,   1.0f,    0.0f, Linear,  Linear,         0.03f, "",   kMidiLearnable | kDiscrete | kPerformance) \
    TF_STRIP(X, Input,     "input",     0.0f, -14.0f, -18.0f, -90.0f) \
    X(InputFreeze,      "input.freeze",       "Freeze",            0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(InputFreezeLevel, "input.freezeLevel",  "Freeze Level",    -24.0f,   6.0f,    0.0f, Linear,  Linear,         0.05f, "dB", TF_TB_ML) \
    X(InputFreezeDrift, "input.freezeDrift",  "Freeze Drift",      0.0f,   1.0f,    0.35f, Linear, Exponential,    0.2f,  "",   TF_TB_ML) \
    X(BloomTransform,   "bloom.transform",    "Transform",         0.0f,   5.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete | kPerformance) \
    X(BloomAmount,      "bloom.amount",       "Amount",            0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML | kPerformance) \
    X(BloomLength,      "bloom.length",       "Length",            0.5f,  30.0f,    8.0f, Log,     LogExponential, 0.2f,  "s",  TF_TB_ML) \
    X(BloomAttack,      "bloom.attack",       "Attack",            0.005f, 4.0f,    0.05f, Log,    LogExponential, 0.1f,  "s",  TF_TB_ML) \
    X(BloomRelease,     "bloom.release",      "Release",           0.1f,  12.0f,    2.5f, Log,     LogExponential, 0.1f,  "s",  TF_TB_ML) \
    X(BloomRoot,        "bloom.root",         "Sample Root",      24.0f,  96.0f,   60.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(BloomPitch,       "bloom.pitch",        "Pitch",           -24.0f,  24.0f,    0.0f, Linear,  Exponential,    0.1f,  "st", TF_TB_ML) \
    X(BloomTone,        "bloom.tone",         "Tone",              0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(BloomSpread,      "bloom.spread",       "Spread",            0.0f,   1.0f,    0.6f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(BloomRandom,      "bloom.random",       "Random",            0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(BloomPosition,    "bloom.position",     "Position",          0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(BloomGravity,     "bloom.gravity",      "Gravity",           0.0f,   1.0f,    1.0f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(ResExciteBloom,   "res.exciteBloom",    "From Bloom",        0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    TF_STRIP(X, Bloom,     "bloom",     0.0f,  -6.0f, -14.0f, 30.0f) \
    X(LoopSource,       "loop.source",        "Source",            0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(LoopErosion,      "loop.erosion",       "Erosion",           0.0f,   1.0f,    0.4f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML | kPerformance) \
    X(LoopFlakes,       "loop.flakes",        "Flakes",            0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.2f,  "",   TF_TB_ML) \
    X(LoopOverdub,      "loop.overdub",       "Overdub",           0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    TF_STRIP(X, Loop,      "loop",      0.0f, -10.0f, -60.0f, -150.0f) \
    X(WeatherWind,      "weather.wind",       "Wind",              0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML | kPerformance) \
    X(WeatherRain,      "weather.rain",       "Rain",              0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML | kPerformance) \
    X(WeatherSurf,      "weather.surf",       "Surf",              0.0f,   1.0f,    0.0f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML | kPerformance) \
    X(WeatherGust,      "weather.gust",       "Gusts",             0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML | kTideScaled) \
    X(WeatherTone,      "weather.tone",       "Tone",              0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    X(WeatherDistance,  "weather.distance",   "Distance",          0.0f,   1.0f,    0.3f, Linear,  Exponential,    0.5f,  "",   TF_TB_ML) \
    TF_STRIP(X, Weather,   "weather",  -4.0f, -14.0f, -60.0f, 150.0f) \
    X(FreezeOn,         "freeze.on",          "Freeze All",        0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete | kPerformance) \
    X(FreezeDuck,       "freeze.duck",        "Duck the Mix",      0.0f,   1.0f,    0.6f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(FreezeTexture,    "freeze.texture",     "Texture",           0.0f,   1.0f,    0.5f, Linear,  Exponential,    0.3f,  "",   TF_TB_ML) \
    TF_STRIP(X, Freeze,    "freeze",    0.0f, -10.0f, -60.0f, -30.0f) \
    TF_FX_SLOT(X, DroneFx1,  "drone.fx1")  TF_FX_SLOT(X, DroneFx2,  "drone.fx2")  \
    TF_FX_SLOT(X, Cloud1Fx1, "cloud1.fx1") TF_FX_SLOT(X, Cloud1Fx2, "cloud1.fx2") \
    TF_FX_SLOT(X, Cloud2Fx1, "cloud2.fx1") TF_FX_SLOT(X, Cloud2Fx2, "cloud2.fx2") \
    TF_FX_SLOT(X, Cloud3Fx1, "cloud3.fx1") TF_FX_SLOT(X, Cloud3Fx2, "cloud3.fx2") \
    TF_FX_SLOT(X, Cloud4Fx1, "cloud4.fx1") TF_FX_SLOT(X, Cloud4Fx2, "cloud4.fx2") \
    TF_FX_SLOT(X, ResFx1,    "res.fx1")    TF_FX_SLOT(X, ResFx2,    "res.fx2")    \
    TF_FX_SLOT(X, InputFx1,  "input.fx1")  TF_FX_SLOT(X, InputFx2,  "input.fx2")  \
    TF_FX_SLOT(X, BloomFx1,  "bloom.fx1")  TF_FX_SLOT(X, BloomFx2,  "bloom.fx2")  \
    TF_FX_SLOT(X, LoopFx1,   "loop.fx1")   TF_FX_SLOT(X, LoopFx2,   "loop.fx2")   \
    TF_FX_SLOT(X, WeatherFx1,"weather.fx1") TF_FX_SLOT(X, WeatherFx2,"weather.fx2") \
    TF_FX_SLOT(X, FreezeFx1, "freeze.fx1") TF_FX_SLOT(X, FreezeFx2, "freeze.fx2") \
    TF_FX_SLOT(X, BusAFx1,   "busA.fx1")   TF_FX_SLOT(X, BusAFx2,   "busA.fx2")   \
    TF_FX_SLOT(X, BusBFx1,   "busB.fx1")   TF_FX_SLOT(X, BusBFx2,   "busB.fx2")   \
    TF_FX_SLOT(X, MasterFx1, "master.fx1") TF_FX_SLOT(X, MasterFx2, "master.fx2") \
    X(ModLfo1Rate,     "mod.lfo1.rate",     "Rate",            0.005f, 20.0f,   0.1f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(ModLfo1Shape,    "mod.lfo1.shape",    "Shape",             0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(ModLfo2Rate,     "mod.lfo2.rate",     "Rate",            0.005f, 20.0f,   0.03f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(ModLfo2Shape,    "mod.lfo2.shape",    "Shape",             0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(ModLfo3Rate,     "mod.lfo3.rate",     "Rate",            0.005f, 20.0f,   0.5f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(ModLfo3Shape,    "mod.lfo3.shape",    "Shape",             0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(ModLfo4Rate,     "mod.lfo4.rate",     "Rate",            0.005f, 20.0f,   0.011f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(ModLfo4Shape,    "mod.lfo4.shape",    "Shape",             0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   TF_TB_ML | kDiscrete) \
    X(ModRandom1Rate,  "mod.random1.rate",  "Rate",            0.01f,  10.0f,   0.2f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(ModRandom1Smooth,"mod.random1.smooth","Smooth",            0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(ModRandom2Rate,  "mod.random2.rate",  "Rate",            0.01f,  10.0f,   0.05f, Log,     LogExponential, 0.2f,  "Hz", TF_TB_ML) \
    X(ModRandom2Smooth,"mod.random2.smooth","Smooth",            0.0f,   1.0f,    0.7f, Linear,  Exponential,    0.1f,  "",   TF_TB_ML) \
    X(ModFollowAttack,  "mod.follow.attack",  "Attack",          0.001f,  1.0f,    0.02f, Log,    LogExponential, 0.1f,  "s",  kMidiLearnable) \
    X(ModFollowRelease, "mod.follow.release", "Release",         0.01f,   5.0f,    0.4f, Log,     LogExponential, 0.1f,  "s",  kMidiLearnable) \
    X(ModFollowGain,    "mod.follow.gain",    "Sensitivity",    -24.0f,  24.0f,    0.0f, Linear,  Linear,         0.05f, "dB", kMidiLearnable) \
    X(ModRoute1Depth, "mod.route1.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute2Depth, "mod.route2.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute3Depth, "mod.route3.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute4Depth, "mod.route4.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute5Depth, "mod.route5.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute6Depth, "mod.route6.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute7Depth, "mod.route7.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute8Depth, "mod.route8.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute9Depth, "mod.route9.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute10Depth, "mod.route10.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute11Depth, "mod.route11.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute12Depth, "mod.route12.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute13Depth, "mod.route13.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute14Depth, "mod.route14.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute15Depth, "mod.route15.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(ModRoute16Depth, "mod.route16.depth", "Depth", -1.0f, 1.0f, 0.0f, Linear, Linear, 0.05f, "", TF_TB_ML) \
    X(SyncSource,       "sync.source",        "Follow",            0.0f,   1.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kDiscrete) \
    X(SpaceMode,        "space.mode",         "Output",            0.0f,   4.0f,    0.0f, Linear,  Linear,         0.0f,  "",   kMidiLearnable | kDiscrete) \
    X(SpaceSpread,      "space.spread",       "Spread",            0.0f,   1.0f,    0.35f, Linear, Exponential,    0.3f,  "",   TF_TB_ML) \
    X(SpaceRotate,      "space.rotate",       "Rotate",          -30.0f,  30.0f,    0.0f, Linear,  Exponential,    0.5f,  "deg/s", kMidiLearnable)

enum class P : ParamIndex
{
#define TF_PARAM_ENUM(name, ...) name,
    TF_PARAM_LIST(TF_PARAM_ENUM)
#undef TF_PARAM_ENUM
    Count
};

inline constexpr ParamIndex kNumParams = static_cast<ParamIndex>(P::Count);

constexpr ParamIndex idx(P p) noexcept { return static_cast<ParamIndex>(p); }
}
