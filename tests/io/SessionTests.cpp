#include <engine/Engine.h>
#include <engine/guest/GuestManager.h>
#include <engine/midi/MidiManager.h>
#include <engine/mix/FxManager.h>
#include <engine/mod/ModRouteManager.h>
#include <engine/mod/SeasonManager.h>
#include <engine/perform/GestureManager.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/SceneManager.h>
#include <io/Presets.h>
#include <io/Session.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

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
}

TEST_CASE("A session round-trips through a .tide file with its audio")
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
    const auto file = tempFile("roundtrip.tide");
    file.getParentDirectory().createDirectory();
    juce::String error;
    REQUIRE(io::saveSession(session, file, error));
    REQUIRE_FALSE(file.getSiblingFile("roundtrip.tide.saving").exists());

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
    const auto file = tempFile("garbage.tide");
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

TEST_CASE("Seasons round-trip through a session and an empty list clears them", "[session][seasons]")
{
    engine::Engine e;
    e.prepare(kFs, 256);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    engine::SeasonManager seasons(e);
    engine::Season s;
    s.param = engine::idx(engine::P::DroneCutoff);
    s.depth = -0.3f;
    s.periodSeconds = 600.0f;
    s.shape = engine::Season::Shape::Drift;
    s.phase = 0.25f;
    REQUIRE(seasons.set(0, s));

    engine::TelemetryFrame frame;
    auto data = io::captureSession(e, frame, scenes, fx, nullptr, &seasons);
    juce::String error;
    auto parsed = io::sessionFromJson(juce::JSON::parse(juce::JSON::toString(io::sessionToJson(data))), error);
    REQUIRE(parsed.has_value());

    engine::Engine e2;
    e2.prepare(kFs, 256);
    engine::SceneManager scenes2(e2);
    engine::FxManager fx2(e2);
    engine::SeasonManager seasons2(e2);
    REQUIRE(io::applySession(*parsed, e2, scenes2, fx2, true, nullptr, &seasons2).empty());
    REQUIRE(seasons2.getSeasons().size() == 1);
    const auto& r = seasons2.getSeasons()[0];
    CHECK(r.param == s.param);
    CHECK(r.depth == Approx(-0.3f));
    CHECK(r.periodSeconds == Approx(600.0f));
    CHECK(r.shape == engine::Season::Shape::Drift);
    CHECK(r.phase == Approx(0.25f));

    io::applySession(io::defaultSession(e2), e2, scenes2, fx2, true, nullptr, &seasons2);
    CHECK(seasons2.getSeasons().empty());
}

TEST_CASE("Modulation routes round-trip through a session and unknown ones become warnings", "[session][mod]")
{
    engine::Engine e;
    e.prepare(kFs, 256);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    engine::ModRouteManager mod(e);
    REQUIRE(mod.add(engine::ModSource::Lfo2, engine::idx(engine::P::DroneCutoff), 0.3f) == 0);
    REQUIRE(mod.add(engine::ModSource::InputLevel, engine::idx(engine::P::BloomTone), -0.5f) == 1);
    mod.remove(0);
    REQUIRE(mod.add(engine::ModSource::Random2, engine::idx(engine::P::Cloud1Spray), 0.2f) == 1);

    engine::TelemetryFrame frame;
    auto data = io::captureSession(e, frame, scenes, fx, nullptr, nullptr, nullptr, nullptr, &mod);
    auto json = io::sessionToJson(data);
    juce::String error;
    auto parsed = io::sessionFromJson(juce::JSON::parse(juce::JSON::toString(json)), error);
    REQUIRE(parsed.has_value());

    engine::Engine e2;
    e2.prepare(kFs, 256);
    engine::SceneManager scenes2(e2);
    engine::FxManager fx2(e2);
    engine::ModRouteManager mod2(e2);
    REQUIRE(io::applySession(*parsed, e2, scenes2, fx2, true, nullptr, nullptr, nullptr, nullptr, &mod2).empty());
    REQUIRE(mod2.getRoutes().size() == 2);
    CHECK(mod2.getRoutes()[0].source == engine::ModSource::InputLevel);
    CHECK(mod2.getRoutes()[0].param == engine::idx(engine::P::BloomTone));
    CHECK(mod2.getRoutes()[0].slot == 1);
    CHECK(mod2.getRoutes()[1].source == engine::ModSource::Random2);
    CHECK(mod2.getRoutes()[1].slot == 0);

    auto* routes = json.getProperty("modRoutes", {}).getArray();
    REQUIRE(routes != nullptr);
    routes->getReference(0).getDynamicObject()->setProperty("source", "theremin");
    parsed = io::sessionFromJson(juce::JSON::parse(juce::JSON::toString(json)), error);
    CHECK(io::applySession(*parsed, e2, scenes2, fx2, true, nullptr, nullptr, nullptr, nullptr, &mod2).size() == 1);
    CHECK(mod2.getRoutes().size() == 1);

    io::applySession(io::defaultSession(e2), e2, scenes2, fx2, true, nullptr, nullptr, nullptr, nullptr, &mod2);
    CHECK(mod2.getRoutes().empty());
}

