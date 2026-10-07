#pragma once

#include "../control/Telemetry.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace tf::engine {

class Engine;

/** Message-thread half of Catch. The engine answers Command::Catch with a
    CatchReady notice; pass every notice to handle(). The caught region is copied out
    of the engine's ring, faded at both ends, normalised, and loaded into a cloud:
    the chosen one (catch.target 1-4) or, on Auto, the first empty cloud, otherwise
    the one caught into longest ago. */
class CatchManager
{
public:
    explicit CatchManager(Engine& engine);

    /** Returns true if the notice was a catch (handled or rejected). */
    bool handle(const EngineNotice& notice);

    /** Called after a successful catch with the cloud index (0-3) and a name. */
    std::function<void(int cloud, const std::string& name)> onCaught;
    /** Called when a catch could not be used (silence, too late), with the reason. */
    std::function<void(const std::string& reason)> onRejected;

    static constexpr float kFadeSeconds = 0.02f;
    static constexpr float kTargetPeakDb = -3.0f;
    static constexpr float kSilenceDb = -70.0f;

private:
    int chooseCloud(int requested) const;

    Engine& engine;
    std::array<std::uint64_t, 4> lastCaught {};
    std::uint64_t counter = 0;
};

} // namespace tf::engine
