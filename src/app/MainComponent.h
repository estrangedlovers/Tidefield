#pragma once

#include "AudioHost.h"
#include "MidiInputs.h"
#include "SessionController.h"
#include "ui/Pages.h"
#include "ui/Theme.h"

#include <engine/capture/CatchManager.h>
#include <engine/midi/MidiManager.h>
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
    void showSessionMenu();
    void showStatus(const juce::String& message, bool warning = false);
    void loadFactoryContent();
    void updateTitle();
    void loadRigMidi();
    void saveRigMidi();

    AudioHost& host;
    engine::Engine& engine;
    theme::LookAndFeel lookAndFeel;
    engine::SceneManager scenes;
    engine::FxManager fx;
    engine::CatchManager catcher;
    engine::MidiManager midi;
    MidiInputs midiInputs;
    SessionController session;

    juce::TextButton settingsButton { "Audio Settings" };
    juce::TextButton fadeInButton { "Fade In" };
    juce::TextButton fadeOutButton { "Fade Out" };
    juce::TextButton panicButton { "PANIC" };
    juce::TextButton sessionButton { "Session" };
    juce::TextButton catchButton { "Catch" };
    juce::Label statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    PerformPage* perform = nullptr;
    MidiPage* midiPage = nullptr;
    std::vector<Page*> pages;

    engine::TelemetryFrame lastFrame;
    juce::String statusMessage;
    bool statusIsWarning = false;
    juce::uint32 statusUntil = 0;
    float meterL = 0.0f, meterR = 0.0f;
    juce::Rectangle<int> meterArea;
};

} // namespace tf::app
