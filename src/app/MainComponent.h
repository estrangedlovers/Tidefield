#pragma once

#include "AudioHost.h"
#include "ui/Pages.h"
#include "ui/Theme.h"

#include <engine/mix/FxManager.h>
#include <engine/scene/SceneManager.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace tf::app {

/** Placeholder UI for phases 1 to 5: a header with transport, safety and status, and
    tabs for performing, sources, mixer and FX. It talks to the engine only through
    Engine::post, the managers and the telemetry queues: the same contract the React
    UI uses in phase 6. */
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent(AudioHost& host);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void showDeviceSettings();
    void toggleFade();
    void togglePanic();
    void updateHeader();

    AudioHost& host;
    engine::Engine& engine;
    theme::LookAndFeel lookAndFeel;
    engine::SceneManager scenes;
    engine::FxManager fx;

    juce::TextButton settingsButton { "Audio Settings" };
    juce::TextButton fadeInButton { "Fade In" };
    juce::TextButton fadeOutButton { "Fade Out" };
    juce::TextButton panicButton { "PANIC" };
    juce::Label statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    PerformPage* perform = nullptr;
    std::vector<Page*> pages;

    engine::TelemetryFrame lastFrame;
    float meterL = 0.0f, meterR = 0.0f;
    juce::Rectangle<int> meterArea;
};

} // namespace tf::app
