#include <engine/Engine.h>
#include <engine/midi/MidiManager.h>
#include <engine/mix/FxManager.h>
#include <engine/scene/SceneManager.h>
#include <io/Session.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tf;
using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;

std::shared_ptr<dsp::SampleBuffer> noise(float seconds, bool stereo, std::uint64_t seed)
{
    auto b = std::make_shared<dsp::SampleBuffer>();
    b->sampleRate = kFs;
    b->name = "noise " + std::to_string(seed);
    dsp::Random rng(seed);
    b->left.resize(static_cast<size_t>(seconds * kFs));
    for (auto& x : b->left)
        x = rng.nextBipolar() * 0.5f;
    if (stereo)
    {
        b->right.resize(b->left.size());
        for (auto& x : b->right)
            x = rng.nextBipolar() * 0.5f;
    }
    return b;
}

struct Rig
{
    engine::Engine engine;
    engine::SceneManager scenes { engine };
    engine::FxManager fx { engine };
    engine::TelemetryFrame last;

    Rig() { engine.prepare(kFs, 512); }

    void run(double seconds)
    {
        std::vector<float> l(512), r(512);
        float* outs[2] = { l.data(), r.data() };
        for (int i = 0; i < static_cast<int>(seconds * kFs / 512); ++i)
        {
            engine.process(nullptr, 0, outs, 2, 512);
            engine::TelemetryFrame f;
            while (engine.popTelemetry(f))
                last = f;
            engine::EngineNotice n;
            while (engine.popNotice(n)) {}
            scenes.tick();
            fx.tick();
            engine.collectGarbage();
        }
    }
};

juce::File tempFile(const juce::String& name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tidefield-tests").getChildFile(name);
}

} // namespace

TEST_CASE("A session round-trips through a .tidefield file with its audio")
{
    Rig a;
    a.fx.loadDefaultLayout();
    a.fx.setType(engine::kMasterSlot, "tf.medium");
    a.engine.setParam(engine::P::DroneCutoff, 3210.0f);
    a.engine.setParam(engine::P::HarmonyRoot, 7.0f);
    a.engine.setParam(engine::P::BloomTransform, 4.0f);
    engine::Scene sc;
    sc.name = "Dusk";
    sc.position = { 0.2f, 0.7f };
    sc.values[engine::idx(engine::P::Cloud1Density)] = 44.0f;
    a.scenes.addScene(sc);
    a.scenes.setPinned(engine::idx(engine::P::DroneLevel), true);
    const auto cloud = noise(1.5f, true, 1);
    const auto bloom = noise(0.8f, false, 2);
    a.engine.loadCloudSample(2, cloud);
    a.engine.loadBloomSample(bloom);
    a.run(0.5);

    auto session = io::captureSession(a.engine, a.last, a.scenes, a.fx);
    session.name = "Round trip";
    const auto file = tempFile("roundtrip.tidefield");
    file.getParentDirectory().createDirectory();
    juce::String error;
    REQUIRE(io::saveSession(session, file, error));
    REQUIRE_FALSE(file.getSiblingFile("roundtrip.tidefield.saving").exists());

    const auto loaded = io::loadSession(file, error);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->warnings.empty());
    REQUIRE(loaded->name == "Round trip");
    REQUIRE(loaded->params.at("drone.cutoff") == Approx(3210.0f));
    REQUIRE(loaded->params.at("harmony.root") == 7.0f);
    REQUIRE(loaded->scenes.size() == 1);
    REQUIRE(loaded->scenes[0].name == "Dusk");
    REQUIRE(loaded->scenes[0].values.at("cloud1.density") == Approx(44.0f));
    REQUIRE(loaded->pins == std::vector<std::string> { "drone.level" });
    REQUIRE(loaded->fx.at("master.fx1") == "tf.medium");
    REQUIRE(loaded->fx.at("busA.fx1") == "tf.reverb");

    const auto& c = *loaded->samples.at("cloud3");
    REQUIRE(c.isStereo());
    REQUIRE(c.size() == cloud->size());
    REQUIRE(c.name == cloud->name);
    for (size_t i = 0; i < c.size(); i += 997)
    {
        REQUIRE(c.left[i] == Approx(cloud->left[i]).margin(1.0e-5));
        REQUIRE(c.right[i] == Approx(cloud->right[i]).margin(1.0e-5));
    }
    REQUIRE_FALSE(loaded->samples.at("bloom")->isStereo());

    // Recall into a fresh engine.
    Rig b;
    const auto warnings = io::applySession(*loaded, b.engine, b.scenes, b.fx, true);
    REQUIRE(warnings.empty());
    b.run(0.5);
    REQUIRE(b.last.paramTargets[engine::idx(engine::P::DroneCutoff)] == Approx(3210.0f));
    REQUIRE(b.last.numScenes == 1);
    REQUIRE(b.last.cloudLoaded[2]);
    REQUIRE_FALSE(b.last.cloudLoaded[0]);
    REQUIRE(b.last.bloomLoaded);
    REQUIRE(b.fx.getType(engine::kMasterSlot) == "tf.medium");
    REQUIRE(b.scenes.isPinned(engine::idx(engine::P::DroneLevel)));
    file.deleteFile();
}

