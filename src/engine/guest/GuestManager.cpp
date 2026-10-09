#include "GuestManager.h"

#include "InstrumentSlot.h"

#include "../Engine.h"

namespace tf::engine {
GuestManager::GuestManager(Engine& e) : engine(e) {}

bool GuestManager::knows(std::string_view typeId) const
{
    return externalInstruments != nullptr && externalInstruments->handlesInstrument(typeId) && externalInstruments->findInstrument(typeId) != nullptr;
}

const InstrumentInfo* GuestManager::getInfo() const
{
    if (type.empty() || externalInstruments == nullptr || ! externalInstruments->handlesInstrument(type))
        return nullptr;
    return externalInstruments->findInstrument(type);
}

std::string GuestManager::getName() const
{
    if (const auto* info = getInfo())
        return info->name;
    return name;
}

std::string GuestManager::getState() const
{
    if (type.empty())
        return {};
    if (! missing && externalInstruments != nullptr && externalInstruments->handlesInstrument(type))
        if (auto live = externalInstruments->captureInstrumentState(); ! live.empty())
            return live;
    return state;
}

void GuestManager::setType(std::string_view typeId, bool applyDefaults, std::string newState, std::string displayName)
{
    type = std::string(typeId);
    state = std::move(newState);
    name = std::move(displayName);
    missing = ! type.empty() && ! knows(type);
    if (const auto* info = getInfo())
        name = info->name;
    pending = ! trySend();

    if (applyDefaults)
    {
        if (const auto* info = getInfo())
            for (int c = 0; c < 6; ++c)
                engine.post(ControlEvent::snapParam(static_cast<ParamIndex>(idx(P::GuestP1) + c), info->defaults[static_cast<std::size_t>(c)]));
    }
}

bool GuestManager::trySend()
{
    if (inFlight >= static_cast<int>(InstrumentSlot::kQueueSize))
        return false;
    InstrumentPtr instrument;
    if (! type.empty() && ! missing && externalInstruments != nullptr)
    {
        instrument = externalInstruments->createInstrument(type, state, engine.getGuestSpec());
        missing = instrument == nullptr;
    }
    const bool isReal = instrument != nullptr;
    if (! engine.sendInstrument(std::move(instrument)))
        return false;
    if (isReal)
        ++inFlight;
    return true;
}

void GuestManager::tick()
{
    inFlight -= engine.collectInstruments();
    if (pending)
        pending = ! trySend();
}
}
