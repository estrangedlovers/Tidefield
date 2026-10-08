#pragma once

#include "Layout.h"

#include <dsp/fx/ProcessorFactory.h>

#include <array>
#include <string>
#include <string_view>

namespace tf::engine {
class Engine;

class FxManager
{
public:
    explicit FxManager(Engine& engine);

    void setType(int slot, std::string_view typeId, bool applyDefaults = true);

    const std::string& getType(int slot) const noexcept { return types[static_cast<std::size_t>(slot)]; }
    const dsp::ProcessorInfo* getInfo(int slot) const noexcept;

    void loadDefaultLayout();

    void tick();

    static int findSlot(std::string_view slotId) noexcept;

private:
    bool trySend(int slot);

    Engine& engine;
    std::array<std::string, kNumFxSlots> types;
    std::array<int, kNumFxSlots> inFlight {};
    std::array<bool, kNumFxSlots> pending {};
};
}
