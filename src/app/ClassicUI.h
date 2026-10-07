#pragma once

#include "AppCore.h"
#include "ui/Pages.h"
#include "ui/Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace tf::app {

/** The JUCE fallback front end (used when the web UI files are missing, or when
    TIDEFIELD_CLASSIC_UI=1): a header with transport, safety and status, and tabs for
    performing, sources, mixer, FX and MIDI. Talks to the engine only through AppCore,
    the same as the web UI. */
class ClassicUI final : public juce::Component, private juce::Timer
{
public:
    explicit ClassicUI(AppCore& core);
    ~ClassicUI() override;

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
    void showRecordMenu();
    void mouseDown(const juce::MouseEvent& e) override;
    void showStatus(const juce::String& message, bool warning = false);
    void updateTitle();
    void onTelemetry(const engine::TelemetryFrame& frame);

    AppCore& core;
    engine::Engine& engine;
    theme::LookAndFeel lookAndFeel;

    juce::TextButton settingsButton { "Audio Settings" };
    juce::TextButton fadeInButton { "Fade In" };
    juce::TextButton fadeOutButton { "Fade Out" };
    juce::TextButton panicButton { "PANIC" };
    juce::TextButton sessionButton { "Session" };
    juce::TextButton catchButton { "Catch" };
    juce::TextButton recordButton { "Record" };
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