TEST_CASE("A session from before a parameter or slot existed resets it to default", "[session]")
{
    engine::Engine e;
    e.prepare(kFs, 256);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    e.setParam(engine::P::WeatherWind, 0.8f);
    fx.setType(engine::FxManager::findSlot("weather.fx1"), "tf.ensemble", false);

    auto old = io::defaultSession(e);
    for (auto it = old.params.begin(); it != old.params.end();)
        it = it->first.rfind("weather.", 0) == 0 ? old.params.erase(it) : std::next(it);
    old.fx.erase("weather.fx1");
    old.fx.erase("weather.fx2");

    io::applySession(old, e, scenes, fx, true);
    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    e.process(nullptr, 0, outs, 2, 256);
    engine::TelemetryFrame f;
    for (int b = 0; b < 400; ++b)
    {
        e.process(nullptr, 0, outs, 2, 256);
        while (e.popTelemetry(f)) {}
    }
    CHECK(f.paramTargets[engine::idx(engine::P::WeatherWind)] == Approx(0.0f));
    CHECK(fx.getType(engine::FxManager::findSlot("weather.fx1")).empty());
}

TEST_CASE("A drawn path round-trips through a session and a session without one clears it", "[session][path]")
{
    engine::Engine e;
    e.prepare(kFs, 256);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    engine::PathManager paths(e);
    paths.set({ { 0.2f, 0.2f }, { 0.8f, 0.25f }, { 0.6f, 0.9f } });

    engine::TelemetryFrame frame;
    auto data = io::captureSession(e, frame, scenes, fx, nullptr, nullptr, &paths);
    juce::String error;
    auto parsed = io::sessionFromJson(juce::JSON::parse(juce::JSON::toString(io::sessionToJson(data))), error);
    REQUIRE(parsed.has_value());

    engine::Engine e2;
    e2.prepare(kFs, 256);
    engine::SceneManager scenes2(e2);
    engine::FxManager fx2(e2);
    engine::PathManager paths2(e2);
    io::applySession(*parsed, e2, scenes2, fx2, true, nullptr, nullptr, &paths2);
    REQUIRE(paths2.getStroke().size() == 3);
    CHECK(paths2.getStroke()[1].x == Approx(0.8f));
    CHECK(paths2.getStroke()[2].y == Approx(0.9f));

    io::applySession(io::defaultSession(e2), e2, scenes2, fx2, true, nullptr, nullptr, &paths2);
    CHECK_FALSE(paths2.hasPath());
}

TEST_CASE("A gesture take round-trips through a session", "[session][gesture]")
{
    engine::Engine e;
    e.prepare(kFs, 256);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    engine::GestureManager gestures(e);
    engine::GestureTake take;
    take.sampleRate = kFs;
    take.length = static_cast<std::uint64_t>(4.0 * kFs);
    take.loop = false;
    take.events.push_back({ static_cast<std::uint64_t>(0.25 * kFs), engine::ControlEvent::setParam(engine::idx(engine::P::TerrainX), 0.8f) });
    take.events.push_back({ static_cast<std::uint64_t>(1.5 * kFs), engine::ControlEvent::note(67, 0.6f) });
    take.events.push_back({ static_cast<std::uint64_t>(2.0 * kFs), engine::ControlEvent::makeCommand(engine::Command::Catch) });
    gestures.setTake(take);

    engine::TelemetryFrame frame;
    auto data = io::captureSession(e, frame, scenes, fx, nullptr, nullptr, nullptr, &gestures);
    juce::String error;
    auto parsed = io::sessionFromJson(juce::JSON::parse(juce::JSON::toString(io::sessionToJson(data))), error);
    REQUIRE(parsed.has_value());

    engine::Engine e2;
    e2.prepare(kFs, 256);
    engine::SceneManager scenes2(e2);
    engine::FxManager fx2(e2);
    engine::GestureManager gestures2(e2);
    REQUIRE(io::applySession(*parsed, e2, scenes2, fx2, true, nullptr, nullptr, nullptr, &gestures2).empty());
    const auto& got = gestures2.getTake();
    REQUIRE(got.events.size() == 3);
    CHECK_FALSE(got.loop);
    CHECK(static_cast<double>(got.length) / got.sampleRate == Approx(4.0));
    CHECK(got.events[0].event.param == engine::idx(engine::P::TerrainX));
    CHECK(got.events[0].event.value == Approx(0.8f));
    CHECK(static_cast<double>(got.events[1].time) / got.sampleRate == Approx(1.5));
    CHECK(got.events[1].event.type == engine::ControlEvent::Type::Note);
    CHECK(got.events[2].event.command == engine::Command::Catch);

    io::applySession(io::defaultSession(e2), e2, scenes2, fx2, true, nullptr, nullptr, nullptr, &gestures2);
    CHECK_FALSE(gestures2.hasTake());
}

