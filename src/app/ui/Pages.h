#pragma once

#include "KnobPanel.h"

#include "../TerrainPad.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <engine/mix/FxManager.h>
#include <engine/scene/SceneManager.h>

#include <array>

namespace tf::app {

/** Shared interface for the placeholder tabs. */
class Page : public juce::Component
{
public:
    virtual void update(const engine::TelemetryFrame& frame) = 0;
};

/** A KnobPanel inside a vertical scroll view. */
class ScrollingPanel : public Page
{
public:
    explicit ScrollingPanel(engine::Engine& engine) : panel(engine)
    {
        viewport.setViewedComponent(&panel, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }

    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        const int w = viewport.getMaximumVisibleWidth();
        panel.setSize(w, panel.layout(w, false));
    }

    void update(const engine::TelemetryFrame& frame) override { panel.update(frame); }

protected:
    juce::Viewport viewport;
    KnobPanel panel;
};

class PerformPage final : public Page, private juce::MidiKeyboardState::Listener
{
public:
    PerformPage(engine::Engine& engine, engine::SceneManager& scenes);
    ~PerformPage() override;
    void update(const engine::TelemetryFrame& frame) override;
    void resized() override;
    void captureScene() { scenes.captureScene({}, last.cursor, last); }
    void releaseLive() { scenes.releaseLiveLayer(); }

private:
    engine::Engine& engine;
    engine::SceneManager& scenes;
    TerrainPad pad;
    juce::TextButton captureButton { "Capture Scene" }, releaseButton { "Release Live" };
    KnobPanel controls;
    juce::Viewport controlsView;
    engine::TelemetryFrame last;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
};

class SourcesPage final : public ScrollingPanel
{
public:
    SourcesPage(engine::Engine& engine);
    void update(const engine::TelemetryFrame& frame) override;

private:
    /** slot 0-3 = clouds, 4 = Bloom. */
    void chooseSample(int slot);

    engine::Engine& engine;
    std::array<juce::TextButton, engine::kNumClouds + 1> loadButtons;
    std::array<juce::Label, engine::kNumClouds + 1> sampleNames;
    std::unique_ptr<juce::FileChooser> chooser;
};

class MixerPage final : public ScrollingPanel
{
public:
    explicit MixerPage(engine::Engine& engine);
    void update(const engine::TelemetryFrame& frame) override;

private:
    struct Meter final : juce::Component
    {
        float l = 0.0f, r = 0.0f;
        void paint(juce::Graphics& g) override;
    };
    std::array<Meter, engine::kNumStrips> meters;
};

class FxPage final : public ScrollingPanel
{
public:
    FxPage(engine::Engine& engine, engine::FxManager& fx);
    void update(const engine::TelemetryFrame& frame) override;

private:
    void refreshSlot(int slot);

    engine::FxManager& fx;
    std::array<juce::ComboBox, engine::kNumFxSlots> typeMenus;
};

} // namespace tf::app
