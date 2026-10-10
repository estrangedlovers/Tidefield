#include "Performance.h"

#include "Recorder.h"

#include <engine/Engine.h>
#include <engine/capture/CatchManager.h>
#include <engine/midi/MidiManager.h>
#include <engine/mix/FxManager.h>
#include <engine/mod/ModRouteManager.h>
#include <engine/mod/SeasonManager.h>
#include <engine/perform/GestureManager.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/SceneManager.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace tf::io {
namespace {
using T = engine::ControlEvent::Type;
using engine::Command;

const std::pair<Command, const char*> kCommandNames[] = {
    { Command::FadeIn, "fadeIn" },       { Command::FadeOut, "fadeOut" },     { Command::Panic, "panic" },
    { Command::ResumeFromPanic, "resume" }, { Command::ReleaseLiveLayer, "release" }, { Command::Catch, "catch" },
    { Command::LoopRecord, "loopRecord" }, { Command::LoopClear, "loopClear" },
};

const char* commandName(Command c)
{
    for (const auto& [command, name] : kCommandNames)
        if (command == c)
            return name;
    return nullptr;
}
}

Performance::Lane Performance::laneOf(const engine::ControlEvent& e) noexcept
{
    if (e.type == T::Note)
        return { LaneKind::Notes, 0 };
    if (e.type == T::Command)
        return { LaneKind::Actions, 0 };
    return { LaneKind::Param, e.param };
}

std::vector<Performance::Lane> Performance::lanes() const
{
    std::set<Lane> seen;
    for (const auto& g : events)
        seen.insert(laneOf(g.event));
    return { seen.begin(), seen.end() };
}

void Performance::setMuted(const Lane& lane, bool mute)
{
    if (mute)
        muted.insert(lane);
    else
        muted.erase(lane);
}

std::uint64_t Performance::toSamples(double s) const noexcept
{
    return static_cast<std::uint64_t>(std::clamp(s, 0.0, seconds()) * sampleRate);
}

void Performance::erase(double from, double to, const Lane* onlyLane)
{
    const auto a = toSamples(std::min(from, to)), b = toSamples(std::max(from, to));
    events.erase(std::remove_if(events.begin(), events.end(),
                                [&](const engine::GestureEvent& g) {
                                    if (g.time < a || g.time > b)
                                        return false;
                                    if (onlyLane != nullptr && ! (laneOf(g.event) == *onlyLane))
                                        return false;
                                    return g.event.type != T::Note || g.event.value > 0.0f;
                                }),
                 events.end());
}

void Performance::smooth(const Lane& lane, double window)
{
    if (lane.kind != LaneKind::Param)
        return;
    const auto span = static_cast<std::uint64_t>(std::max(0.01, window) * sampleRate);
    std::vector<engine::GestureEvent> kept, sets;
    for (const auto& g : events)
        (laneOf(g.event) == lane && g.event.type == T::SetParam ? sets : kept).push_back(g);
    if (sets.size() < 3)
        return;
    std::vector<engine::GestureEvent> smoothed;
    std::size_t lo = 0;
    for (std::size_t i = 0; i < sets.size(); ++i)
    {
        if (! smoothed.empty() && sets[i].time < smoothed.back().time + span / 4 && i + 1 != sets.size())
            continue;
        while (sets[lo].time + span / 2 < sets[i].time)
            ++lo;
        double sum = 0.0;
        int n = 0;
        for (std::size_t k = lo; k < sets.size() && sets[k].time <= sets[i].time + span / 2; ++k)
        {
            sum += sets[k].event.value;
            ++n;
        }
        auto g = sets[i];
        g.event.value = static_cast<float>(sum / std::max(1, n));
        smoothed.push_back(g);
    }
    kept.insert(kept.end(), smoothed.begin(), smoothed.end());
    std::stable_sort(kept.begin(), kept.end(), [](const auto& x, const auto& y) { return x.time < y.time; });
    events = std::move(kept);
}

void Performance::trim(double from, double to)
{
    const auto a = toSamples(std::min(from, to)), b = toSamples(std::max(from, to));
    if (b <= a)
        return;
    std::map<engine::ParamIndex, engine::GestureEvent> before;
    std::vector<engine::GestureEvent> inside;
    for (const auto& g : events)
    {
        if (g.time < a)
        {
            if (g.event.type == T::SetParam)
                before[g.event.param] = g;
        }
        else if (g.time <= b)
            inside.push_back(g);
    }
    std::vector<engine::GestureEvent> out;
    for (auto& [param, g] : before)
    {
        g.time = 0;
        out.push_back(g);
    }
    for (auto g : inside)
    {
        g.time -= a;
        out.push_back(g);
    }
    events = std::move(out);
    length = b - a;
}