TEST_CASE("Presets save, list after the factory ones, and delete", "[presets]")
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tidefield-preset-test");
    dir.deleteRecursively();
    io::PresetLibrary lib(dir);
    lib.addFactory({ "Zeta", "cloud", { { "density", 5.0f } }, true });
    lib.addFactory({ "Alpha", "cloud", { { "density", 9.0f } }, true });
    lib.addFactory({ "Other kind", "drone", { { "root", 40.0f } }, true });

    juce::String error;
    REQUIRE(lib.save({ "My: dusty/cloud", "cloud", { { "density", 33.0f }, { "pitch", -12.0f } }, false }, error));
    REQUIRE(lib.save({ "Bright", "fx:tf.reverb", { { "p1", 0.8f }, { "mix", 0.4f } }, false }, error));

    const auto clouds = lib.list("cloud");
    REQUIRE(clouds.size() == 3);
    CHECK(clouds[0].name == "Alpha");
    CHECK(clouds[1].name == "Zeta");
    CHECK_FALSE(clouds[2].factory);
    CHECK(clouds[2].name == "My: dusty/cloud");
    CHECK(clouds[2].values.at("pitch") == Approx(-12.0f));
    REQUIRE(lib.list("fx:tf.reverb").size() == 1);
    CHECK(lib.list("fx:tf.reverb")[0].values.at("mix") == Approx(0.4f));

    CHECK_FALSE(lib.remove(clouds[0]));
    CHECK(lib.remove(clouds[2]));
    CHECK(lib.list("cloud").size() == 2);
    dir.deleteRecursively();
}

TEST_CASE("Damaged or hostile sessions are refused or made safe", "[session][robust]")
{
    engine::Engine e;
    e.prepare(kFs, 256);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    engine::SeasonManager seasons(e);

    auto data = io::defaultSession(e);
    auto* season = new juce::DynamicObject();
    season->setProperty("param", "drone.cutoff");
    season->setProperty("period", 0.0);
    season->setProperty("depth", 50.0);
    data.seasons = juce::Array<juce::var> { juce::var(season) };
    fx.setType(engine::kBusASlot, "tf.reverb", false);
    data.fx[engine::kFxSlots[static_cast<std::size_t>(engine::kBusASlot)].id] = "someone.elses.shimmer";
    io::applySession(data, e, scenes, fx, true, nullptr, &seasons);
    REQUIRE(seasons.getSeasons().size() == 1);
    CHECK(seasons.getSeasons()[0].periodSeconds >= 20.0f);
    CHECK(seasons.getSeasons()[0].depth <= 1.0f);
    CHECK(fx.getType(engine::kBusASlot).empty());

    std::vector<float> l(256), r(256);
    float* outs[2] = { l.data(), r.data() };
    bool finite = true;
    for (int b = 0; b < 200; ++b)
    {
        e.process(nullptr, 0, outs, 2, 256);
        for (float v : l)
            finite = finite && std::isfinite(v);
    }
    CHECK(finite);

    CHECK(e.getRegistry().spec(engine::P::DroneCutoff).clamp(std::numeric_limits<float>::quiet_NaN()) == Approx(900.0f));

    juce::MemoryOutputStream out;
    juce::String error;
    REQUIRE(io::writeSession(io::defaultSession(e), out, error));
    const auto block = out.getMemoryBlock();
    CHECK_FALSE(io::readSession(block.getData(), block.getSize() / 2, error).has_value());
    const char garbage[] = "PK\x03\x04 this is not really a zip at all";
    CHECK_FALSE(io::readSession(garbage, sizeof(garbage), error).has_value());
    CHECK(io::readSession(block.getData(), block.getSize(), error).has_value());
}

