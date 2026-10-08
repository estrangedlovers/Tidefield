#include "Score.h"

#include <engine/mix/FxManager.h>

#include <algorithm>
#include <stdexcept>

namespace tf::tools {
namespace {
engine::Command parseCommand(const juce::String& name)
{
    using engine::Command;
    if (name == "fadeIn") return Command::FadeIn;
    if (name == "fadeOut") return Command::FadeOut;
    if (name == "panic") return Command::Panic;
    if (name == "resume") return Command::ResumeFromPanic;
    if (name == "resetFeedback") return Command::ResetFeedback;
    if (name == "releaseLive") return Command::ReleaseLiveLayer;
    if (name == "catch") return Command::Catch;
    if (name == "loopRecord") return Command::LoopRecord;
    if (name == "loopClear") return Command::LoopClear;
    throw std::runtime_error("Unknown command: " + name.toStdString());
}
}

Score Score::load(const juce::File& file, const engine::ParamRegistry& registry)
{
    if (! file.existsAsFile())
        throw std::runtime_error("Score not found: " + file.getFullPathName().toStdString());

    juce::var root;
    const auto result = juce::JSON::parse(file.loadFileAsString(), root);
    if (result.failed())
        throw std::runtime_error("Score JSON error: " + result.getErrorMessage().toStdString());

    Score s;
    s.durationSeconds = static_cast<double>(root.getProperty("duration", 30.0));
    s.sampleRate = static_cast<double>(root.getProperty("sampleRate", 48000.0));
    s.blockSize = static_cast<int>(root.getProperty("blockSize", 256));
    s.seed = static_cast<std::uint64_t>(static_cast<juce::int64>(root.getProperty("seed", 1)));
    s.randomBlockSizes = static_cast<bool>(root.getProperty("randomBlockSizes", false));

    if (s.durationSeconds <= 0.0 || s.sampleRate < 8000.0 || s.blockSize < 1)
        throw std::runtime_error("Score has invalid duration, sampleRate or blockSize");

    if (const auto* events = root.getProperty("events", juce::var()).getArray())
    {
        for (const auto& ev : *events)
        {
            TimedEvent te;
            const double t = static_cast<double>(ev.getProperty("t", 0.0));
            te.sample = static_cast<std::uint64_t>(std::max(0.0, t) * s.sampleRate);

            if (ev.hasProperty("cmd"))
            {
                te.event = engine::ControlEvent::makeCommand(parseCommand(ev["cmd"].toString()), engine::ControlSource::Score);
            }
            else if (ev.hasProperty("note"))
            {
                te.event = engine::ControlEvent::note(static_cast<int>(ev["note"]), static_cast<float>(static_cast<double>(ev.getProperty("velocity", 0.8))),
                                                      engine::ControlSource::Score);
            }
            else if (ev.hasProperty("noteOff"))
            {
                te.event = engine::ControlEvent::note(static_cast<int>(ev["noteOff"]), 0.0f, engine::ControlSource::Score);
            }
            else if (ev.hasProperty("param"))
            {
                const auto id = ev["param"].toString().toStdString();
                const auto index = registry.find(id);
                if (! index)
                    throw std::runtime_error("Unknown parameter in score: " + id);
                te.event = engine::ControlEvent::setParam(*index, static_cast<float>(static_cast<double>(ev["value"])),
                                                          engine::ControlSource::Score);
            }
            else
            {
                throw std::runtime_error("Score event needs 'cmd', 'param', 'note' or 'noteOff'");
            }
            s.events.push_back(te);
        }
    }

    auto paramIndex = [&](const juce::String& id) {
        const auto index = registry.find(id.toStdString());
        if (! index)
            throw std::runtime_error("Unknown parameter in score: " + id.toStdString());
        return *index;
    };

    if (const auto* scenes = root.getProperty("scenes", juce::var()).getArray())
    {
        for (const auto& sc : *scenes)
        {
            engine::Scene scene;
            scene.name = sc.getProperty("name", "").toString().toStdString();
            scene.position = { static_cast<float>(static_cast<double>(sc.getProperty("x", 0.5))),
                               static_cast<float>(static_cast<double>(sc.getProperty("y", 0.5))) };
            if (const auto* values = sc.getProperty("values", juce::var()).getDynamicObject())
                for (const auto& prop : values->getProperties())
                    scene.values[paramIndex(prop.name.toString())] = static_cast<float>(static_cast<double>(prop.value));
            s.scenes.push_back(std::move(scene));
        }
    }

    auto resolve = [&](const juce::String& path) {
        const auto cwd = juce::File::getCurrentWorkingDirectory().getChildFile(path);
        if (cwd.existsAsFile())
            return cwd;
        const auto rel = file.getParentDirectory().getChildFile(path);
        if (rel.existsAsFile())
            return rel;
        throw std::runtime_error("File not found: " + path.toStdString());
    };

    if (const auto* samples = root.getProperty("samples", juce::var()).getArray())
        for (const auto& sm : *samples)
            s.samples.push_back({ static_cast<int>(sm.getProperty("cloud", 0)), resolve(sm.getProperty("file", "").toString()) });

    if (const auto* fx = root.getProperty("fx", juce::var()).getArray())
        for (const auto& f : *fx)
        {
            const auto slotId = f.getProperty("slot", "").toString().toStdString();
            const int slot = engine::FxManager::findSlot(slotId);
            if (slot < 0)
                throw std::runtime_error("Unknown FX slot in score: " + slotId);
            s.fx.push_back({ slot, f.getProperty("type", "").toString().toStdString() });
        }

    s.defaultFx = static_cast<bool>(root.getProperty("defaultFx", true));
    if (root.hasProperty("input"))
        s.input = resolve(root["input"].toString());
    if (root.hasProperty("bloomSample"))
        s.bloomSample = resolve(root["bloomSample"].toString());
    if (root.hasProperty("session"))
        s.session = resolve(root["session"].toString());

    if (const auto* pins = root.getProperty("pins", juce::var()).getArray())
        for (const auto& id : *pins)
            s.pins.push_back(paramIndex(id.toString()));

    std::stable_sort(s.events.begin(), s.events.end(),
                     [](const TimedEvent& a, const TimedEvent& b) { return a.sample < b.sample; });
    return s;
}
}