std::vector<engine::GestureEvent> Performance::playable() const
{
    std::vector<engine::GestureEvent> out;
    out.reserve(events.size());
    for (const auto& g : events)
        if (! isMuted(laneOf(g.event)) || (g.event.type == T::Note && g.event.value <= 0.0f))
            out.push_back(g);
    return out;
}

juce::var performanceToJson(const Performance& p, const engine::ParamRegistry& reg)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("seconds", p.seconds());
    o->setProperty("startedOpen", p.startedOpen);
    juce::Array<juce::var> rows;
    for (const auto& g : p.events)
    {
        const auto& e = g.event;
        juce::Array<juce::var> row;
        row.add(std::round(static_cast<double>(g.time) / p.sampleRate * 100000.0) / 100000.0);
        if ((e.type == T::SetParam || e.type == T::ReleaseParam) && e.param < reg.size())
        {
            row.add(e.type == T::SetParam ? "set" : "release");
            row.add(juce::String(reg.spec(e.param).id));
            row.add(e.value);
        }
        else if (e.type == T::Note)
        {
            row.add("note");
            row.add(static_cast<int>(e.param));
            row.add(e.value);
        }
        else if (e.type == T::Command && commandName(e.command) != nullptr)
        {
            row.add(commandName(e.command));
            row.add(0);
            row.add(0);
        }
        else
            continue;
        rows.add(row);
    }
    o->setProperty("events", rows);
    juce::Array<juce::var> mutedLanes;
    for (const auto& lane : p.muted)
        mutedLanes.add(lane.kind == Performance::LaneKind::Notes     ? juce::String("notes")
                       : lane.kind == Performance::LaneKind::Actions ? juce::String("actions")
                       : lane.param < reg.size()                     ? juce::String(reg.spec(lane.param).id)
                                                                     : juce::String());
    o->setProperty("muted", mutedLanes);
    return juce::var(o);
}

