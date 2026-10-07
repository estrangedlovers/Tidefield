#pragma once

#include <engine/Engine.h>
#include <engine/scene/SceneManager.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace tf::app {

/** Placeholder 2D terrain (phase 2). The React performance view replaces it in
    phase 6 with the same interactions:

      drag on empty space     move the cursor (the sound morphs, with glide)
      double-click            capture what you hear as a new scene there
      drag a scene            move it
      right-click a scene     commit the live layer into it, rename, delete */
class TerrainPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TerrainPad(engine::Engine& engine, engine::SceneManager& scenes);

    /** Called by the owner's timer with the latest telemetry. */
    void update(const engine::TelemetryFrame& frame);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> padBounds() const;
    juce::Point<float> toScreen(engine::Point2 p) const;
    engine::Point2 toTerrain(juce::Point<float> p) const;
    int sceneAt(juce::Point<float> screen) const;
    void moveCursor(juce::Point<float> screen);
    void showSceneMenu(int scene);

    engine::Engine& engine;
    engine::SceneManager& scenes;
    engine::TelemetryFrame frame;
    int draggingScene = -1;
    bool draggingCursor = false;
};

} // namespace tf::app
