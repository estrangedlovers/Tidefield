#include "Pages.h"

#include "Formatters.h"
#include "Theme.h"

#include <io/AudioFileIO.h>

namespace tf::app {

using engine::P;

// --- Perform -------------------------------------------------------------------------

PerformPage::PerformPage(engine::Engine& e, engine::SceneManager& s) : engine(e), scenes(s), pad(e, s), controls(e)
{
    addAndMakeVisible(pad);
    addAndMakeVisible(captureButton);
    addAndMakeVisible(releaseButton);
    controlsView.setViewedComponent(&controls, false);
    controlsView.setScrollBarsShown(true, false);
    addAndMakeVisible(controlsView);
    captureButton.setTooltip("C: store what you hear now as a scene at the cursor");
    releaseButton.setTooltip("R: hand every knob you have touched back to the terrain");
    captureButton.onClick = [this] { captureScene(); };
    releaseButton.onClick = [this] { releaseLive(); };

    controls.addSection("Master", { P::MasterLevel, P::MasterFadeSecs, P::BusALevel, P::BusBLevel });
    controls.addSection("Terrain", { P::TerrainGlide, P::TerrainFocus, P::TerrainWander, P::TerrainWanderRate, P::TerrainWanderStyle });
    controls.addSection("Tide and key", { P::TideRate, P::HarmonyRoot, P::HarmonyScale, P::HarmonyGravity, P::HarmonyMorph });
    controls.addSection("Medium", { P::MediumType, P::MediumAge, P::MediumNoise, P::MediumWobble, P::MediumDrive, P::MediumMix });
    controls.addSection("Bloom keyboard", { P::BloomTransform, P::BloomAmount, P::BloomLength, P::BloomPitch, P::BloomLevel });
    controls.addSection("Catch", { P::CatchSeconds, P::CatchSource, P::CatchTarget });
    controls.findKnob(P::BloomTransform)->setDisplay("Transform", format::bloomTransform);
    controls.findKnob(P::BloomLevel)->setDisplay("Level", [](double v) { return juce::String(v, 1) + " dB"; });
    controls.findKnob(P::CatchSource)->setDisplay("Source", format::catchSource);
    controls.findKnob(P::CatchTarget)->setDisplay("Into", format::catchTarget);

    keyboard.setAvailableRange(24, 108);
    keyboard.setLowestVisibleKey(48);
    keyboard.setKeyWidth(22.0f);
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::live.withAlpha(0.6f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent.withAlpha(0.3f));
    keyboard.setWantsKeyboardFocus(false);
    keyboardState.addListener(this);
    addAndMakeVisible(keyboard);

    controls.findKnob(P::TerrainWanderStyle)->setDisplay("Wander Style", format::wanderStyle);
    controls.findKnob(P::HarmonyRoot)->setDisplay("Key", format::note);
    controls.findKnob(P::HarmonyScale)->setDisplay("Scale", format::scale);
    controls.findKnob(P::MediumType)->setDisplay("Medium", format::medium);
}

PerformPage::~PerformPage() { keyboardState.removeListener(this); }

void PerformPage::handleNoteOn(juce::MidiKeyboardState*, int, int note, float velocity) { engine.noteOn(note, velocity); }

void PerformPage::handleNoteOff(juce::MidiKeyboardState*, int, int note, float) { engine.noteOff(note); }

void PerformPage::update(const engine::TelemetryFrame& frame)
{
    last = frame;
    pad.update(frame);
    controls.update(frame);
    const bool anyLive = std::any_of(frame.live.begin(), frame.live.end(), [](auto v) { return v != 0; });
    releaseButton.setEnabled(anyLive);
    releaseButton.setColour(juce::TextButton::buttonColourId, anyLive ? theme::live.darker(0.5f) : theme::button);
    captureButton.setEnabled(! scenes.isFull());
}

void PerformPage::resized()
{
    auto b = getLocalBounds();
    keyboard.setBounds(b.removeFromBottom(72).reduced(0, 4));
    b.removeFromBottom(6);
    auto left = b.removeFromLeft(std::min(b.getHeight() - 40, b.getWidth() / 2));
    auto buttons = left.removeFromBottom(36);
    pad.setBounds(left.reduced(0, 4));
    captureButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(4));
    releaseButton.setBounds(buttons.reduced(4));
    b.removeFromLeft(12);
    controlsView.setBounds(b);
    const int w = controlsView.getMaximumVisibleWidth();
    controls.setSize(w, controls.layout(w, false));
}

// --- Sources -------------------------------------------------------------------------

