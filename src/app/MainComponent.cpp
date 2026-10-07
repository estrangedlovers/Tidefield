#include "MainComponent.h"

#include <BinaryData.h>
#include <io/AudioFileIO.h>
#include <io/Session.h>

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

MainComponent::MainComponent(AudioHost& h)
    : host(h), engine(h.getEngine()), scenes(h.getEngine()), fx(h.getEngine()), catcher(h.getEngine()), midi(h.getEngine()),
      midiInputs(h.getEngine(), h.getSettings()), session(h.getEngine(), scenes, fx, &midi)
{
    knobContext().midi = &midi;
    midi.onLearned = [this](const std::string& d) { showStatus("MIDI learned: " + juce::String(d)); };
    loadRigMidi();
    // Default (not per-component) so every child copies the palette when it is built.
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    fx.loadDefaultLayout();

    for (auto* b : { &settingsButton, &fadeInButton, &fadeOutButton, &panicButton, &sessionButton, &catchButton })
        addAndMakeVisible(*b);
    panicButton.setColour(juce::TextButton::buttonColourId, theme::warn.darker(0.6f));
    catchButton.setColour(juce::TextButton::buttonColourId, theme::accent.darker(0.55f));
    catchButton.setTooltip("K: capture the last few seconds of the output into a granular cloud");
    catchButton.onClick = [this] { engine.command(engine::Command::Catch); };
    sessionButton.onClick = [this] { showSessionMenu(); };
    sessionButton.setTooltip("New, open and save sessions (Cmd+N, Cmd+O, Cmd+S)");

    catcher.onCaught = [this](int cloud, const std::string& name) {
        showStatus("Caught into Cloud " + juce::String(cloud + 1) + " (" + juce::String(name) + ")");
    };
    catcher.onRejected = [this](const std::string& reason) { showStatus(reason, true); };
    session.onStatus = [this](const juce::String& m) { showStatus(m); };
    session.onSessionChanged = [this] { updateTitle(); };

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
    midiPage = new MidiPage(midi, midiInputs);
    addPage("MIDI", midiPage);
    tabs.setTabBarDepth(32);
    addAndMakeVisible(tabs);

    setWantsKeyboardFocus(true);
    setSize(1380, 900);
    loadFactoryContent();
    startTimerHz(30);
}

void MainComponent::loadFactoryContent()
{
    // A first launch should make sound: Bloom gets the glass one-shot, Cloud 1 a pad.
    auto decode = [](const void* data, int size, const char* name) -> std::shared_ptr<const dsp::SampleBuffer> {
        juce::String error;
        auto b = io::loadSample(std::make_unique<juce::MemoryInputStream>(data, static_cast<size_t>(size), false), name, error);
        return std::shared_ptr<const dsp::SampleBuffer>(std::move(b));
    };
    engine.loadBloomSample(decode(BinaryData::glass_wav, BinaryData::glass_wavSize, "glass"));
    engine.loadCloudSample(0, decode(BinaryData::chord_wav, BinaryData::chord_wavSize, "chord"));
    engine.setParam(engine::P::BloomRoot, 81.0f); // glass.wav rings at A5
}

void MainComponent::loadRigMidi()
{
    // The controller mapping belongs to the rig: it persists in the app settings and
    // only changes when a session that carries its own mapping is opened.
    const auto stored = host.getSettings().getValue("midiMapping");
    if (stored.isNotEmpty())
        io::applyMidiJson(juce::JSON::parse(stored), midi, engine.getRegistry());
    else
        midi.loadDefaultLayout();
}

void MainComponent::saveRigMidi()
{
    host.getSettings().setValue("midiMapping", juce::JSON::toString(io::midiToJson(midi, engine.getRegistry()), true));
    host.getSettings().saveIfNeeded();
}

void MainComponent::showStatus(const juce::String& message, bool warning)
{
    statusMessage = message;
    statusIsWarning = warning;
    statusUntil = juce::Time::getMillisecondCounter() + 4000;
}

void MainComponent::updateTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName("Tidefield - " + session.getName());
}

void MainComponent::showSessionMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "New session");
    menu.addItem(2, "Open...");
    menu.addSeparator();
    menu.addItem(3, "Save");
    menu.addItem(4, "Save as...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sessionButton), [this](int r) {
        switch (r)
        {
            case 1: session.newSession(); break;
            case 2: session.open(); break;
            case 3: session.save(); break;
            case 4: session.saveAs(); break;
            default: break;
        }
    });
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveRigMidi();
    knobContext().midi = nullptr;
    tabs.clearTabs();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (key.getModifiers().isCommandDown())
    {
        const auto code = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
        if (code == 's')
            key.getModifiers().isShiftDown() ? session.saveAs() : session.save();
        else if (code == 'o')
            session.open();
        else if (code == 'n')
            session.newSession();
        else
            return false;
        return true;
    }
    if (c == 'k')
        engine.command(engine::Command::Catch);
    else if (key == juce::KeyPress::spaceKey)
        toggleFade();
    else if (key == juce::KeyPress::escapeKey)
        togglePanic();
    else if (c == 'c')
        perform->captureScene();
    else if (c == 'r')
        perform->releaseLive();
    else if (c >= '1' && c <= '5')
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
    settingsButton.setColour(juce::TextButton::buttonColourId, theme::button);
    if (juce::Time::getMillisecondCounter() < statusUntil)
    {
        statusLabel.setColour(juce::Label::textColourId, statusIsWarning ? theme::warn : theme::live);
        statusLabel.setText(statusMessage, juce::dontSendNotification);
        return;
    }
    statusLabel.setColour(juce::Label::textColourId, theme::textDim);

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
    midi.tick();
    engine.collectGarbage();

    engine::RawMidi monitored;
    while (engine.popMidiMonitor(monitored))
    {
        midi.handleMonitor(monitored);
        midiPage->noteActivity(monitored);
    }

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

    session.setLatest(lastFrame);
    engine::EngineNotice notice;
    while (engine.popNotice(notice))
    {
        if (notice.type == engine::EngineNotice::Type::GuardTripped)
            showStatus("Safety guard tripped: a non-finite sample was caught and the engine reset.", true);
        if (notice.type == engine::EngineNotice::Type::CaptureSceneRequest)
            perform->captureScene();
        catcher.handle(notice);
        session.handleNotice(notice);
    }

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
    sessionButton.setBounds(top.removeFromLeft(96).reduced(3));
    settingsButton.setBounds(top.removeFromLeft(130).reduced(3));
    fadeInButton.setBounds(top.removeFromLeft(96).reduced(3));
    fadeOutButton.setBounds(top.removeFromLeft(96).reduced(3));
    panicButton.setBounds(top.removeFromRight(110).reduced(3));
    top.removeFromRight(10);
    catchButton.setBounds(top.removeFromRight(110).reduced(3));
    top.removeFromRight(10);
    meterArea = top.removeFromRight(200).reduced(0, 8);

    statusLabel.setBounds(area.removeFromTop(24));
    tabs.setBounds(area);
}

} // namespace tf::app