TEST_CASE("Preset names that share a file name do not overwrite each other", "[presets]")
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tidefield-preset-collide");
    dir.deleteRecursively();
    io::PresetLibrary lib(dir);
    juce::String error;
    REQUIRE(lib.save({ "Pad?", "drone", { { "root", 40.0f } }, false }, error));
    REQUIRE(lib.save({ "Pad", "drone", { { "root", 50.0f } }, false }, error));
    REQUIRE(lib.save({ "Pad", "drone", { { "root", 55.0f } }, false }, error));
    auto list = lib.list("drone");
    REQUIRE(list.size() == 2);
    for (const auto& p : list)
        if (p.name == "Pad?")
            CHECK(lib.remove(p));
    list = lib.list("drone");
    REQUIRE(list.size() == 1);
    CHECK(list[0].name == "Pad");
    CHECK(list[0].values.at("root") == Approx(55.0f));
    dir.deleteRecursively();
}

TEST_CASE("Bloom's keyboard of sounds round-trips through a session file", "[session][bloom]")
{
    engine::Engine e;
    e.prepare(kFs, 256);
    engine::SceneManager scenes(e);
    engine::FxManager fx(e);
    auto tone = [](float hz) {
        auto b = std::make_shared<dsp::SampleBuffer>();
        b->sampleRate = kFs;
        b->left.resize(24000);
        for (std::size_t i = 0; i < b->left.size(); ++i)
            b->left[i] = 0.3f * std::sin(2.0f * 3.14159265f * hz * static_cast<float>(i) / static_cast<float>(kFs));
        b->name = "tone";
        return b;
    };
    REQUIRE(e.loadBloomZones({ { tone(220.0f), 57.0f }, { tone(440.0f), 69.0f }, { tone(880.0f), 81.0f } }));

    engine::TelemetryFrame frame;
    auto data = io::captureSession(e, frame, scenes, fx);
    const auto file = juce::File::createTempFile(".tide");
    juce::String error;
    REQUIRE(io::saveSession(data, file, error));
    auto loaded = io::loadSession(file, error);
    file.deleteFile();
    REQUIRE(loaded.has_value());

    engine::Engine e2;
    e2.prepare(kFs, 256);
    engine::SceneManager scenes2(e2);
    engine::FxManager fx2(e2);
    io::applySession(*loaded, e2, scenes2, fx2, true);
    const auto zones = e2.getBloomZones();
    REQUIRE(zones.size() == 3);
    CHECK(zones[0].root == 57.0f);
    CHECK(zones[1].root == 69.0f);
    CHECK(zones[2].root == 81.0f);
    CHECK(zones[2].buffer->size() == 24000);
}

TEST_CASE("Projects saved as .tidefield by 1.3 and earlier still open")
{
    Rig a;
    a.fx.loadDefaultLayout();
    a.engine.setParam(engine::P::DroneCutoff, 1234.0f);
    a.run(0.2);
    auto session = io::captureSession(a.engine, a.last, a.scenes, a.fx);
    const auto legacy = tempFile("old project.tidefield");
    legacy.getParentDirectory().createDirectory();
    juce::String error;
    REQUIRE(io::saveSession(session, legacy, error));
    REQUIRE(io::isSessionFile(legacy));
    REQUIRE(io::isSessionFile(legacy.withFileExtension(io::kSessionExtension)));
    REQUIRE_FALSE(io::isSessionFile(legacy.withFileExtension(".wav")));
    const auto loaded = io::loadSession(legacy, error);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->params.at("drone.cutoff") == Approx(1234.0f));
    REQUIRE(juce::String(io::kSessionExtension) == ".tide");
}