TEST_CASE("MIDI mappings round-trip through a session; sessions without MIDI keep the rig's mapping")
{
    engine::Engine e;
    e.prepare(kFs, 512);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    engine::MidiManager midi(e);
    midi.loadDefaultLayout();
    engine::MidiBinding pad;
    pad.source = engine::MidiBinding::Source::Note;
    pad.channel = 9;
    pad.cc = 36;
    pad.action = engine::MidiAction::Catch;
    midi.addBinding(pad);
    midi.setNotesToDrone(true);
    midi.setNoteChannel(2);

    engine::TelemetryFrame frame;
    auto s = io::captureSession(e, frame, scenes, fx, &midi);
    juce::String error;
    auto parsed = io::sessionFromJson(juce::JSON::parse(juce::JSON::toString(io::sessionToJson(s))), error);
    REQUIRE(parsed.has_value());

    engine::Engine e2;
    e2.prepare(kFs, 512);
    engine::SceneManager scenes2(e2);
    engine::FxManager fx2(e2);
    engine::MidiManager midi2(e2);
    REQUIRE(io::applySession(*parsed, e2, scenes2, fx2, true, &midi2).empty());
    REQUIRE(midi2.getBindings().size() == 9);
    REQUIRE(midi2.getBindings().back().action == engine::MidiAction::Catch);
    REQUIRE(midi2.getBindings().back().source == engine::MidiBinding::Source::Note);
    REQUIRE(midi2.getBindings()[7].high == Approx(midi.getBindings()[7].high));
    REQUIRE(midi2.getNotesToDrone());
    REQUIRE(midi2.getNoteChannel() == 2);

    // A session saved without MIDI leaves the current mapping untouched.
    io::SessionData bare;
    io::applySession(bare, e2, scenes2, fx2, true, &midi2);
    REQUIRE(midi2.getBindings().size() == 9);
}

TEST_CASE("Sessions from the future are refused; unknown IDs become warnings")
{
    io::SessionData s;
    s.params["drone.cutoff"] = 500.0f;
    s.params["some.futureParam"] = 1.0f;
    s.fx["busA.fx1"] = "tf.nonexistent";
    auto json = io::sessionToJson(s);

    juce::String error;
    auto parsed = io::sessionFromJson(json, error);
    REQUIRE(parsed.has_value());
    Rig rig;
    const auto warnings = io::applySession(*parsed, rig.engine, rig.scenes, rig.fx, true);
    REQUIRE(warnings.size() == 2);

    json.getDynamicObject()->setProperty("version", io::SessionData::kCurrentVersion + 1);
    REQUIRE_FALSE(io::sessionFromJson(json, error).has_value());
    REQUIRE(error.contains("newer"));
}

TEST_CASE("Damaged or foreign files fail with a message, not a crash")
{
    const auto file = tempFile("garbage.tidefield");
    file.getParentDirectory().createDirectory();
    file.replaceWithText("this is not a zip");
    juce::String error;
    REQUIRE_FALSE(io::loadSession(file, error).has_value());
    REQUIRE(error.isNotEmpty());
    file.deleteFile();
}

TEST_CASE("The default session resets everything")
{
    Rig rig;
    rig.engine.setParam(engine::P::DroneCutoff, 5000.0f);
    rig.engine.loadCloudSample(0, noise(0.5f, false, 3));
    rig.scenes.addScene({});
    rig.run(0.3);
    io::applySession(io::defaultSession(rig.engine), rig.engine, rig.scenes, rig.fx, true);
    rig.run(0.3);
    REQUIRE(rig.last.paramTargets[engine::idx(engine::P::DroneCutoff)] == Approx(900.0f));
    REQUIRE(rig.last.numScenes == 0);
    REQUIRE_FALSE(rig.last.cloudLoaded[0]);
    REQUIRE(rig.fx.getType(engine::kBusASlot) == "tf.reverb");
}
