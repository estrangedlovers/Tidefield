#include "FxManager.h"

#include "FxSlot.h"

#include "../Engine.h"

namespace tf::engine {
FxManager::FxManager(Engine& e) : engine(e) {}

const dsp::ProcessorInfo* FxManager::getInfo(int slot) const noexcept
{
    if (slot < 0 || slot >= kNumFxSlots)
        return nullptr;
    if (isExternal(slot))
        return externalEffects->find(types[static_cast<std::size_t>(slot)]);
    return dsp::ProcessorFactory::instance().find(types[static_cast<std::size_t>(slot)]);
}

bool FxManager::isExternal(int slot) const
{
    return slot >= 0 && slot < kNumFxSlots && externalEffects != nullptr && externalEffects->handles(types[static_cast<std::size_t>(slot)]);
}

bool FxManager::knows(std::string_view typeId) const
{
    if (externalEffects != nullptr && externalEffects->handles(typeId))
        return externalEffects->find(typeId) != nullptr;
    return dsp::ProcessorFactory::instance().find(typeId) != nullptr;
}

std::string FxManager::getState(int slot) const
{
    return isExternal(slot) ? externalEffects->captureState(slot) : std::string();
}

int FxManager::findSlot(std::string_view slotId) noexcept
{
    for (int i = 0; i < kNumFxSlots; ++i)
        if (slotId == kFxSlots[static_cast<std::size_t>(i)].id)
            return i;
    return -1;
}

void FxManager::setType(int slot, std::string_view typeId, bool applyDefaults, std::string state)
{
    if (slot < 0 || slot >= kNumFxSlots)
        return;
    const auto s = static_cast<std::size_t>(slot);
    types[s] = std::string(typeId);
    states[s] = std::move(state);
    pending[s] = ! trySend(slot);

    if (applyDefaults)
    {
        const auto first = idx(kFxSlots[s].firstParam);
        if (const auto* info = getInfo(slot))
            for (int c = 0; c < 6; ++c)
                engine.post(ControlEvent::setParam(static_cast<ParamIndex>(first + c), info->controls[static_cast<std::size_t>(c)].defaultValue));
        engine.post(ControlEvent::setParam(static_cast<ParamIndex>(first + 6), 1.0f));
    }
}

bool FxManager::trySend(int slot)
{
    const auto s = static_cast<std::size_t>(slot);
    if (inFlight[s] >= static_cast<int>(FxSlot::kQueueSize))
        return false;
    dsp::ProcessorPtr processor;
    if (isExternal(slot))
        processor = externalEffects->create(slot, types[s], states[s], engine.getProcessSpec());
    else
    {
        processor = dsp::ProcessorFactory::instance().create(types[s]);
        if (processor != nullptr)
            processor->prepare(engine.getProcessSpec());
    }
    const bool isReal = processor != nullptr;
    if (! engine.sendProcessor(slot, std::move(processor)))
        return false;
    if (isReal)
        ++inFlight[s];
    return true;
}

void FxManager::loadDefaultLayout()
{
    for (int i = 0; i < kNumFxSlots; ++i)
        if (! types[static_cast<std::size_t>(i)].empty())
            setType(i, "");
    setType(kBusASlot, "tf.reverb");
    setType(kBusBSlot, "tf.delay");
}

void FxManager::tick()
{
    for (int i = 0; i < kNumFxSlots; ++i)
    {
        const auto s = static_cast<std::size_t>(i);
        inFlight[s] -= engine.collectProcessors(i);
        if (pending[s])
            pending[s] = ! trySend(i);
    }
}
}
