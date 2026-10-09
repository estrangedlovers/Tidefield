#pragma once

#include "../params/ParamDefs.h"

#include <array>

namespace tf::engine {
enum class StripId : int { Drone, Cloud1, Cloud2, Cloud3, Cloud4, Resonator, Input, Bloom, Loop, Weather, Freeze, Guest, Count };
inline constexpr int kNumStrips = static_cast<int>(StripId::Count);
inline constexpr int kNumClouds = 4;

struct StripInfo
{
    const char* id;
    const char* name;
    P level, pan, width, sendA, sendB, azimuth;
    P fx1, fx2;
};

inline constexpr std::array<StripInfo, kNumStrips> kStrips { {
    { "drone",  "Drone",     P::DroneLevel,  P::DronePan,  P::DroneWidth,  P::DroneSendA,  P::DroneSendB,  P::DroneAzimuth, P::DroneFx1P1,  P::DroneFx2P1 },
    { "cloud1", "Cloud 1",   P::Cloud1Level, P::Cloud1Pan, P::Cloud1Width, P::Cloud1SendA, P::Cloud1SendB, P::Cloud1Azimuth, P::Cloud1Fx1P1, P::Cloud1Fx2P1 },
    { "cloud2", "Cloud 2",   P::Cloud2Level, P::Cloud2Pan, P::Cloud2Width, P::Cloud2SendA, P::Cloud2SendB, P::Cloud2Azimuth, P::Cloud2Fx1P1, P::Cloud2Fx2P1 },
    { "cloud3", "Cloud 3",   P::Cloud3Level, P::Cloud3Pan, P::Cloud3Width, P::Cloud3SendA, P::Cloud3SendB, P::Cloud3Azimuth, P::Cloud3Fx1P1, P::Cloud3Fx2P1 },
    { "cloud4", "Cloud 4",   P::Cloud4Level, P::Cloud4Pan, P::Cloud4Width, P::Cloud4SendA, P::Cloud4SendB, P::Cloud4Azimuth, P::Cloud4Fx1P1, P::Cloud4Fx2P1 },
    { "res",    "Resonator", P::ResLevel,    P::ResPan,    P::ResWidth,    P::ResSendA,    P::ResSendB,    P::ResAzimuth, P::ResFx1P1,    P::ResFx2P1 },
    { "input",  "Live Input", P::InputLevel, P::InputPan,  P::InputWidth,  P::InputSendA,  P::InputSendB,  P::InputAzimuth, P::InputFx1P1,  P::InputFx2P1 },
    { "bloom",  "Bloom",     P::BloomLevel,  P::BloomPan,  P::BloomWidth,  P::BloomSendA,  P::BloomSendB,  P::BloomAzimuth, P::BloomFx1P1,  P::BloomFx2P1 },
    { "loop",   "Loop",      P::LoopLevel,   P::LoopPan,   P::LoopWidth,   P::LoopSendA,   P::LoopSendB,   P::LoopAzimuth, P::LoopFx1P1,   P::LoopFx2P1 },
    { "weather", "Weather",  P::WeatherLevel, P::WeatherPan, P::WeatherWidth, P::WeatherSendA, P::WeatherSendB, P::WeatherAzimuth, P::WeatherFx1P1, P::WeatherFx2P1 },
    { "freeze", "Freeze",    P::FreezeLevel, P::FreezePan, P::FreezeWidth, P::FreezeSendA, P::FreezeSendB, P::FreezeAzimuth, P::FreezeFx1P1, P::FreezeFx2P1 },
    { "guest",  "Guest",     P::GuestLevel,  P::GuestPan,  P::GuestWidth,  P::GuestSendA,  P::GuestSendB,  P::GuestAzimuth, P::GuestFx1P1,  P::GuestFx2P1 },
} };

struct FxSlotInfo
{
    const char* id;
    const char* name;
    P firstParam;
};

inline constexpr int kNumFxSlots = kNumStrips * 2 + 4 + 2;
inline constexpr int kBusASlot = kNumStrips * 2;
inline constexpr int kBusBSlot = kNumStrips * 2 + 2;
inline constexpr int kMasterSlot = kNumStrips * 2 + 4;

inline constexpr std::array<FxSlotInfo, kNumFxSlots> kFxSlots { {
    { "drone.fx1", "Drone insert 1", P::DroneFx1P1 },   { "drone.fx2", "Drone insert 2", P::DroneFx2P1 },
    { "cloud1.fx1", "Cloud 1 insert 1", P::Cloud1Fx1P1 }, { "cloud1.fx2", "Cloud 1 insert 2", P::Cloud1Fx2P1 },
    { "cloud2.fx1", "Cloud 2 insert 1", P::Cloud2Fx1P1 }, { "cloud2.fx2", "Cloud 2 insert 2", P::Cloud2Fx2P1 },
    { "cloud3.fx1", "Cloud 3 insert 1", P::Cloud3Fx1P1 }, { "cloud3.fx2", "Cloud 3 insert 2", P::Cloud3Fx2P1 },
    { "cloud4.fx1", "Cloud 4 insert 1", P::Cloud4Fx1P1 }, { "cloud4.fx2", "Cloud 4 insert 2", P::Cloud4Fx2P1 },
    { "res.fx1", "Resonator insert 1", P::ResFx1P1 },     { "res.fx2", "Resonator insert 2", P::ResFx2P1 },
    { "input.fx1", "Input insert 1", P::InputFx1P1 },     { "input.fx2", "Input insert 2", P::InputFx2P1 },
    { "bloom.fx1", "Bloom insert 1", P::BloomFx1P1 },     { "bloom.fx2", "Bloom insert 2", P::BloomFx2P1 },
    { "loop.fx1", "Loop insert 1", P::LoopFx1P1 },        { "loop.fx2", "Loop insert 2", P::LoopFx2P1 },
    { "weather.fx1", "Weather insert 1", P::WeatherFx1P1 }, { "weather.fx2", "Weather insert 2", P::WeatherFx2P1 },
    { "freeze.fx1", "Freeze insert 1", P::FreezeFx1P1 },  { "freeze.fx2", "Freeze insert 2", P::FreezeFx2P1 },
    { "guest.fx1", "Guest insert 1", P::GuestFx1P1 },     { "guest.fx2", "Guest insert 2", P::GuestFx2P1 },
    { "busA.fx1", "Reverb bus 1", P::BusAFx1P1 },         { "busA.fx2", "Reverb bus 2", P::BusAFx2P1 },
    { "busB.fx1", "Delay bus 1", P::BusBFx1P1 },          { "busB.fx2", "Delay bus 2", P::BusBFx2P1 },
    { "master.fx1", "Master insert 1", P::MasterFx1P1 },  { "master.fx2", "Master insert 2", P::MasterFx2P1 },
} };

inline constexpr std::array<P, kNumClouds> kCloudFirstParam { P::Cloud1Density, P::Cloud2Density, P::Cloud3Density, P::Cloud4Density };
}