SourcesPage::SourcesPage(engine::Engine& e) : ScrollingPanel(e), engine(e)
{
    panel.addSection("Drone: tone", { P::DroneRoot, P::DroneCutoff, P::DroneResonance, P::DroneDetune, P::DroneShape, P::DroneNoise, P::DroneGravity });
    panel.addSection("Drone: motion", { P::DroneDensity, P::DroneEvolve, P::DroneDriftDepth, P::DroneDriftRate, P::DroneSpread });
    panel.findKnob(P::DroneRoot)->setDisplay("Root", format::midiNote);

    for (int k = 0; k < engine::kNumClouds; ++k)
    {
        const P first = engine::kCloudFirstParam[static_cast<std::size_t>(k)];
        auto at = [&](int o) { return static_cast<P>(engine::idx(first) + o); };
        const int s = panel.addSection("Cloud " + juce::String(k + 1),
                                       { at(0), at(1), at(2), at(3), at(4), at(5), at(6), at(7), at(8), at(9), at(10), at(11) });
        auto& b = loadButtons[static_cast<std::size_t>(k)];
        b.setButtonText("Load sample...");
        b.onClick = [this, k] { chooseSample(k); };
        auto& l = sampleNames[static_cast<std::size_t>(k)];
        l.setText("empty", juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, theme::textDim);
        l.setJustificationType(juce::Justification::centredRight);
        panel.addHeaderComponent(s, b, 120);
        panel.addHeaderComponent(s, l, 220);
    }

    const int bloomSection = panel.addSection("Bloom", { P::BloomTransform, P::BloomAmount, P::BloomLength, P::BloomAttack, P::BloomRelease,
                                                        P::BloomRoot, P::BloomPitch, P::BloomTone, P::BloomSpread, P::BloomRandom,
                                                        P::BloomPosition, P::BloomGravity });
    panel.findKnob(P::BloomTransform)->setDisplay("Transform", format::bloomTransform);
    panel.findKnob(P::BloomRoot)->setDisplay("Sample Root", format::midiNote);
    {
        auto& b = loadButtons[engine::kNumClouds];
        b.setButtonText("Load one-shot...");
        b.onClick = [this] { chooseSample(engine::kNumClouds); };
        auto& l = sampleNames[engine::kNumClouds];
        l.setText("glass (built in)", juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, theme::textDim);
        l.setJustificationType(juce::Justification::centredRight);
        panel.addHeaderComponent(bloomSection, b, 130);
        panel.addHeaderComponent(bloomSection, l, 220);
    }

    panel.addSection("Resonator", { P::ResRoot, P::ResModes, P::ResStructure, P::ResDecay, P::ResBrightness, P::ResSpread, P::ResGravity });
    panel.findKnob(P::ResRoot)->setDisplay("Root", format::midiNote);
    panel.addSection("Resonator: excitation", { P::ResRain, P::ResRainColour, P::ResExciteInput, P::ResExciteDrone, P::ResExciteClouds, P::ResExciteBloom });
    panel.addSection("Live input", { P::InputArmed, P::InputChannel, P::InputGain, P::InputHighPass, P::InputGate });
    panel.findKnob(P::InputArmed)->setDisplay("Monitor", format::onOff);
    panel.findKnob(P::InputChannel)->setDisplay("Channel", format::inputChannel);
}

void SourcesPage::chooseSample(int cloud)
{
    const bool isBloom = cloud == engine::kNumClouds;
    chooser = std::make_unique<juce::FileChooser>(isBloom ? juce::String("Load a one-shot into Bloom")
                                                          : "Load a sample into Cloud " + juce::String(cloud + 1),
                                                  juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<SourcesPage> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe, cloud](const juce::FileChooser& fc) {
                             const auto file = fc.getResult();
                             if (safe == nullptr || file == juce::File())
                                 return;
                             // Decode off the message thread, publish back on it (the engine's
                             // snapshot channels have a single producer: the message thread).
                             std::thread([safe, cloud, file] {
                                 juce::String error;
                                 std::shared_ptr<dsp::SampleBuffer> buffer(io::loadSample(file, error).release());
                                 juce::MessageManager::callAsync([safe, cloud, buffer, error] {
                                     if (safe == nullptr)
                                         return;
                                     if (buffer == nullptr)
                                     {
                                         juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Could not load sample", error);
                                         return;
                                     }
                                     safe->sampleNames[static_cast<std::size_t>(cloud)].setText(buffer->name, juce::dontSendNotification);
                                     if (cloud == engine::kNumClouds)
                                         safe->engine.loadBloomSample(buffer);
                                     else
                                         safe->engine.loadCloudSample(cloud, buffer);
                                 });
                             }).detach();
                         });
}

void SourcesPage::update(const engine::TelemetryFrame& frame)
{
    ScrollingPanel::update(frame);
    // Names follow whatever is loaded (file, Catch, session recall).
    for (int k = 0; k < engine::kNumClouds; ++k)
    {
        const auto sample = engine.getCloudSample(k);
        sampleNames[static_cast<std::size_t>(k)].setText(sample != nullptr ? juce::String(sample->name) : juce::String("empty"),
                                                         juce::dontSendNotification);
    }
    const auto bloom = engine.getBloomSample();
    sampleNames[engine::kNumClouds].setText(bloom != nullptr ? juce::String(bloom->name) : juce::String("empty"), juce::dontSendNotification);
}

