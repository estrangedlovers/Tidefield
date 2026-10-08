#include <app/PluginHost.h>
#include <engine/Engine.h>
#include <engine/mix/FxManager.h>

#include <cmath>
#include <iostream>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
    std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
    failures += ok ? 0 : 1;
}

float renderRms(tf::engine::Engine& engine, tf::engine::FxManager& fx, double seconds)
{
    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    double sum = 0.0;
    int count = 0;
    for (int b = 0; b < static_cast<int>(seconds * 48000.0 / 256.0); ++b)
    {
        engine.process(nullptr, 0, outs, 2, 256);
        fx.tick();
        tf::engine::TelemetryFrame f;
        while (engine.popTelemetry(f)) {}
        if (b * 256 > 24000)
            for (float x : l)
            {
                sum += static_cast<double>(x) * x;
                ++count;
            }
    }
    return count > 0 ? static_cast<float>(std::sqrt(sum / count)) : 0.0f;
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: tidefield_hostcheck <folder containing Tidefield Test Gain.vst3>\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    using namespace tf;

    const auto dir = juce::File::createTempFile("hostcheck");
    dir.createDirectory();
    juce::PropertiesFile::Options options;
    options.applicationName = "hostcheck";
    options.filenameSuffix = ".settings";
    juce::PropertiesFile settings(dir.getChildFile("hostcheck.settings"), options);

    {
        app::PluginHost host(settings, dir.getChildFile("crash.txt"));
        const int found = host.scanNow(juce::FileSearchPath(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]).getFullPathName()));
        check(found >= 1, "the test plugin is found by a scan");
        const auto effects = host.effects();
        const auto it = std::find_if(effects.begin(), effects.end(), [](const auto& d) { return d.name == "Tidefield Test Gain"; });
        check(it != effects.end(), "it is listed as an effect");
        if (it == effects.end())
            if (auto xml = settings.getXmlValue("knownPlugins"))
                std::cout << xml->toString().toStdString() << std::endl;
        if (it == effects.end())
            return 1;
        const auto type = app::PluginHost::typeIdFor(*it);

        engine::Engine engine;
        engine.prepare(48000.0, 256);
        engine::FxManager fx(engine);
        fx.setExternal(&host);
        engine.setParam(engine::P::MasterFadeSecs, 0.5f);
        engine.setParam(engine::P::BusALevel, -60.0f);
        engine.setParam(engine::P::BusBLevel, -60.0f);
        engine.setParam(engine::P::ResLevel, -60.0f);
        engine.command(engine::Command::FadeIn);
        const int slot = 0;

        const float dry = renderRms(engine, fx, 1.5);
        check(dry > 1.0e-3f, "the drone is audible without the plugin");

        fx.setType(slot, type);
        check(fx.isExternal(slot) && fx.getInfo(slot) != nullptr, "the slot holds the plugin");
        check(juce::String(fx.getInfo(slot)->controls[0].name) == "Gain", "its first control is named after the plugin's parameter");
        check(host.parameterText(slot, 0, 1.0f).contains("12"), "its value text comes from the plugin");

        const auto first = engine::idx(engine::kFxSlots[static_cast<std::size_t>(slot)].firstParam);
        engine.post(engine::ControlEvent::setParam(first, 0.0f));
        const float quiet = renderRms(engine, fx, 1.5);
        check(quiet < dry * 0.05f, "turning the control down turns the plugin's gain down");

        engine.post(engine::ControlEvent::setParam(first, 0.5f));
        renderRms(engine, fx, 1.0);
        const auto state = fx.getState(slot);
        check(! state.empty(), "the plugin's state can be captured");

        engine::Engine engine2;
        engine2.prepare(48000.0, 256);
        engine::FxManager fx2(engine2);
        fx2.setExternal(&host);
        fx2.setType(1, type, false, state);
        bool restored = false;
        for (int k = 0; k < 10 && ! restored; ++k)
        {
            renderRms(engine2, fx2, 0.05);
            restored = host.hasInstance(1);
        }
        check(restored, "a new instance opens from the saved state");

        fx.setType(slot, "");
        renderRms(engine, fx, 0.5);
        check(! host.hasInstance(slot), "clearing the slot releases the plugin");
        fx.setExternal(nullptr);
        fx2.setExternal(nullptr);
    }
    dir.deleteRecursively();
    std::cout << (failures == 0 ? "Hosting check passed" : "Hosting check FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
