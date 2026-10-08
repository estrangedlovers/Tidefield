#pragma once

#include "Layout.h"

#include <dsp/fx/ProcessorFactory.h>

#include <array>
#include <string>
#include <string_view>

namespace tf::engine {
class Engine;

class ExternalEffects
{
public:
    virtual ~ExternalEffects() = default;
    virtual bool handles(std::string_view typeId) const = 0;
    virtual const dsp::ProcessorInfo* find(std::string_view typeId) const = 0;
    virtual dsp::ProcessorPtr create(int slot, std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec) = 0;
    virtual std::string captureState(int slot) const = 0;
};

class FxManager
{
public:
    explicit FxManager(Engine& engine);

    void setType(int slot, std::string_view typeId, bool applyDefaults = true, std::string state = {});
    void setExternal(ExternalEffects* external) noexcept { externalEffects = external; }
    bool isExternal(int slot) const;
    bool knows(std::string_view typeId) const;
    std::string getState(int slot) const;

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
    std::array<std::string, kNumFxSlots> states;
    ExternalEffects* externalEffects = nullptr;
};
}