// --- Mixer ---------------------------------------------------------------------------

void MixerPage::Meter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(theme::panelRaised);
    g.fillRoundedRectangle(b, 3.0f);
    auto bar = [&](juce::Rectangle<float> r, float peak) {
        const float db = juce::Decibels::gainToDecibels(peak, -60.0f);
        const float w = juce::jmap(db, -60.0f, 0.0f, 0.0f, r.getWidth());
        g.setColour(db > -3.0f ? theme::warn : theme::accent);
        g.fillRoundedRectangle(r.withWidth(w), 2.0f);
    };
    bar(b.removeFromTop(b.getHeight() / 2).reduced(1.0f), l);
    bar(b.reduced(1.0f), r);
}

MixerPage::MixerPage(engine::Engine& e) : ScrollingPanel(e)
{
    for (int s = 0; s < engine::kNumStrips; ++s)
    {
        const auto& info = engine::kStrips[static_cast<std::size_t>(s)];
        const int section = panel.addSection(info.name, { info.level, info.pan, info.width, info.sendA, info.sendB });
        panel.addHeaderComponent(section, meters[static_cast<std::size_t>(s)], 180);
    }
    panel.addSection("Returns", { P::BusALevel, P::BusBLevel, P::MasterLevel });
}

void MixerPage::update(const engine::TelemetryFrame& frame)
{
    ScrollingPanel::update(frame);
    for (int s = 0; s < engine::kNumStrips; ++s)
    {
        auto& m = meters[static_cast<std::size_t>(s)];
        m.l = std::max(frame.stripPeakL[static_cast<std::size_t>(s)], m.l * 0.85f);
        m.r = std::max(frame.stripPeakR[static_cast<std::size_t>(s)], m.r * 0.85f);
        m.repaint();
    }
}

// --- FX ------------------------------------------------------------------------------

FxPage::FxPage(engine::Engine& e, engine::FxManager& f) : ScrollingPanel(e), fx(f)
{
    const auto& factory = dsp::ProcessorFactory::instance();
    // Buses and master first: they are where most of the sound is shaped.
    std::vector<int> order;
    for (int slot = engine::kBusASlot; slot < engine::kNumFxSlots; ++slot)
        order.push_back(slot);
    for (int slot = 0; slot < engine::kBusASlot; ++slot)
        order.push_back(slot);

    for (int slot : order)
    {
        const auto& info = engine::kFxSlots[static_cast<std::size_t>(slot)];
        const auto first = engine::idx(info.firstParam);
        auto at = [&](int o) { return static_cast<P>(first + o); };
        const int section = panel.addSection(info.name, { at(0), at(1), at(2), at(3), at(4), at(5), at(6) });

        auto& menu = typeMenus[static_cast<std::size_t>(slot)];
        menu.addItem("Empty", 1);
        int id = 2;
        for (const auto& entry : factory.entries())
            menu.addItem(entry.info->name, id++);
        menu.onChange = [this, slot, &menu] {
            const int index = menu.getSelectedId() - 2;
            const auto& list = dsp::ProcessorFactory::instance().entries();
            fx.setType(slot, index >= 0 && index < static_cast<int>(list.size()) ? list[static_cast<std::size_t>(index)].info->typeId : "");
            refreshSlot(slot);
        };
        panel.addHeaderComponent(section, menu, 180);
        refreshSlot(slot);
    }
}

void FxPage::refreshSlot(int slot)
{
    const auto* info = fx.getInfo(slot);
    auto& menu = typeMenus[static_cast<std::size_t>(slot)];
    int selected = 1;
    const auto& list = dsp::ProcessorFactory::instance().entries();
    for (std::size_t i = 0; i < list.size(); ++i)
        if (info == list[i].info)
            selected = static_cast<int>(i) + 2;
    menu.setSelectedId(selected, juce::dontSendNotification);

    const auto first = engine::idx(engine::kFxSlots[static_cast<std::size_t>(slot)].firstParam);
    for (int c = 0; c < 6; ++c)
    {
        auto* knob = panel.findKnob(static_cast<P>(first + c));
        knob->setVisible(info != nullptr);
        if (info == nullptr)
            continue;
        const auto& control = info->controls[static_cast<std::size_t>(c)];
        auto fmt = control.format;
        knob->setDisplay(control.name, [fmt](double v) {
            char text[32] {};
            if (fmt != nullptr)
                fmt(static_cast<float>(v), text, sizeof(text));
            return juce::String(text);
        });
        knob->setVisible(std::string_view(control.name) != "-");
    }
    panel.findKnob(static_cast<P>(first + 6))->setVisible(info != nullptr);
    resized();
}

void FxPage::update(const engine::TelemetryFrame& frame) { ScrollingPanel::update(frame); }

} // namespace tf::app
