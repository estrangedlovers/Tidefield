#pragma once

#include "MidiTypes.h"

#include "../params/ParamRegistry.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tf::engine {
class Engine;

class MidiManager
{
public:
    explicit MidiManager(Engine& engine);

    const std::vector<MidiBinding>& getBindings() const noexcept { return bindings; }
    void setBindings(std::vector<MidiBinding> newBindings);
    void addBinding(const MidiBinding& b);
    void removeBinding(int index);
    void clearParam(ParamIndex param);
    void clearAll();
    std::vector<int> bindingsFor(ParamIndex param) const;

    int getNoteChannel() const noexcept { return noteChannel; }
    void setNoteChannel(int channel);
    bool getNotesToDrone() const noexcept { return notesToDrone; }
    void setNotesToDrone(bool enabled);

    void learnParam(ParamIndex param);
    void learnAction(MidiAction action);
    void cancelLearn();
    bool isLearning() const noexcept { return learning; }
    std::optional<ParamIndex> getLearnParam() const noexcept;

    bool handleMonitor(const RawMidi& message);
    std::function<void(const std::string&)> onLearned;

    void loadDefaultLayout();

    static const char* actionName(MidiAction action) noexcept;
    std::string describe(const MidiBinding& b) const;

    void tick();
    bool publish();

private:
    Engine& engine;
    const ParamRegistry& registry;
    std::vector<MidiBinding> bindings;
    int noteChannel = -1;
    bool notesToDrone = false;
    std::uint64_t version = 0;
    bool dirty = false;

    bool learning = false;
    ParamIndex learnTargetParam = 0;
    MidiAction learnTargetAction = MidiAction::None;
};
}
