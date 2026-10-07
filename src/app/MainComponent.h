#pragma once

#include "AudioHost.h"
#include "TerrainPad.h"

#include <engine/scene/SceneManager.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <vector>

namespace tf::app {

/** Placeholder UI for phases 1 to 5: device settings, fade and panic, terrain pad,
    parameter knobs and meters. It talks to the engine only through Engine::post and
    the telemetry queues, the same contract the React UI will use in phase 6. */
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent(AudioHost& host);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct ParamControl
    {
        engine::P param;
        juce::Slider slider;
        juce::Label label;
        bool wasLive = false;
    };

    struct Section
    {
        juce::String title;
        std::size_t firstControl = 0;
        std::size_t numControls = 0;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void addParamControl(engine::P param);
    void addSection(const juce::String& title, std::initializer_list<engine::P> params);
    void showDeviceSettings();
    void toggleFade();
    void togglePanic();
    void updateTransportButtons();
    void followTelemetry();

    AudioHost& host;
    engine::Engine& engine;
    engine::SceneManager scenes;

    juce::TextButton settingsButton { "Audio Settings" };
    juce::TextButton fadeInButton { "Fade In" };
    juce::TextButton fadeOutButton { "Fade Out" };
    juce::TextButton panicButton { "PANIC" };
    juce::TextButton captureButton { "Capture Scene" };
    juce::TextButton releaseButton { "Release Live" };
    juce::ComboBox wanderStyle;
    juce::Label statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    TerrainPad terrainPad { engine, scenes };

    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<Section> sections;
    bool deviceWarningShown = false;

    engine::TelemetryFrame lastFrame;
    float meterL = 0.0f, meterR = 0.0f;
    juce::Rectangle<int> meterArea, voicesArea;
};

} // namespace tf::app
