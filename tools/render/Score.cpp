#include "Score.h"

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
    throw std::runtime_error("Unknown command: " + name.toStdString());
}

} // namespace

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
                throw std::runtime_error("Score event needs 'cmd' or 'param'");
            }
            s.events.push_back(te);
        }
    }

    std::stable_sort(s.events.begin(), s.events.end(),
                     [](const TimedEvent& a, const TimedEvent& b) { return a.sample < b.sample; });
    return s;
}

} // namespace tf::tools