TEST_CASE("Macro names, targets and ranges survive a save and reload")
{
    Rig a;
    engine::ModRouteManager mod(a.engine);
    REQUIRE(mod.addMacroTarget(2, engine::idx(engine::P::DroneCutoff), 0.1f, -0.7f));
    REQUIRE(mod.addMacroTarget(2, engine::idx(engine::P::BusALevel), 0.0f, 0.4f));
    mod.setMacroName(2, "Darken");
    auto session = io::captureSession(a.engine, a.last, a.scenes, a.fx, nullptr, nullptr, nullptr, nullptr, &mod);
    const auto file = tempFile("macros.tide");
    file.getParentDirectory().createDirectory();
    juce::String error;
    REQUIRE(io::saveSession(session, file, error));
    const auto loaded = io::loadSession(file, error);
    REQUIRE(loaded.has_value());

    Rig b;
    engine::ModRouteManager mod2(b.engine);
    const auto warnings = io::applySession(*loaded, b.engine, b.scenes, b.fx, true, nullptr, nullptr, nullptr, nullptr, &mod2);
    CHECK(warnings.empty());
    const auto& m = mod2.getMacros()[2];
    CHECK(m.name == "Darken");
    REQUIRE(m.targets.size() == 2);
    CHECK(m.targets[0].param == engine::idx(engine::P::DroneCutoff));
    CHECK(m.targets[0].from == Approx(0.1f));
    CHECK(m.targets[0].to == Approx(-0.7f));
    CHECK(mod2.getMacros()[0].name == "Macro 1");
}

TEST_CASE("A session counts as changed only when its content changes")
{
    Rig a;
    engine::ModRouteManager mod(a.engine);
    const auto saved = io::captureSession(a.engine, a.last, a.scenes, a.fx, nullptr, nullptr, nullptr, nullptr, &mod);
    auto renamed = saved;
    renamed.name = "Another name";
    CHECK(io::sameContent(saved, renamed));

    auto nudged = saved;
    nudged.params.at("drone.cutoff") += 1.0e-7f;
    CHECK(io::sameContent(saved, nudged));
    nudged.params.at("drone.cutoff") += 50.0f;
    CHECK_FALSE(io::sameContent(saved, nudged));

    REQUIRE(mod.addMacroTarget(0, engine::idx(engine::P::DroneCutoff), 0.0f, 0.5f));
    const auto withMacro = io::captureSession(a.engine, a.last, a.scenes, a.fx, nullptr, nullptr, nullptr, nullptr, &mod);
    CHECK_FALSE(io::sameContent(saved, withMacro));
}

TEST_CASE("The Guest instrument's identity and state survive a session, and a missing one only warns", "[session][guest]")
{
    Rig a;
    engine::GuestManager guestA(a.engine);
    guestA.setType("plugin:VST3-Pad Synth-1-2", false, "map=0,1,2,3,4,5;c3RhdGU=", "Pad Synth");
    a.engine.setParam(engine::P::GuestPlayFrom, 3.0f);
    a.run(0.2);
    auto session = io::captureSession(a.engine, a.last, a.scenes, a.fx, nullptr, nullptr, nullptr, nullptr, nullptr, &guestA);
    REQUIRE(session.guest.type == "plugin:VST3-Pad Synth-1-2");
    REQUIRE(session.guest.name == "Pad Synth");
    REQUIRE(session.guest.state == "map=0,1,2,3,4,5;c3RhdGU=");

    juce::MemoryOutputStream out;
    juce::String error;
    REQUIRE(io::writeSession(session, out, error));
    auto loaded = io::readSession(out.getData(), out.getDataSize(), error);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->guest.type == session.guest.type);
    REQUIRE(loaded->guest.state == session.guest.state);
    REQUIRE(io::sameContent(session, *loaded));

    Rig b;
    engine::GuestManager guestB(b.engine);
    const auto warnings = io::applySession(*loaded, b.engine, b.scenes, b.fx, true, nullptr, nullptr, nullptr, nullptr, nullptr, &guestB);
    REQUIRE(guestB.isMissing());
    REQUIRE(guestB.getState() == session.guest.state);
    bool warned = false;
    for (const auto& w : warnings)
        warned = warned || juce::String(w).contains("Pad Synth");
    REQUIRE(warned);
    b.run(0.2);
    REQUIRE(b.last.paramTargets[engine::idx(engine::P::GuestPlayFrom)] == 3.0f);
    const auto again = io::captureSession(b.engine, b.last, b.scenes, b.fx, nullptr, nullptr, nullptr, nullptr, nullptr, &guestB);
    REQUIRE(again.guest.type == session.guest.type);
    REQUIRE(again.guest.state == session.guest.state);

    auto changed = session;
    changed.guest.state = "different";
    REQUIRE_FALSE(io::sameContent(session, changed));

    auto json = io::sessionToJson(session);
    json.getDynamicObject()->removeProperty("guest");
    auto old = io::sessionFromJson(json, error);
    REQUIRE(old.has_value());
    REQUIRE(old->guest.type.empty());
    io::applySession(*old, b.engine, b.scenes, b.fx, true, nullptr, nullptr, nullptr, nullptr, nullptr, &guestB);
    REQUIRE(guestB.isEmpty());
}
