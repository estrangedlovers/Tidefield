#pragma once

#include "Layout.h"

#include <dsp/fx/ProcessorFactory.h>

#include <array>
#include <string>
#include <string_view>

namespace tf::engine {

class Engine;

/** Message-thread owner of which processor sits in each FX slot. Creating and
    preparing a processor allocates, so it happens here; the engine only receives
    ready-to-run objects and crossfades them in. */
class FxManager
{
public:
    explicit FxManager(Engine& engine);

    /** Loads `typeId` ("" = empty) into a slot. When applyDefaults is true the slot's
        controls jump (smoothed) to the processor's defaults, as a fresh load should;
        session recall passes false and sets the stored values itself. */
    void setType(int slot, std::string_view typeId, bool applyDefaults = true);

    const std::string& getType(int slot) const noexcept { return types[static_cast<std::size_t>(slot)]; }
    const dsp::ProcessorInfo* getInfo(int slot) const noexcept;

    /** Reverb on bus A, tape delay on bus B, everything else empty. */
    void loadDefaultLayout();

    /** Call regularly on the message thread: frees retired processors and retries
        loads that could not be sent yet. */
    void tick();

    static int findSlot(std::string_view slotId) noexcept;

private:
    bool trySend(int slot);

    Engine& engine;
    std::array<std::string, kNumFxSlots> types;
    std::array<int, kNumFxSlots> inFlight {};
    std::array<bool, kNumFxSlots> pending {};
};

} // namespace tf::engine
