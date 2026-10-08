#pragma once

#include "../control/Telemetry.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace tf::engine {
class Engine;

class CatchManager
{
public:
    explicit CatchManager(Engine& engine);

    bool handle(const EngineNotice& notice);

    std::function<void(int cloud, const std::string& name)> onCaught;
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
}
