#include <app/Undo.h>
#include <engine/Engine.h>
#include <engine/scene/SceneManager.h>

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace tf;

TEST_CASE("A dragged control undoes in one step and redoes to where it ended", "[undo]")
{
    juce::UndoManager undo;
    std::map<int, float> values { { 7, 0.2f } };
    auto apply = [&values](int p, float v) { values[p] = v; };
    undo.beginNewTransaction("Drag");
    for (float v : { 0.3f, 0.4f, 0.5f, 0.6f })
    {
        undo.perform(new app::ParamAction(7, values[7], v, apply));
        values[7] = v;
    }
    CHECK(undo.getNumActionsInCurrentTransaction() == 1);
    REQUIRE(undo.undo());
    CHECK(values[7] == 0.2f);
    REQUIRE(undo.redo());
    CHECK(values[7] == 0.6f);
}

TEST_CASE("Scene edits undo and redo as whole snapshots", "[undo]")
{
    engine::Engine e;
    e.prepare(48000.0, 256);
    engine::SceneManager scenes(e);
    juce::UndoManager undo;
    using State = std::vector<engine::Scene>;
    auto record = [&](const juce::String& name, const std::function<void()>& change) {
        State before = scenes.getScenes();
        change();
        undo.beginNewTransaction(name);
        undo.perform(new app::SnapshotAction<State>(before, scenes.getScenes(), [&scenes](const State& s) { scenes.replaceAll(s, {}); }));
    };
    engine::TelemetryFrame frame;
    record("Capture", [&] { scenes.captureScene("A", { 0.2f, 0.3f }, frame); });
    record("Capture", [&] { scenes.captureScene("B", { 0.7f, 0.8f }, frame); });
    record("Move", [&] { scenes.moveScene(0, { 0.9f, 0.1f }); });
    record("Delete", [&] { scenes.removeScene(1); });
    REQUIRE(scenes.size() == 1);
    undo.undo();
    REQUIRE(scenes.size() == 2);
    undo.undo();
    CHECK(scenes.getScenes()[0].position.x == 0.2f);
    undo.undo();
    CHECK(scenes.size() == 1);
    undo.redo();
    undo.redo();
    CHECK(scenes.size() == 2);
    CHECK(scenes.getScenes()[0].position.x == 0.9f);
}
