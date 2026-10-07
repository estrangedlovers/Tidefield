#include "MainComponent.h"

namespace tf::app {

namespace {

const char* fadeStateName(engine::FadeState s)
{
    switch (s)
    {
        case engine::FadeState::Silent: return "silent";
        case engine::FadeState::FadingIn: return "fading in";
        case engine::FadeState::Open: return "open";
        case engine::FadeState::FadingOut: return "fading out";
    }
    return "";
}

} // namespace

MainComponent::MainComponent(AudioHost& h) : host(h), engine(h.getEngine()), scenes(h.getEngine()), fx(h.getEngine())
{
    // Default (not per-component) so every child copies the palette when it is built.
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    fx.loadDefaultLayout();

    for (auto* b : { &settingsButton, &fadeInButton, &fadeOutButton, &panicButton })
        addAndMakeVisible(*b);
    panicButton.setColour(juce::TextButton::buttonColourId, theme::warn.darker(0.6f));

    settingsButton.onClick = [this] { showDeviceSettings(); };
    fadeInButton.onClick = [this] { engine.command(engine::Command::FadeIn); };
    fadeOutButton.onClick = [this] { engine.command(engine::Command::FadeOut); };
    panicButton.onClick = [this] { togglePanic(); };
    fadeInButton.setTooltip("Space toggles fade in / fade out");
    panicButton.setTooltip("Esc: fast fade to silence and reset. Press again to resume.");

    statusLabel.setColour(juce::Label::textColourId, theme::textDim);
    statusLabel.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(statusLabel);

    auto addPage = [this](const juce::String& name, Page* page) {
        tabs.addTab(name, theme::background, page, true);
        pages.push_back(page);
    };
    perform = new PerformPage(engine, scenes);
    addPage("Perform", perform);
    addPage("Sources", new SourcesPage(engine));
    addPage("Mixer", new MixerPage(engine));
    addPage("FX", new FxPage(engine, fx));
    tabs.setTabBarDepth(32);
    addAndMakeVisible(tabs);

    setWantsKeyboardFocus(true);
    setSize(1380, 860);
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    tabs.clearTabs();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (key == juce::KeyPress::spaceKey)
        toggleFade();
    else if (key == juce::KeyPress::escapeKey)
        togglePanic();
    else if (c == 'c')
        perform->captureScene();
    else if (c == 'r')
        perform->releaseLive();
    else if (c >= '1' && c <= '4')
        tabs.setCurrentTabIndex(c - '1');
    else
        return false;
    return true;
}

void MainComponent::toggleFade()
{
    using engine::FadeState;
    const bool goingUp = lastFrame.fadeState == FadeState::Silent || lastFrame.fadeState == FadeState::FadingOut;
    engine.command(goingUp ? engine::Command::FadeIn : engine::Command::FadeOut);
}

void MainComponent::togglePanic()
{
    engine.command(lastFrame.panicActive ? engine::Command::ResumeFromPanic : engine::Command::Panic);
}

void MainComponent::updateHeader()
{
    using engine::FadeState;
    const auto state = lastFrame.fadeState;
    const bool up = state == FadeState::FadingIn || state == FadeState::Open;
    const bool moving = state == FadeState::FadingIn || state == FadeState::FadingOut;
    const auto active = theme::accent.darker(0.4f);
    fadeInButton.setColour(juce::TextButton::buttonColourId, up ? (moving ? active.withAlpha(0.7f) : active) : theme::button);
    fadeOutButton.setColour(juce::TextButton::buttonColourId, ! up && moving ? active.withAlpha(0.7f) : theme::button);
    panicButton.setButtonText(lastFrame.panicActive ? "Resume" : "PANIC");
    panicButton.setColour(juce::TextButton::buttonColourId, lastFrame.panicActive ? theme::warn : theme::warn.darker(0.6f));

    // No usable output device (no default on this machine, or a remembered interface
    // that is unplugged): say so and highlight the settings button, without a modal.
    if (host.getDeviceManager().getCurrentAudioDevice() == nullptr)
    {
        statusLabel.setColour(juce::Label::textColourId, theme::warn);
        statusLabel.setText("No audio output is open. Choose a device in Audio Settings.", juce::dontSendNotification);
        settingsButton.setColour(juce::TextButton::buttonColourId, theme::warn.darker(0.5f));
        return;
    }
    statusLabel.setColour(juce::Label::textColourId, theme::textDim);
    settingsButton.setColour(juce::TextButton::buttonColourId, theme::button);

    const int liveCount = static_cast<int>(std::count_if(lastFrame.live.begin(), lastFrame.live.end(), [](auto v) { return v != 0; }));
    statusLabel.setText(juce::String::formatted("CPU %4.1f%%   xruns %d   master %s   limiter %4.1f dB   tide %.2fx   scenes %d   live %d   guard %u",
                                                host.getCpuLoad() * 100.0, host.getXrunCount(), fadeStateName(lastFrame.fadeState),
                                                juce::Decibels::gainToDecibels(lastFrame.limiterGain), lastFrame.tide, scenes.size(),
                                                liveCount, lastFrame.guardTrips),
                        juce::dontSendNotification);
}

void MainComponent::showDeviceSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(host.getDeviceManager(), 0, 2, 2, 2, false, false,
                                                                           true, false);
    selector->setSize(520, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::timerCallback()
{
    scenes.tick();
    fx.tick();
    engine.collectGarbage();

    engine::TelemetryFrame f;
    bool got = false;
    while (engine.popTelemetry(f))
        got = true;
    if (got)
    {
        lastFrame = f;
        for (auto* p : pages)
            if (p->isShowing())
                p->update(lastFrame);
    }

    engine::EngineNotice notice;
    while (engine.popNotice(notice))
        if (notice.type == engine::EngineNotice::Type::GuardTripped)
            juce::Logger::writeToLog("Safety guard tripped");

    meterL = std::max(lastFrame.peakL, meterL * 0.85f);
    meterR = std::max(lastFrame.peakR, meterR * 0.85f);
    updateHeader();

    repaint(meterArea);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::text);
    g.setFont(juce::FontOptions(22.0f));
    g.drawText("Tidefield", 20, 12, 160, 30, juce::Justification::centredLeft);

    g.setColour(theme::panel);
    g.fillRoundedRectangle(meterArea.toFloat(), 3.0f);
    auto bar = [&](juce::Rectangle<float> r, float peak) {
        const float db = juce::Decibels::gainToDecibels(peak, -60.0f);
        g.setColour(db > -1.5f ? theme::warn : theme::accent);
        g.fillRoundedRectangle(r.withWidth(juce::jmap(db, -60.0f, 0.0f, 0.0f, r.getWidth())), 2.0f);
    };
    auto m = meterArea.toFloat().reduced(2.0f);
    bar(m.removeFromTop(m.getHeight() / 2).reduced(0, 1), meterL);
    bar(m.reduced(0, 1), meterR);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(16, 10);
    auto top = area.removeFromTop(36);
    top.removeFromLeft(150);
    settingsButton.setBounds(top.removeFromLeft(130).reduced(3));
    fadeInButton.setBounds(top.removeFromLeft(96).reduced(3));
    fadeOutButton.setBounds(top.removeFromLeft(96).reduced(3));
    panicButton.setBounds(top.removeFromRight(110).reduced(3));
    top.removeFromRight(10);
    meterArea = top.removeFromRight(200).reduced(0, 8);

    statusLabel.setBounds(area.removeFromTop(24));
    tabs.setBounds(area);
}

} // namespace tf::app