std::optional<Performance> performanceFromSession(const SessionData& session, const engine::ParamRegistry& reg)
{
    const auto& json = session.performance;
    if (! json.isObject())
        return std::nullopt;
    Performance p;
    p.start = session;
    p.start.performance = juce::var();
    p.sampleRate = 48000.0;
    auto seconds = [](const juce::var& v) {
        const double s = v;
        return std::isfinite(s) ? std::clamp(s, 0.0, 86400.0) : 0.0;
    };
    p.length = static_cast<std::uint64_t>(seconds(json.getProperty("seconds", 0.0)) * p.sampleRate);
    p.startedOpen = static_cast<bool>(json.getProperty("startedOpen", true));
    if (const auto* rows = json.getProperty("events", {}).getArray())
        for (const auto& row : *rows)
        {
            const auto* r = row.getArray();
            if (r == nullptr || r->size() < 4 || p.events.size() >= 2000000)
                continue;
            engine::GestureEvent g;
            g.time = static_cast<std::uint64_t>(seconds((*r)[0]) * p.sampleRate);
            const auto kind = (*r)[1].toString();
            const double raw = (*r)[3];
            const float value = std::isfinite(raw) ? static_cast<float>(raw) : 0.0f;
            if (kind == "set" || kind == "release")
            {
                const auto param = reg.find((*r)[2].toString().toStdString());
                if (! param.has_value())
                    continue;
                g.event = kind == "set" ? engine::ControlEvent::setParam(*param, value) : engine::ControlEvent::releaseParam(*param);
            }
            else if (kind == "note")
                g.event = engine::ControlEvent::note(std::clamp(static_cast<int>((*r)[2]), 0, 127), std::clamp(value, 0.0f, 1.0f));
            else
            {
                const auto it = std::find_if(std::begin(kCommandNames), std::end(kCommandNames), [&](const auto& c) { return kind == c.second; });
                if (it == std::end(kCommandNames))
                    continue;
                g.event = engine::ControlEvent::makeCommand(it->first);
            }
            if (g.time <= p.length)
                p.events.push_back(g);
        }
    std::stable_sort(p.events.begin(), p.events.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    if (const auto* mutedLanes = json.getProperty("muted", {}).getArray())
        for (const auto& m : *mutedLanes)
        {
            const auto id = m.toString();
            if (id == "notes")
                p.muted.insert({ Performance::LaneKind::Notes, 0 });
            else if (id == "actions")
                p.muted.insert({ Performance::LaneKind::Actions, 0 });
            else if (const auto param = reg.find(id.toStdString()))
                p.muted.insert({ Performance::LaneKind::Param, *param });
        }
    return p;
}

RenderResult renderPerformance(const Performance& performance, const RenderOptions& options)
{
    RenderResult result;
    constexpr int kBlock = 512;
    const double rate = options.sampleRate;
    engine::Engine engine;
    engine.prepare(rate, kBlock);
    engine::SceneManager scenes(engine);
    engine::FxManager fx(engine);
    engine::MidiManager midi(engine);
    engine::SeasonManager seasons(engine);
    engine::PathManager path(engine);
    engine::GestureManager gestures(engine);
    engine::ModRouteManager mod(engine);
    engine::CatchManager catcher(engine);
    result.warnings = applySession(performance.start, engine, scenes, fx, true, &midi, &seasons, &path, &gestures, &mod);
    if (const auto& guest = performance.start.guest; ! guest.type.empty())
    {
        std::string error;
        auto instrument = options.makeGuest ? options.makeGuest(guest, engine.getGuestSpec(), error) : nullptr;
        if (instrument == nullptr || ! engine.sendInstrument(std::move(instrument)))
            result.warnings.push_back("The render leaves out the Guest instrument" + (guest.name.empty() ? std::string() : " '" + guest.name + "'")
                                      + (error.empty() ? std::string(": it cannot be opened here") : ": " + error));
    }

    std::vector<float> l(kBlock), r(kBlock);
    float* outs[2] = { l.data(), r.data() };
    auto tick = [&] {
        scenes.tick();
        fx.tick();
        midi.tick();
        seasons.tick();
        path.tick();
        gestures.tick();
        mod.tick();
        engine.collectGarbage();
        engine::TelemetryFrame frame;
        while (engine.popTelemetry(frame)) {}
        engine::EngineNotice notice;
        while (engine.popNotice(notice))
            catcher.handle(notice);
    };

    const float fadeSeconds = engine.getRegistry().spec(engine::P::MasterFadeSecs).clamp(
        performance.start.params.count("master.fadeSeconds") != 0 ? performance.start.params.at("master.fadeSeconds") : 8.0f);
    if (performance.startedOpen)
    {
        engine.post(engine::ControlEvent::snapParam(engine::idx(engine::P::MasterFadeSecs), 0.5f));
        engine.command(engine::Command::FadeIn);
    }
    for (int b = 0; b < static_cast<int>(1.0 * rate / kBlock); ++b)
    {
        engine.process(nullptr, 0, outs, 2, kBlock);
        tick();
    }
    engine.post(engine::ControlEvent::snapParam(engine::idx(engine::P::MasterFadeSecs), fadeSeconds));

    const bool loop = options.loopCrossfadeSeconds > 0.0;
    const bool stems = options.stems && ! loop;
    auto& tap = engine.getRecordTap();
    if (! tap.begin(stems))
    {
        result.error = "The render could not start its recorder.";
        return result;
    }
    const int stride = tap.getStride();

    if (! options.folder.createDirectory())
    {
        result.error = "Could not create " + options.folder.getFullPathName();
        return result;
    }
    juce::WavAudioFormat wav;
    const auto writerOptions = juce::AudioFormatWriterOptions {}
                                   .withSampleRate(rate)
                                   .withNumChannels(2)
                                   .withBitsPerSample(32)
                                   .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto openWriter = [&](const juce::File& file) -> std::unique_ptr<juce::AudioFormatWriter> {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        if (! static_cast<juce::FileOutputStream*>(stream.get())->openedOk())
            return nullptr;
        return wav.createWriterFor(stream, writerOptions);
    };
    const auto masterFile = options.folder.getChildFile(loop ? "loop-unfolded.wav" : "master.wav");
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
    writers.push_back(openWriter(masterFile));
    if (stems)
    {
        const auto stemDir = options.folder.getChildFile("stems");
        stemDir.createDirectory();
        for (const auto& name : Recorder::stemNames())
            writers.push_back(openWriter(stemDir.getChildFile(name + ".wav")));
    }
    if (std::any_of(writers.begin(), writers.end(), [](const auto& w) { return w == nullptr; }))
    {
        result.error = "Could not write to " + options.folder.getFullPathName();
        return result;
    }

    const auto events = performance.playable();
    const double scale = rate / performance.sampleRate;
    const auto bodyLength = static_cast<std::uint64_t>(static_cast<double>(performance.length) * scale);
    const auto fold = static_cast<std::uint64_t>(std::max(0.0, options.loopCrossfadeSeconds) * rate);
    const std::uint64_t total = bodyLength + (loop ? fold : 0);
    std::vector<float> interleaved(static_cast<std::size_t>(kBlock * stride));
    std::vector<std::vector<float>> channels(static_cast<std::size_t>(stride), std::vector<float>(kBlock));
    std::size_t next = 0;
    std::uint64_t pos = 0;
    auto drainTap = [&] {
        int frames = 0;
        while ((frames = tap.read(interleaved.data(), kBlock)) > 0)
        {
            for (int c = 0; c < stride; ++c)
                for (int i = 0; i < frames; ++i)
                    channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
                        interleaved[static_cast<std::size_t>(i * stride + c)];
            for (std::size_t w = 0; w < writers.size(); ++w)
            {
                const float* pair[2] = { channels[w * 2].data(), channels[w * 2 + 1].data() };
                writers[w]->writeFromFloatArrays(pair, 2, frames);
            }
        }
    };
    while (pos < total)
    {
        if (options.cancel != nullptr && options.cancel->load())
        {
            result.error = "Render cancelled.";
            return result;
        }
        while (next < events.size() && static_cast<std::uint64_t>(static_cast<double>(events[next].time) * scale) <= pos)
        {
            auto e = events[next++].event;
            e.source = engine::ControlSource::Score;
            engine.post(e);
        }
        std::uint64_t until = total;
        if (next < events.size())
            until = std::min(until, std::max(pos + 1, static_cast<std::uint64_t>(static_cast<double>(events[next].time) * scale)));
        const int n = static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(kBlock), until - pos));
        engine.process(nullptr, 0, outs, 2, n);
        pos += static_cast<std::uint64_t>(n);
        tick();
        drainTap();
        if (options.onProgress && (pos % (static_cast<std::uint64_t>(kBlock) * 64) < static_cast<std::uint64_t>(n)))
            options.onProgress(static_cast<float>(static_cast<double>(pos) / static_cast<double>(total)));
    }
    drainTap();
    writers.clear();

    if (loop)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(masterFile));
        if (reader == nullptr)
        {
            result.error = "Could not read the rendered loop back.";
            return result;
        }
        const auto loopFile = options.folder.getChildFile("loop.wav");
        auto writer = openWriter(loopFile);
        if (writer == nullptr)
        {
            result.error = "Could not write " + loopFile.getFullPathName();
            return result;
        }
        const int fadeLen = static_cast<int>(std::min<std::uint64_t>(fold, bodyLength));
        juce::AudioBuffer<float> head(2, fadeLen), tail(2, fadeLen);
        reader->read(&head, 0, fadeLen, 0, true, true);
        reader->read(&tail, 0, fadeLen, static_cast<juce::int64>(bodyLength), true, true);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < fadeLen; ++i)
            {
                const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(fadeLen);
                const float in = std::sin(0.5f * juce::MathConstants<float>::pi * t);
                const float out = std::cos(0.5f * juce::MathConstants<float>::pi * t);
                head.setSample(c, i, head.getSample(c, i) * in + tail.getSample(c, i) * out);
            }
        writer->writeFromAudioSampleBuffer(head, 0, fadeLen);
        juce::AudioBuffer<float> chunk(2, 65536);
        for (auto at = static_cast<juce::int64>(fadeLen); at < static_cast<juce::int64>(bodyLength);)
        {
            const int n = static_cast<int>(std::min<juce::int64>(65536, static_cast<juce::int64>(bodyLength) - at));
            reader->read(&chunk, 0, n, at, true, true);
            writer->writeFromAudioSampleBuffer(chunk, 0, n);
            at += n;
        }
        writer.reset();
        reader.reset();
        masterFile.deleteFile();
        result.master = loopFile;
    }
    else
        result.master = masterFile;
    if (options.onProgress)
        options.onProgress(1.0f);
    result.ok = true;
    return result;
}
}
