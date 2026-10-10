#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
#include <io/AudioFileIO.h>

#include <dsp/core/Denormal.h>
#include <dsp/core/Random.h>
#include <dsp/fx/ProcessorFactory.h>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {
using namespace tf;

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

struct Setting
{
    double t;
    std::string id;
    float value;
};

struct Case
{
    std::string name;
    std::vector<Setting> settings;
    std::vector<std::pair<int, std::string>> clouds;
    std::vector<std::pair<std::string, std::string>> fx;
    std::string bloom;
    std::string input;
    std::vector<std::pair<double, engine::Command>> commands;
    std::vector<std::pair<double, int>> notes;
    bool defaultFx = false;
    bool muteAll = true;
};

struct Options
{
    double seconds = 20.0;
    int repeats = 3;
    std::string filter;
};

std::shared_ptr<const dsp::SampleBuffer> load(const std::string& path)
{
    juce::String error;
    auto buffer = io::loadSample(juce::File::getCurrentWorkingDirectory().getChildFile(path), error);
    if (buffer == nullptr)
        std::fprintf(stderr, "could not load %s: %s\n", path.c_str(), error.toRawUTF8());
    return std::shared_ptr<const dsp::SampleBuffer>(std::move(buffer));
}

double runCase(const Case& c, const Options& o)
{
    engine::Engine::Config config;
    config.seed = 7;
    engine::Engine eng(config);
    eng.prepare(kSampleRate, kBlock);
    engine::FxManager fx(eng);
    if (c.defaultFx)
        fx.loadDefaultLayout();
    for (const auto& [slot, type] : c.fx)
        fx.setType(engine::FxManager::findSlot(slot), type);
    for (const auto& [cloud, path] : c.clouds)
        if (auto b = load(path))
            eng.loadCloudSample(cloud, b);
    if (! c.bloom.empty())
        if (auto b = load(c.bloom))
            eng.loadBloomSample(b);
    std::shared_ptr<const dsp::SampleBuffer> input;
    if (! c.input.empty())
        input = load(c.input);

    const auto& registry = eng.getRegistry();
    std::vector<Setting> settings = c.settings;
    if (c.muteAll)
        for (const auto& s : engine::kStrips)
            settings.insert(settings.begin(), { 0.0, std::string(s.id) + ".level", -60.0f });
    std::stable_sort(settings.begin(), settings.end(), [](const Setting& a, const Setting& b) { return a.t < b.t; });
    for (const auto& s : settings)
        if (! registry.find(s.id))
            std::fprintf(stderr, "unknown parameter %s\n", s.id.c_str());
    eng.post(engine::ControlEvent::setParam(*registry.find("master.fadeSeconds"), 0.1f));
    eng.command(engine::Command::FadeIn);

    const auto total = static_cast<std::uint64_t>(o.seconds * kSampleRate);
    std::vector<float> outL(kBlock), outR(kBlock), inBuf(kBlock);
    float* outs[2] = { outL.data(), outR.data() };
    std::size_t nextSetting = 0, nextCommand = 0, nextNote = 0, inPos = 0;
    double elapsed = 0.0;
    engine::TelemetryFrame frame;
    for (std::uint64_t pos = 0; pos < total; pos += kBlock)
    {
        const double t = static_cast<double>(pos) / kSampleRate;
        while (nextSetting < settings.size() && settings[nextSetting].t <= t)
        {
            const auto& s = settings[nextSetting++];
            if (auto i = registry.find(s.id))
                eng.post(engine::ControlEvent::setParam(*i, s.value));
        }
        while (nextCommand < c.commands.size() && c.commands[nextCommand].first <= t)
            eng.command(c.commands[nextCommand++].second);
        while (nextNote < c.notes.size() && c.notes[nextNote].first <= t)
            eng.noteOn(c.notes[nextNote++].second, 0.8f);

        const auto started = std::chrono::steady_clock::now();
        if (input != nullptr)
        {
            for (int i = 0; i < kBlock; ++i, ++inPos)
                inBuf[static_cast<std::size_t>(i)] = input->left[inPos % input->size()];
            const float* ins[2] = { inBuf.data(), inBuf.data() };
            eng.process(ins, 2, outs, 2, kBlock);
        }
        else
        {
            eng.process(nullptr, 0, outs, 2, kBlock);
        }
        elapsed += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        fx.tick();
        eng.collectGarbage();
        while (eng.popTelemetry(frame)) {}
        engine::EngineNotice notice;
        while (eng.popNotice(notice)) {}
    }
    return elapsed;
}

