#include <app/PluginHost.h>
#include <engine/Engine.h>
#include <engine/guest/GuestManager.h>
#include <engine/mix/FxManager.h>
#include <io/Performance.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
    std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
    failures += ok ? 0 : 1;
}

float renderRms(tf::engine::Engine& engine, tf::engine::FxManager& fx, double seconds, tf::engine::GuestManager* guest = nullptr)
{
    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    double sum = 0.0;
    int count = 0;
    for (int b = 0; b < static_cast<int>(seconds * 48000.0 / 256.0); ++b)
    {
        engine.process(nullptr, 0, outs, 2, 256);
        fx.tick();
        if (guest != nullptr)
            guest->tick();
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

float filePeak(const juce::File& file, double fromSeconds, double toSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return -1.0f;
    const auto from = static_cast<int>(fromSeconds * reader->sampleRate);
    const auto to = std::min(static_cast<int>(toSeconds * reader->sampleRate), static_cast<int>(reader->lengthInSamples));
    if (to <= from)
        return -1.0f;
    juce::AudioBuffer<float> audio(2, to - from);
    reader->read(&audio, 0, to - from, from, true, true);
    return audio.getMagnitude(0, to - from);
}

tf::io::RenderResult renderOnWorker(const tf::io::Performance& performance, tf::io::RenderOptions options)
{
    tf::io::RenderResult result;
    std::atomic<bool> done { false };
    std::thread worker([&] {
        result = tf::io::renderPerformance(performance, options);
        done.store(true);
    });
    while (! done.load())
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    worker.join();
    return result;
}

tf::io::RenderOptions::GuestFactory lend(std::shared_ptr<tf::engine::Instrument> instrument, juce::String failure)
{
    return [instrument, failure](const tf::io::SessionData::GuestData&, const tf::dsp::ProcessSpec&, std::string& error) -> tf::engine::InstrumentPtr {
        if (instrument == nullptr)
        {
            error = failure.toStdString();
            return nullptr;
        }
        return std::make_unique<tf::engine::SharedInstrument>(instrument);
    };
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: tidefield_hostcheck <folder containing Tidefield Test Gain.vst3> [folder containing Tidefield Test Sine.vst3]\n";
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
        juce::FileSearchPath paths;
        for (int a = 1; a < argc; ++a)
            paths.add(juce::File::getCurrentWorkingDirectory().getChildFile(argv[a]));
        if (argc < 3)
        {
            const auto first = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]).getFullPathName();
            paths.add(juce::File(first.replace("TidefieldTestGain_artefacts", "TidefieldTestSine_artefacts")));
        }
        const int found = host.scanNow(paths);
        check(found >= 2, "the test effect and the test instrument are found by a scan");
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
        host.chooseParameter(slot, 1, 0);
        check(host.chosenParameter(slot, 1) == 0 && host.parameterName(slot, 1) == "Gain", "a second knob can be pointed at a chosen parameter");
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
        check(host.chosenParameter(1, 1) == 0, "the chosen parameters come back with the state");

        fx.setType(slot, "");
        renderRms(engine, fx, 0.5);
        check(! host.hasInstance(slot), "clearing the slot releases the plugin");

        const auto synths = host.instruments();
        const auto sine = std::find_if(synths.begin(), synths.end(), [](const auto& d) { return d.name == "Tidefield Test Sine"; });
        check(sine != synths.end(), "the test instrument is listed as an instrument");
        const auto effectList = host.effects();
        check(std::none_of(effectList.begin(), effectList.end(), [](const auto& d) { return d.name == "Tidefield Test Sine"; }), "and not as an effect");
        if (sine != synths.end())
        {
            const auto synthType = app::PluginHost::typeIdFor(*sine);
            engine::Engine e3;
            e3.prepare(48000.0, 256);
            engine::FxManager fx3(e3);
            engine::GuestManager guest(e3);
            guest.setExternal(&host);
            for (const auto& strip : engine::kStrips)
                e3.post(engine::ControlEvent::snapParam(engine::idx(strip.level), -60.0f));
            e3.post(engine::ControlEvent::snapParam(engine::idx(engine::P::GuestLevel), 0.0f));
            e3.post(engine::ControlEvent::snapParam(engine::idx(engine::P::BusALevel), -60.0f));
            e3.post(engine::ControlEvent::snapParam(engine::idx(engine::P::BusBLevel), -60.0f));
            e3.setParam(engine::P::MasterFadeSecs, 0.5f);
            e3.command(engine::Command::FadeIn);
            guest.setType(synthType);
            check(! guest.isMissing() && host.hasInstance(app::PluginHost::kGuestSlot), "the instrument opens in the Guest strip");
            check(guest.getInfo() != nullptr && guest.getInfo()->controls[0] == "Level", "its first control is named after the synth's parameter");
            const float silent = renderRms(e3, fx3, 1.0, &guest);
            check(silent < 1.0e-5f, "it is silent before a note");
            e3.noteOn(69, 0.9f);
            const float playing = renderRms(e3, fx3, 1.5, &guest);
            check(playing > 0.01f, "a note from the keyboard plays it");
            e3.noteOff(69);
            renderRms(e3, fx3, 1.5, &guest);
            const float released = renderRms(e3, fx3, 1.0, &guest);
            check(released < playing * 0.01f, "the note-off releases it");

            e3.setParam(engine::P::GuestPlayFrom, static_cast<float>(engine::Engine::kFromCycles));
            e3.setParam(engine::P::LoopsOn, 1.0f);
            e3.setParam(engine::P::LoopsRate, 4.0f);
            e3.setParam(engine::P::TideRate, 8.0f);
            e3.setParam(engine::P::LoopsDensity, 1.0f);
            e3.setParam(engine::P::LoopsTarget, 3.0f);
            const float cycles = renderRms(e3, fx3, 3.0, &guest);
            check(cycles > 0.005f, "the Cycles play it");
            e3.setParam(engine::P::LoopsOn, 0.0f);
            renderRms(e3, fx3, 2.5, &guest);
            check(renderRms(e3, fx3, 1.0, &guest) < cycles * 0.01f, "stopping the Cycles releases every note");

            e3.setParam(engine::P::GuestPlayFrom, 0.0f);
            e3.setParam(engine::P::GuestP1, 0.0f);
            e3.noteOn(69, 0.9f);
            const float muted = renderRms(e3, fx3, 1.5, &guest);
            check(muted < playing * 0.05f, "control 1 turns the synth's level down");
            e3.noteOff(69);
            host.chooseParameter(app::PluginHost::kGuestSlot, 2, 1);
            check(host.parameterName(app::PluginHost::kGuestSlot, 2) == "Release", "Choose controls points a knob at another parameter");
            const auto synthState = guest.getState();
            check(! synthState.empty(), "the instrument's state can be captured");

            engine::Engine e4;
            e4.prepare(44100.0, 512);
            engine::FxManager fx4(e4);
            engine::GuestManager guest2(e4);
            guest2.setExternal(&host);
            guest2.setType(synthType, false, synthState);
            renderRms(e4, fx4, 0.1, &guest2);
            check(host.hasInstance(app::PluginHost::kGuestSlot) && host.chosenParameter(app::PluginHost::kGuestSlot, 2) == 1,
                  "it reopens from the saved state with its chosen controls");

            const auto guestSlot = app::PluginHost::kGuestSlot;
            const auto liveRevision = host.editRevision(guestSlot);
            io::Performance performance;
            performance.start = io::defaultSession(e4);
            performance.start.guest = { synthType, "Tidefield Test Sine", synthState };
            performance.sampleRate = 48000.0;
            performance.length = static_cast<std::uint64_t>(1.5 * 48000.0);
            performance.events.push_back({ static_cast<std::uint64_t>(0.4 * 48000.0), engine::ControlEvent::note(69, 0.9f), 0 });
            performance.events.push_back({ static_cast<std::uint64_t>(1.0 * 48000.0), engine::ControlEvent::note(69, 0.0f), 0 });
            const dsp::ProcessSpec renderSpec { 48000.0, engine::Engine::kGuestBlock };
            std::vector<juce::MemoryBlock> guestStems;
            for (int pass = 0; pass < 2; ++pass)
            {
                juce::String error;
                auto lent = host.createRenderInstrument(synthType, synthState, renderSpec, error);
                if (pass == 0)
                {
                    check(lent != nullptr, "a fresh instance of the instrument opens for a render");
                    check(host.editRevision(guestSlot) == liveRevision && host.chosenParameter(guestSlot, 2) == 1,
                          "and leaves the live Guest instrument alone");
                }
                io::RenderOptions render;
                render.folder = dir.getChildFile("render" + juce::String(pass));
                render.stems = true;
                render.makeGuest = lend(std::move(lent), error);
                const auto result = renderOnWorker(performance, render);
                const auto stem = render.folder.getChildFile("stems").getChildFile("guest.wav");
                if (pass == 0)
                {
                    check(result.ok && result.warnings.empty(), "a performance with the instrument in the Guest renders without warnings");
                    check(filePeak(stem, 0.0, 0.35) < 1.0e-5f, "the rendered Guest is silent before its note");
                    check(filePeak(stem, 0.5, 0.95) > 0.01f, "the render plays the instrument's note");
                    check(filePeak(render.folder.getChildFile("master.wav"), 0.5, 0.95) > 0.01f, "and the master has it");
                }
                juce::MemoryBlock bytes;
                stem.loadFileAsData(bytes);
                guestStems.push_back(bytes);
            }
            check(guestStems.size() == 2 && guestStems[0].getSize() > 0 && guestStems[0] == guestStems[1],
                  "two renders of the instrument are identical");
            {
                juce::String error;
                auto none = host.createRenderInstrument("plugin:VST3-Not Installed-0-0", {}, renderSpec, error);
                io::RenderOptions render;
                render.folder = dir.getChildFile("renderMissing");
                render.makeGuest = lend(std::move(none), error);
                auto gone = performance;
                gone.start.guest = { "plugin:VST3-Not Installed-0-0", "Not Installed", "kept" };
                const auto result = renderOnWorker(gone, render);
                check(result.ok && result.warnings.size() == 1 && juce::String(result.warnings.front()).contains("Not Installed"),
                      "a missing instrument leaves the render going with a warning");
            }

            auto snapshot = [&] {
                io::SessionData s;
                s.guest = { synthType, guest2.getName(), guest2.getState() };
                s.pluginEdits["guest"] = host.editRevision(guestSlot);
                return s;
            };
            const auto untouched = snapshot();
            renderRms(e4, fx4, 0.3, &guest2);
            check(io::sameContent(untouched, snapshot()), "an untouched instrument does not count as a change to the session");
            host.chooseParameter(guestSlot, 3, 0);
            check(! io::sameContent(untouched, snapshot()), "choosing another parameter for a knob does");

            engine::GuestManager missing(e4);
            missing.setExternal(&host);
            missing.setType("plugin:VST3-Not Installed-0-0", false, "kept", "Not Installed");
            check(missing.isMissing() && missing.getState() == "kept", "a missing instrument keeps its identity and state");

            guest2.clear();
            renderRms(e4, fx4, 0.3, &guest2);
            guest.clear();
            renderRms(e3, fx3, 0.3, &guest);
            check(! host.hasInstance(app::PluginHost::kGuestSlot), "clearing the Guest strip releases the instrument");
            guest.setExternal(nullptr);
            guest2.setExternal(nullptr);
        }
        fx.setExternal(nullptr);
        fx2.setExternal(nullptr);
    }
    dir.deleteRecursively();
    std::cout << (failures == 0 ? "Hosting check passed" : "Hosting check FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
