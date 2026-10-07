#include "MidiManager.h"

#include "../Engine.h"

#include <algorithm>
#include <cctype>

namespace tf::engine {

MidiManager::MidiManager(Engine& e) : engine(e), registry(e.getRegistry()) {}

const char* MidiManager::actionName(MidiAction action) noexcept
{
    switch (action)
    {
        case MidiAction::Catch: return "Catch";
        case MidiAction::FadeToggle: return "Fade in/out";
        case MidiAction::Panic: return "Panic";
        case MidiAction::ReleaseLive: return "Release live layer";
        case MidiAction::CaptureScene: return "Capture scene";
        case MidiAction::RecordToggle: return "Record";
        case MidiAction::LoopRecord: return "Loop record / overdub";
        case MidiAction::LoopClear: return "Loop clear";
        case MidiAction::FreezeToggle: return "Freeze all";
        case MidiAction::InputFreezeToggle: return "Freeze input";
        case MidiAction::None: break;
    }
    return "";
}

std::string MidiManager::describe(const MidiBinding& b) const
{
    std::string control = (b.source == MidiBinding::Source::Cc ? "CC " : "Note ") + std::to_string(b.cc)
                          + (b.channel < 0 ? " (any ch)" : " (ch " + std::to_string(b.channel + 1) + ")");
    if (b.action != MidiAction::None)
        return control + " -> " + actionName(b.action);
    // Display name, with its group when the name alone is ambiguous ("Level").
    const auto& spec = registry.spec(b.param);
    std::string target = spec.name;
    const auto dot = spec.id.find('.');
    if (dot != std::string::npos)
    {
        std::string group = spec.id.substr(0, dot);
        std::string lowerName = spec.name;
        for (auto& ch : lowerName)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lowerName.find(group) == std::string::npos)
        {
            group[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(group[0])));
            target = group + " " + spec.name;
        }
    }
    return control + " -> " + target;
}

bool MidiManager::publish()
{
    auto map = std::make_unique<MidiMap>();
    map->bindings = bindings;
    if (map->bindings.size() > static_cast<std::size_t>(kMaxMidiBindings))
        map->bindings.resize(static_cast<std::size_t>(kMaxMidiBindings));
    map->noteChannel = noteChannel;
    map->notesToDrone = notesToDrone;
    map->version = ++version;
    map->rebuildLookup();
    dirty = ! engine.publishMidiMap(std::move(map));
    return ! dirty;
}

void MidiManager::tick()
{
    if (dirty)
        publish();
}

void MidiManager::setBindings(std::vector<MidiBinding> newBindings)
{
    bindings = std::move(newBindings);
    publish();
}

void MidiManager::addBinding(const MidiBinding& b)
{
    // One control drives one target when learned: drop older bindings of the same
    // control so moving a knob never surprises you with a forgotten mapping.
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                  [&](const MidiBinding& o) { return o.source == b.source && o.cc == b.cc && o.channel == b.channel; }),
                   bindings.end());
    if (bindings.size() < static_cast<std::size_t>(kMaxMidiBindings))
        bindings.push_back(b);
    publish();
}

void MidiManager::removeBinding(int index)
{
    if (index < 0 || index >= static_cast<int>(bindings.size()))
        return;
    bindings.erase(bindings.begin() + index);
    publish();
}

void MidiManager::clearParam(ParamIndex param)
{
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                  [&](const MidiBinding& b) { return b.action == MidiAction::None && b.param == param; }),
                   bindings.end());
    publish();
}

void MidiManager::clearAll()
{
    bindings.clear();
    publish();
}

std::vector<int> MidiManager::bindingsFor(ParamIndex param) const
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(bindings.size()); ++i)
        if (bindings[static_cast<std::size_t>(i)].action == MidiAction::None && bindings[static_cast<std::size_t>(i)].param == param)
            out.push_back(i);
    return out;
}

void MidiManager::setNoteChannel(int channel)
{
    noteChannel = std::clamp(channel, -1, 15);
    publish();
}

void MidiManager::setNotesToDrone(bool enabled)
{
    notesToDrone = enabled;
    publish();
}

void MidiManager::learnParam(ParamIndex param)
{
    learning = true;
    learnTargetParam = param;
    learnTargetAction = MidiAction::None;
}

void MidiManager::learnAction(MidiAction action)
{
    learning = true;
    learnTargetAction = action;
}

void MidiManager::cancelLearn() { learning = false; }

std::optional<ParamIndex> MidiManager::getLearnParam() const noexcept
{
    if (learning && learnTargetAction == MidiAction::None)
        return learnTargetParam;
    return std::nullopt;
}

bool MidiManager::handleMonitor(const RawMidi& m)
{
    if (! learning)
        return false;
    MidiBinding b;
    if (m.isCc())
    {
        if (m.data1 == 64)
            return false; // the sustain pedal is reserved for Bloom
        b.source = MidiBinding::Source::Cc;
    }
    else if (m.isNoteOn() && learnTargetAction != MidiAction::None)
    {
        b.source = MidiBinding::Source::Note; // pads can trigger actions
    }
    else
    {
        return false;
    }
    b.channel = m.channel();
    b.cc = m.data1;
    b.action = learnTargetAction;
    b.param = learnTargetParam;
    learning = false;
    addBinding(b);
    if (onLearned)
        onLearned(describe(b));
    return true;
}

void MidiManager::loadDefaultLayout()
{
    const P targets[8] = { P::TerrainX, P::TerrainY, P::TideRate, P::TerrainWander,
                           P::HarmonyGravity, P::BusALevel, P::Cloud1Density, P::MasterLevel };
    std::vector<MidiBinding> layout;
    for (int i = 0; i < 8; ++i)
    {
        MidiBinding b;
        b.channel = -1;
        b.cc = 21 + i;
        b.param = idx(targets[i]);
        layout.push_back(b);
    }
    // Keep master level out of the danger zone at the top of the knob.
    layout[7].high = registry.spec(P::MasterLevel).toNormalised(0.0f);
    setBindings(std::move(layout));
}

} // namespace tf::engine