double runProcessor(const dsp::ProcessorFactory::Entry& e, const Options& o)
{
    const dsp::ScopedFlushDenormals noDenormals;
    auto p = e.create();
    p->prepare({ kSampleRate, kBlock });
    std::array<float, 6> controls {};
    for (std::size_t i = 0; i < 6; ++i)
        controls[i] = e.info->controls[i].defaultValue;
    dsp::Random rng(3);
    const auto total = static_cast<std::uint64_t>(o.seconds * kSampleRate);
    std::vector<float> l(kBlock), r(kBlock);
    float lp = 0.0f;
    double elapsed = 0.0;
    for (std::uint64_t pos = 0; pos < total; pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            lp += 0.05f * (rng.nextBipolar() - lp);
            const bool on = (pos / 48000) % 4 != 3;
            l[static_cast<std::size_t>(i)] = on ? lp : 0.0f;
            r[static_cast<std::size_t>(i)] = on ? 0.7f * lp + 0.1f * rng.nextBipolar() : 0.0f;
        }
        const auto started = std::chrono::steady_clock::now();
        p->setControls(controls, {});
        p->process(l.data(), r.data(), kBlock);
        elapsed += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    }
    return elapsed;
}

std::vector<Case> makeCases()
{
    using C = engine::Command;
    std::vector<Case> cases;
    cases.push_back({ .name = "engine.silent" });
    cases.push_back({ .name = "engine.default", .clouds = { { 0, "resources/samples/chord.wav" } }, .defaultFx = true, .muteAll = false });
    cases.push_back({ .name = "drone.d3", .settings = { { 0, "drone.level", 0 } } });
    cases.push_back({ .name = "drone.d6", .settings = { { 0, "drone.level", 0 }, { 0, "drone.density", 6 } } });
    cases.push_back({ .name = "cloud.d12", .settings = { { 0, "cloud1.level", -3 } }, .clouds = { { 0, "resources/samples/chord.wav" } } });
    cases.push_back({ .name = "cloud.d200",
                      .settings = { { 0, "cloud1.level", -3 }, { 0, "cloud1.density", 200 }, { 0, "cloud1.grainMs", 1200 } },
                      .clouds = { { 0, "resources/samples/chord.wav" } } });
    cases.push_back({ .name = "resonator.m16", .settings = { { 0, "res.level", -4 } } });
    cases.push_back({ .name = "resonator.m24.rain", .settings = { { 0, "res.level", -4 }, { 0, "res.modes", 24 }, { 0, "res.rain", 1 } } });
    {
        Case c { .name = "bloom.8notes", .settings = { { 0, "bloom.level", 0 } }, .bloom = "resources/samples/glass.wav" };
        for (int k = 0; k < 8; ++k)
            c.notes.push_back({ 0.5 + 2.0 * (k % 4), 60 + 3 * k });
        cases.push_back(c);
    }
    cases.push_back({ .name = "input.armed", .settings = { { 0, "input.level", 0 }, { 0, "input.armed", 1 } }, .input = "resources/samples/pluck.wav" });
    cases.push_back({ .name = "input.freeze",
                      .settings = { { 0, "input.level", 0 }, { 0, "input.armed", 1 }, { 1, "input.freeze", 1 } },
                      .input = "resources/samples/pluck.wav" });
    cases.push_back({ .name = "loop.playing",
                      .settings = { { 0, "loop.level", 0 }, { 0, "loop.source", 0 } },
                      .input = "resources/samples/pluck.wav",
                      .commands = { { 0.5, C::LoopRecord }, { 4.5, C::LoopRecord } } });
    cases.push_back({ .name = "weather.all",
                      .settings = { { 0, "weather.level", -4 }, { 0, "weather.wind", 0.8f }, { 0, "weather.rain", 0.8f }, { 0, "weather.surf", 0.8f } } });
    cases.push_back({ .name = "freeze.drone",
                      .settings = { { 0, "drone.level", 0 }, { 0, "freeze.level", 0 }, { 1, "freeze.on", 1 } } });
    cases.push_back({ .name = "medium.cassette", .settings = { { 0, "drone.level", 0 }, { 0, "medium.type", 1 } } });
    cases.push_back({ .name = "automaster.drone", .settings = { { 0, "drone.level", 0 }, { 0, "master.auto", 1 } } });
    {
        Case c { .name = "full.stress",
                 .settings = { { 0, "master.auto", 1 },        { 0, "drone.density", 6 },     { 0, "cloud1.density", 200 },
                               { 0, "cloud2.density", 200 },   { 0, "cloud3.density", 200 },  { 0, "cloud4.density", 200 },
                               { 0, "cloud1.grainMs", 1200 },  { 0, "cloud2.grainMs", 1200 }, { 0, "cloud3.grainMs", 1200 },
                               { 0, "cloud4.grainMs", 1200 },  { 0, "res.modes", 24 },        { 0, "res.rain", 1 },
                               { 0, "res.exciteClouds", 0.5f }, { 0, "weather.wind", 0.8f },  { 0, "weather.rain", 0.8f },
                               { 0, "weather.surf", 0.8f },    { 0, "input.armed", 1 },       { 0, "loops.on", 1 },
                               { 0, "loops.count", 8 },        { 0, "loops.rate", 4 },        { 0, "loop.source", 1 },
                               { 0, "medium.type", 1 },        { 1, "freeze.on", 1 },         { 1, "input.freeze", 1 } },
                 .clouds = { { 0, "resources/samples/chord.wav" }, { 1, "resources/samples/glass.wav" }, { 2, "resources/samples/breath.wav" },
                             { 3, "resources/samples/pluck.wav" } },
                 .fx = { { "master.fx1", "tf.blur" }, { "master.fx2", "tf.strings" }, { "busB.fx1", "tf.wornEcho" },
                         { "drone.fx1", "tf.ensemble" }, { "cloud1.fx1", "tf.medium" } },
                 .bloom = "resources/samples/glass.wav",
                 .input = "resources/samples/pluck.wav",
                 .commands = { { 0.5, C::LoopRecord }, { 4.5, C::LoopRecord } },
                 .defaultFx = true,
                 .muteAll = false };
        for (int k = 0; k < 8; ++k)
            c.notes.push_back({ 1.0, 62 + 3 * k });
        cases.push_back(c);
    }
    return cases;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
}

