#pragma once

#include "Instrument.h"

#include <string>
#include <string_view>

namespace tf::engine {
class Engine;

class GuestManager
{
public:
    explicit GuestManager(Engine& engine);

    void setExternal(ExternalInstruments* external) noexcept { externalInstruments = external; }
    ExternalInstruments* getExternal() const noexcept { return externalInstruments; }

    void setType(std::string_view typeId, bool applyDefaults = true, std::string state = {}, std::string name = {});
    void clear() { setType({}, false); }

    const std::string& getType() const noexcept { return type; }
    std::string getState() const;
    std::string getName() const;
    const InstrumentInfo* getInfo() const;
    bool isEmpty() const noexcept { return type.empty(); }
    bool isMissing() const noexcept { return missing; }
    bool knows(std::string_view typeId) const;

    void tick();

private:
    bool trySend();

    Engine& engine;
    std::string type, state, name;
    int inFlight = 0;
    bool pending = false;
    bool missing = false;
    ExternalInstruments* externalInstruments = nullptr;
};
}
