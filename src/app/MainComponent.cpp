#include "MainComponent.h"

namespace tf::app {

namespace {

const juce::Colour kBackground { 0xff12151a };
const juce::Colour kPanel { 0xff1b2028 };
const juce::Colour kText { 0xffc9d1d9 };
const juce::Colour kAccent { 0xff7fb4c9 };
const juce::Colour kLive { 0xffd9c38f };
const juce::Colour kWarn { 0xffc97f7f };
const juce::Colour kButton { 0xff2a313c };

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

MainComponent::MainComponent(AudioHost& h) : host(h), engine(h.getEngine()), scenes(h.getEngine())
{
    for (auto* b : { &settingsButton, &fadeInButton, &fadeOutButton, &panicButton, &captureButton, &releaseButton })
    {
        b->setColour(juce::TextButton::buttonColourId, kButton);
        addAndMakeVisible(*b);
    }
    panicButton.setColour(juce::TextButton::buttonColourId, kWarn.darker(0.6f));

    settingsButton.onClick = [this] { showDeviceSettings(); };
    fadeInButton.onClick = [this] { engine.command(engine::Command::FadeIn); };
    fadeOutButton.onClick = [this] { engine.command(engine::Command::FadeOut); };
    panicButton.onClick = [this] { togglePanic(); };
    captureButton.onClick = [this] { scenes.captureScene({}, lastFrame.cursor, lastFrame); };
    releaseButton.onClick = [this] { scenes.releaseLiveLayer(); };

    fadeInButton.setTooltip("Space toggles fade in / fade out");
    panicButton.setTooltip("Esc: fast fade to silence and reset. Press again to resume.");
    captureButton.setTooltip("C: store what you hear now as a scene at the cursor");
    releaseButton.setTooltip("R: hand every knob you have touched back to the terrain");

    wanderStyle.addItemList({ "Drift", "Orbit", "Tide pool" }, 1);
    wanderStyle.setSelectedItemIndex(0, juce::dontSendNotification);
    wanderStyle.setTooltip("Drift: random walk around the cursor. Orbit: slow circles. "
                           "Tide pool: settles into nearby scenes, lingers, drifts on.");
    wanderStyle.onChange = [this] {
        engine.setParam(engine::P::TerrainWanderStyle, static_cast<float>(wanderStyle.getSelectedItemIndex()));
    };
    addAndMakeVisible(wanderStyle);

    statusLabel.setColour(juce::Label::textColourId, kText);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(terrainPad);

    using engine::P;
    addSection("Master", { P::MasterLevel, P::MasterFadeSecs, P::MasterCeiling });
    addSection("Terrain", { P::TerrainGlide, P::TerrainFocus, P::TerrainWander, P::TerrainWanderRate });
    addSection("Drone: tone", { P::DroneLevel, P::DroneRoot, P::DroneCutoff, P::DroneResonance, P::DroneDetune, P::DroneShape, P::DroneNoise });
    addSection("Drone: motion", { P::DroneDensity, P::DroneEvolve, P::DroneDriftDepth, P::DroneDriftRate, P::DroneSpread, P::DroneWidth, P::DronePan });

    setWantsKeyboardFocus(true);
    setSize(1380, 860);
    startTimerHz(30);
}

MainComponent::~MainComponent() { stopTimer(); }

void MainComponent::addSection(const juce::String& title, std::initializer_list<engine::P> params)
{
    Section section;
    section.title = title;
    section.firstControl = controls.size();
    for (auto p : params)
        addParamControl(p);
    section.numControls = controls.size() - section.firstControl;
    sections.push_back(section);
}

void MainComponent::addParamControl(engine::P param)
{
    const auto& spec = engine.getRegistry().spec(param);
    auto c = std::make_unique<ParamControl>();
    c->param = param;

    auto& s = c->slider;
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    s.setRange(spec.minValue, spec.maxValue);
    if (spec.taper == engine::Taper::Log)
        s.setSkewFactorFromMidPoint(std::sqrt(spec.minValue * spec.maxValue));
    s.setValue(spec.defaultValue, juce::dontSendNotification);
    s.setDoubleClickReturnValue(true, spec.defaultValue);
    s.setTextValueSuffix(spec.unit.empty() ? juce::String() : " " + juce::String(spec.unit));
    s.setNumDecimalPlacesToDisplay(spec.maxValue - spec.minValue > 100.0f ? 0 : 2);
    s.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    s.setColour(juce::Slider::textBoxTextColourId, kText);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.onValueChange = [this, param, &s] { engine.setParam(param, static_cast<float>(s.getValue())); };
    if ((spec.flags & engine::ParamFlag::kTerrainBound) != 0)
        s.setTooltip("Follows the terrain. Turning it holds it in the live layer (gold) until released.");

    c->label.setText(spec.name, juce::dontSendNotification);
    c->label.setJustificationType(juce::Justification::centred);
    c->label.setColour(juce::Label::textColourId, kText.withAlpha(0.7f));

    addAndMakeVisible(s);
    addAndMakeVisible(c->label);
    controls.push_back(std::move(c));
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
        toggleFade();
    else if (key == juce::KeyPress::escapeKey)
        togglePanic();
    else if (key.getTextCharacter() == 'c' || key.getTextCharacter() == 'C')
        captureButton.triggerClick();
    else if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        releaseButton.triggerClick();
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

void MainComponent::updateTransportButtons()
{
    using engine::FadeState;
    const auto state = lastFrame.fadeState;
    const bool up = state == FadeState::FadingIn || state == FadeState::Open;
    const bool moving = state == FadeState::FadingIn || state == FadeState::FadingOut;
    const auto active = kAccent.darker(0.4f);
    fadeInButton.setColour(juce::TextButton::buttonColourId, up ? (moving ? active.withAlpha(0.7f) : active) : kButton);
    fadeOutButton.setColour(juce::TextButton::buttonColourId, ! up && moving ? active.withAlpha(0.7f) : kButton);
    panicButton.setButtonText(lastFrame.panicActive ? "Resume" : "PANIC");
    panicButton.setColour(juce::TextButton::buttonColourId, lastFrame.panicActive ? kWarn : kWarn.darker(0.6f));

    const bool anyLive = std::any_of(lastFrame.live.begin(), lastFrame.live.end(), [](auto v) { return v != 0; });
    releaseButton.setEnabled(anyLive);
    releaseButton.setColour(juce::TextButton::buttonColourId, anyLive ? kLive.darker(0.5f) : kButton);
    captureButton.setEnabled(! scenes.isFull());
}

void MainComponent::followTelemetry()
{
    // Knobs follow whatever the terrain is doing, except the one under the mouse.
    for (auto& c : controls)
    {
        const auto i = engine::idx(c->param);
        if (! c->slider.isMouseButtonDown())
            c->slider.setValue(lastFrame.paramTargets[i], juce::dontSendNotification);

        const bool isLive = lastFrame.live[i] != 0;
        if (isLive != c->wasLive)
        {
            c->wasLive = isLive;
            c->slider.setColour(juce::Slider::rotarySliderFillColourId, isLive ? kLive : kAccent);
            c->label.setColour(juce::Label::textColourId, isLive ? kLive : kText.withAlpha(0.7f));
        }
    }

    const int style = static_cast<int>(std::lround(lastFrame.paramTargets[engine::idx(engine::P::TerrainWanderStyle)]));
    if (wanderStyle.getSelectedItemIndex() != style)
        wanderStyle.setSelectedItemIndex(style, juce::dontSendNotification);
}

void MainComponent::showDeviceSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(host.getDeviceManager(), 0, 2, 2, 2, false, false,
                                                                           true, false);
    selector->setSize(520, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = kPanel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::timerCallback()
{
    scenes.tick();

    engine::TelemetryFrame f;
    bool got = false;
    while (engine.popTelemetry(f))
        got = true;
    if (got)
    {
        lastFrame = f;
        followTelemetry();
        terrainPad.update(lastFrame);
    }

    engine::EngineNotice notice;
    while (engine.popNotice(notice))
        if (notice.type == engine::EngineNotice::Type::GuardTripped)
            juce::Logger::writeToLog("Safety guard tripped");

    // Meters fall slowly so the motion stays calm.
    meterL = std::max(lastFrame.peakL, meterL * 0.85f);
    meterR = std::max(lastFrame.peakR, meterR * 0.85f);

    updateTransportButtons();

    // No usable output device (first launch on a machine without a default, or a
    // remembered interface that is unplugged): open settings once instead of sitting silent.
    if (! deviceWarningShown && host.getDeviceManager().getCurrentAudioDevice() == nullptr)
    {
        deviceWarningShown = true;
        showDeviceSettings();
    }

    const int liveCount = static_cast<int>(std::count_if(lastFrame.live.begin(), lastFrame.live.end(), [](auto v) { return v != 0; }));
    statusLabel.setText(juce::String::formatted("CPU %4.1f%%   xruns %d   master %s   limiter %4.1f dB   scenes %d   live %d   guard trips %u",
                                                host.getCpuLoad() * 100.0, host.getXrunCount(),
                                                fadeStateName(lastFrame.fadeState),
                                                juce::Decibels::gainToDecibels(lastFrame.limiterGain),
                                                scenes.size(), liveCount, lastFrame.guardTrips),
                        juce::dontSendNotification);
    repaint(meterArea);
    repaint(voicesArea);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);

    g.setColour(kText);
    g.setFont(juce::FontOptions(22.0f));
    g.drawText("Tidefield", 20, 14, 300, 30, juce::Justification::centredLeft);

    for (const auto& section : sections)
    {
        g.setColour(kPanel);
        g.fillRoundedRectangle(section.bounds.toFloat(), 6.0f);
        g.setColour(kText.withAlpha(0.5f));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(section.title.toUpperCase(), section.bounds.reduced(12, 6).removeFromTop(16), juce::Justification::centredLeft);
    }

    // Peak meters.
    g.setColour(kPanel);
    g.fillRoundedRectangle(meterArea.toFloat(), 4.0f);
    auto drawBar = [&](juce::Rectangle<int> r, float peak) {
        const float db = juce::Decibels::gainToDecibels(peak, -60.0f);
        const float h = juce::jmap(db, -60.0f, 0.0f, 0.0f, static_cast<float>(r.getHeight()));
        g.setColour(db > -1.5f ? kWarn : kAccent);
        g.fillRect(r.toFloat().removeFromBottom(h));
    };
    auto inner = meterArea.reduced(6);
    drawBar(inner.removeFromLeft(inner.getWidth() / 2).reduced(2, 0), meterL);
    drawBar(inner.reduced(2, 0), meterR);

    // Drone voices: one dot per voice, brightness = level, height = interval.
    const auto voices = voicesArea;
    g.setColour(kPanel);
    g.fillRoundedRectangle(voices.toFloat(), 6.0f);
    for (int v = 0; v < 6; ++v)
    {
        const float level = lastFrame.droneVoiceLevel[static_cast<size_t>(v)];
        const float interval = lastFrame.droneVoiceInterval[static_cast<size_t>(v)];
        const float x = static_cast<float>(voices.getX()) + static_cast<float>(voices.getWidth()) * (static_cast<float>(v) + 0.5f) / 6.0f;
        const float y = juce::jmap(interval, -12.0f, 24.0f, static_cast<float>(voices.getBottom()) - 12.0f, static_cast<float>(voices.getY()) + 12.0f);
        const float r = 4.0f + 8.0f * level;
        g.setColour(kAccent.withAlpha(0.15f + 0.85f * level));
        g.fillEllipse(x - r, y - r, 2.0f * r, 2.0f * r);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);
    auto top = area.removeFromTop(40);
    top.removeFromLeft(140);
    settingsButton.setBounds(top.removeFromLeft(130).reduced(4));
    fadeInButton.setBounds(top.removeFromLeft(100).reduced(4));
    fadeOutButton.setBounds(top.removeFromLeft(100).reduced(4));
    panicButton.setBounds(top.removeFromRight(120).reduced(4));

    statusLabel.setBounds(area.removeFromTop(28));
    voicesArea = getLocalBounds().removeFromBottom(90).reduced(20, 10);
    area.removeFromBottom(90);

    // Right column: terrain pad with its buttons underneath, then the meters.
    meterArea = area.removeFromRight(50).reduced(0, 10);
    area.removeFromRight(10);
    auto right = area.removeFromRight(440);
    auto padButtons = right.removeFromBottom(40);
    terrainPad.setBounds(right.reduced(0, 4));
    captureButton.setBounds(padButtons.removeFromLeft(140).reduced(4));
    releaseButton.setBounds(padButtons.removeFromLeft(140).reduced(4));
    wanderStyle.setBounds(padButtons.reduced(4));
    area.removeFromRight(10);

    constexpr int kCellW = 112;
    constexpr int kCellH = 132;
    constexpr int kHeader = 24;
    int y = area.getY();
    for (auto& section : sections)
    {
        const int cols = std::max(1, std::min(static_cast<int>(section.numControls), (area.getWidth() - 16) / kCellW));
        const int rows = (static_cast<int>(section.numControls) + cols - 1) / cols;
        section.bounds = { area.getX(), y, area.getWidth(), kHeader + rows * kCellH + 4 };
        for (std::size_t i = 0; i < section.numControls; ++i)
        {
            const int col = static_cast<int>(i) % cols;
            const int row = static_cast<int>(i) / cols;
            juce::Rectangle<int> cell(area.getX() + 8 + col * kCellW, y + kHeader + row * kCellH, kCellW, kCellH);
            auto& c = *controls[section.firstControl + i];
            c.label.setBounds(cell.removeFromTop(18));
            c.slider.setBounds(cell.reduced(6, 2));
        }
        y = section.bounds.getBottom() + 6;
    }
}

} // namespace tf::app