int main(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--seconds" && i + 1 < argc)
            o.seconds = std::atof(argv[++i]);
        else if (a == "--repeats" && i + 1 < argc)
            o.repeats = std::max(1, std::atoi(argv[++i]));
        else if (a == "--filter" && i + 1 < argc)
            o.filter = argv[++i];
        else
        {
            std::fprintf(stderr, "usage: tidefield_bench [--seconds S] [--repeats N] [--filter text]\n");
            return 2;
        }
    }

    std::printf("%-24s %10s %10s %12s\n", "case", "ms/s", "% core", "x realtime");
    auto report = [&](const std::string& name, std::vector<double> runs) {
        const double m = median(std::move(runs));
        std::printf("%-24s %10.2f %10.2f %12.1f\n", name.c_str(), 1000.0 * m / o.seconds, 100.0 * m / o.seconds, o.seconds / m);
        std::fflush(stdout);
    };

    for (const auto& c : makeCases())
    {
        if (! o.filter.empty() && c.name.find(o.filter) == std::string::npos)
            continue;
        std::vector<double> runs;
        for (int k = 0; k < o.repeats; ++k)
            runs.push_back(runCase(c, o));
        report(c.name, runs);
    }
    for (const auto& e : dsp::ProcessorFactory::instance().entries())
    {
        const std::string name = std::string("fx.") + e.info->typeId;
        if (! o.filter.empty() && name.find(o.filter) == std::string::npos)
            continue;
        std::vector<double> runs;
        for (int k = 0; k < o.repeats; ++k)
            runs.push_back(runProcessor(e, o));
        report(name, runs);
    }
    return 0;
}
