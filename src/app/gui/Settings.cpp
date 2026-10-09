#include "Settings.h"

#include "Controls.h"
#include "MainView.h"
#include "Pages.h"
#include "Places.h"
#include "Style.h"

#include "../LinkSync.h"
#include "../PluginHost.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace tf::app::gui {
namespace {
constexpr int kTabW = 168, kLabelW = 190, kRowGap = 8, kControlMaxW = 440;
constexpr const char* kScaleKey = "uiScale";
constexpr const char* kHelpKey = "hoverHelp";
constexpr const char* kManualUrl = "https://github.com/estrangedlovers/Tidefield/blob/main/docs/MANUAL.md";

const char* tabName(SettingsTab t)
{
    switch (t)
    {
        case SettingsTab::Look: return "Look and Feel";
        case SettingsTab::Audio: return "Audio";
        case SettingsTab::Midi: return "MIDI, Sync and Remote";
        case SettingsTab::Plugins: return "Plug-ins";
        case SettingsTab::Files: return "Files and Startup";
        case SettingsTab::Record: return "Record and Render";
        case SettingsTab::About: return "About";
        default: return "";
    }
}

class FormPage : public juce::Component
{
public:
    void header(const juce::String& text) { rows.push_back({ text, nullptr, 30, Kind::Header }); }

    void note(const juce::String& text, int height = 34) { rows.push_back({ text, nullptr, height, Kind::Note }); }

    template <typename T>
    T* row(const juce::String& label, std::unique_ptr<T> c, int height = 26)
    {
        auto* raw = c.get();
        addAndMakeVisible(*raw);
        rows.push_back({ label, raw, height, Kind::Control });
        owned.push_back(std::move(c));
        return raw;
    }

    template <typename T>
    T* full(std::unique_ptr<T> c, int height)
    {
        auto* raw = c.get();
        addAndMakeVisible(*raw);
        rows.push_back({ {}, raw, height, Kind::Full });
        owned.push_back(std::move(c));
        return raw;
    }

    int preferredHeight() const
    {
        int h = 12;
        for (const auto& r : rows)
            h += r.height + kRowGap;
        return h + 12;
    }

    void resized() override
    {
        int y = 12;
        for (auto& r : rows)
        {
            if (r.c != nullptr)
            {
                const int x = r.kind == Kind::Full ? 16 : kLabelW;
                const int w = getWidth() - x - 16;
                r.c->setBounds(x, y, r.kind == Kind::Full ? w : std::min(w, kControlMaxW), r.height);
            }
            y += r.height + kRowGap;
        }
    }

    void paint(juce::Graphics& g) override
    {
        int y = 12;
        for (const auto& r : rows)
        {
            auto area = juce::Rectangle<int>(16, y, getWidth() - 32, r.height);
            if (r.kind == Kind::Header)
            {
                g.setFont(caps(11.0f));
                g.setColour(colour::textDim());
                g.drawText(r.text.toUpperCase(), area.withTrimmedTop(8), juce::Justification::bottomLeft, false);
                g.setColour(colour::line());
                g.fillRect(area.removeFromBottom(1));
            }
            else if (r.kind == Kind::Note)
            {
                g.setFont(font(11.5f));
                g.setColour(colour::textFaint());
                g.drawFittedText(r.text, area, juce::Justification::topLeft, 4);
            }
            else if (r.kind == Kind::Control)
            {
                g.setFont(font(12.5f));
                g.setColour(colour::text());
                g.drawText(r.text, juce::Rectangle<int>(16, y, kLabelW - 24, std::min(r.height, 26)), juce::Justification::centredLeft, true);
            }
            y += r.height + kRowGap;
        }
    }

private:
    enum class Kind { Header, Note, Control, Full };
    struct Row
    {
        juce::String text;
        juce::Component* c = nullptr;
        int height = 26;
        Kind kind = Kind::Control;
    };
    std::vector<Row> rows;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

class ChoiceRow final : public juce::Component
{
public:
    ChoiceRow(juce::StringArray options, int selected, std::function<void(int)> chosen) : onChosen(std::move(chosen))
    {
        for (int i = 0; i < options.size(); ++i)
        {
            auto b = std::make_unique<FlatButton>(options[i]);
            b->setToggleState(i == selected, juce::dontSendNotification);
            b->onClick = [this, i] { choose(i); };
            addAndMakeVisible(*b);
            buttons.push_back(std::move(b));
        }
    }

    void choose(int i)
    {
        for (std::size_t k = 0; k < buttons.size(); ++k)
            buttons[k]->setToggleState(static_cast<int>(k) == i, juce::dontSendNotification);
        if (onChosen)
            onChosen(i);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int w = std::min(120, (r.getWidth() - 4 * (static_cast<int>(buttons.size()) - 1)) / std::max<int>(1, static_cast<int>(buttons.size())));
        for (auto& b : buttons)
        {
            b->setBounds(r.removeFromLeft(w));
            r.removeFromLeft(4);
        }
    }

private:
    std::vector<std::unique_ptr<FlatButton>> buttons;
    std::function<void(int)> onChosen;
};

std::unique_ptr<FlatButton> toggle(const juce::String& text, bool on, std::function<void(bool)> changed)
{
    auto b = std::make_unique<FlatButton>(text, colour::tide());
    b->setClickingTogglesState(true);
    b->setToggleState(on, juce::dontSendNotification);
    auto* raw = b.get();
    b->onClick = [raw, changed = std::move(changed)] { changed(raw->getToggleState()); };
    return b;
}

std::unique_ptr<FlatButton> button(const juce::String& text, std::function<void()> clicked)
{
    auto b = std::make_unique<FlatButton>(text);
    b->onClick = std::move(clicked);
    return b;
}

class Label final : public juce::Component
{
public:
    explicit Label(std::function<juce::String()> source) : text(std::move(source)) { startRefresh(); }

    void paint(juce::Graphics& g) override
    {
        g.setFont(font(12.0f));
        g.setColour(colour::textDim());
        g.drawFittedText(shown, getLocalBounds(), juce::Justification::centredLeft, 3);
    }

private:
    void startRefresh()
    {
        shown = text();
        juce::Timer::callAfterDelay(500, [safe = juce::Component::SafePointer<Label>(this)] {
            if (safe == nullptr)
                return;
            if (const auto now = safe->text(); now != safe->shown)
            {
                safe->shown = now;
                safe->repaint();
            }
            safe->startRefresh();
        });
    }

    std::function<juce::String()> text;
    juce::String shown;
};

class ThemeGrid final : public juce::Component
{
public:
    explicit ThemeGrid(AppCore& c) : core(c) {}

    static int heightFor(int width)
    {
        const int perRow = std::max(1, width / 148);
        return ((static_cast<int>(kThemes.size()) + perRow - 1) / perRow) * 74;
    }

    void paint(juce::Graphics& g) override
    {
        for (std::size_t i = 0; i < kThemes.size(); ++i)
        {
            const auto t = kThemes[i];
            const auto r = tile(static_cast<int>(i)).toFloat();
            const auto previous = theme();
            setTheme(t);
            const auto ui = palette();
            const auto disp = displayPalette();
            setTheme(previous);
            g.setColour(ui.window);
            g.fillRoundedRectangle(r, 4.0f);
            g.setColour(ui.header);
            g.fillRect(r.withHeight(12.0f).reduced(2.0f, 0.0f).translated(0.0f, 2.0f));
            auto body = r.reduced(6.0f).withTrimmedTop(12.0f).withTrimmedBottom(16.0f);
            g.setColour(ui.panel);
            g.fillRect(body.removeFromLeft(body.getWidth() * 0.45f));
            g.setColour(disp.well);
            g.fillRect(body);
            for (int k = 0; k < 4; ++k)
            {
                g.setColour(disp.scenes[static_cast<std::size_t>(k)]);
                g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({ body.getX() + body.getWidth() * (0.2f + 0.2f * static_cast<float>(k)),
                                                                             body.getCentreY() + (k % 2 == 0 ? -4.0f : 4.0f) }));
            }
            g.setColour(ui.accent);
            g.fillRect(juce::Rectangle<float>(r.getX() + 6.0f, r.getBottom() - 20.0f, 18.0f, 3.0f));
            g.setFont(font(11.0f, 600));
            g.setColour(ui.text);
            g.drawText(themeName(t), r.reduced(6.0f, 2.0f).removeFromBottom(14.0f).withTrimmedLeft(22.0f), juce::Justification::centredLeft, true);
            if (t == theme())
            {
                g.setColour(colour::accent());
                g.drawRoundedRectangle(r.reduced(1.0f), 4.0f, 2.0f);
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        for (std::size_t i = 0; i < kThemes.size(); ++i)
            if (tile(static_cast<int>(i)).contains(e.getPosition()))
            {
                const auto t = kThemes[i];
                later(this, [this, t] { MainView::switchTheme(core, t); });
                return;
            }
    }

private:
    juce::Rectangle<int> tile(int i) const
    {
        const int perRow = std::max(1, getWidth() / 148);
        const int w = (getWidth() - (perRow - 1) * 8) / perRow;
        return { (i % perRow) * (w + 8), (i / perRow) * 74, w, 66 };
    }

    AppCore& core;
};

class FolderList final : public juce::Component
{
public:
    FolderList(PluginHost& h) : host(h)
    {
        add.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Add a plug-in folder", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [safe = juce::Component::SafePointer<FolderList>(this)](const juce::FileChooser& fc) {
                                     if (safe == nullptr || fc.getResult() == juce::File())
                                         return;
                                     auto folders = safe->host.getCustomFolders();
                                     folders.addIfNotAlreadyThere(fc.getResult().getFullPathName());
                                     safe->host.setCustomFolders(folders);
                                     safe->repaint();
                                 });
        };
        addAndMakeVisible(add);
    }

    void resized() override { add.setBounds(getLocalBounds().removeFromBottom(24).removeFromLeft(140)); }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedBottom(30.0f);
        g.setColour(colour::well());
        g.fillRect(r);
        const auto folders = host.getCustomFolders();
        g.setFont(font(11.5f));
        if (folders.isEmpty())
        {
            g.setColour(colour::wellTextDim());
            g.drawText("No extra folders. Add one if your plug-ins live somewhere unusual.", r.reduced(8.0f), juce::Justification::centredLeft, true);
            return;
        }
        for (int i = 0; i < folders.size(); ++i)
        {
            auto line = juce::Rectangle<float>(r.getX(), r.getY() + 4.0f + 20.0f * static_cast<float>(i), r.getWidth(), 20.0f);
            g.setColour(colour::wellText());
            g.drawText(folders[i], line.reduced(8.0f, 0.0f).withTrimmedRight(24.0f), juce::Justification::centredLeft, true);
            g.setColour(colour::wellTextDim());
            g.drawText("x", line.removeFromRight(24.0f), juce::Justification::centred);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const int i = (e.y - 4) / 20;
        auto folders = host.getCustomFolders();
        if (i >= 0 && i < folders.size() && e.x > getWidth() - 28 && e.y < getHeight() - 30)
        {
            folders.remove(i);
            host.setCustomFolders(folders);
            repaint();
        }
    }

private:
    PluginHost& host;
    FlatButton add { "Add folder..." };
    std::unique_ptr<juce::FileChooser> chooser;
};

class PlaceList final : public juce::Component, private juce::Timer
{
public:
    explicit PlaceList(juce::PropertiesFile& s) : settings(s)
    {
        add.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Add a folder to Places", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [safe = juce::Component::SafePointer<PlaceList>(this)](const juce::FileChooser& fc) {
                                     if (safe != nullptr && fc.getResult().isDirectory())
                                         places::add(safe->settings, fc.getResult());
                                 });
        };
        addAndMakeVisible(add);
        startTimerHz(2);
    }

    void resized() override { add.setBounds(getLocalBounds().removeFromBottom(24).removeFromLeft(140)); }

    void paint(juce::Graphics& g) override
    {
        shown = places::version();
        auto r = getLocalBounds().toFloat().withTrimmedBottom(30.0f);
        g.setColour(colour::well());
        g.fillRect(r);
        const auto list = places::get(settings);
        g.setFont(font(11.5f));
        if (list.isEmpty())
        {
            g.setColour(colour::wellTextDim());
            g.drawText("No places yet. Add the folders where you keep your own sounds.", r.reduced(8.0f), juce::Justification::centredLeft, true);
            return;
        }
        const int fits = std::max(1, static_cast<int>((r.getHeight() - 8.0f) / kLineH));
        for (int i = 0; i < list.size() && i < fits; ++i)
        {
            auto line = juce::Rectangle<float>(r.getX(), r.getY() + 4.0f + kLineH * static_cast<float>(i), r.getWidth(), kLineH);
            if (i == fits - 1 && list.size() > fits)
            {
                g.setColour(colour::wellTextDim());
                g.drawText("and " + juce::String(list.size() - fits + 1) + " more in the browser", line.reduced(8.0f, 0.0f), juce::Justification::centredLeft, true);
                break;
            }
            const bool missing = ! juce::File(list[i]).isDirectory();
            g.setColour(missing ? colour::wellTextDim() : colour::wellText());
            const auto text = line.reduced(8.0f, 0.0f).withTrimmedRight(24.0f);
            g.drawText(elideStart(list[i] + (missing ? "  (not found)" : ""), g.getCurrentFont(), text.getWidth()), text, juce::Justification::centredLeft, false);
            g.setColour(i == hover ? colour::lift(colour::wellText(), 0.3f) : colour::wellTextDim());
            g.drawText("x", line.removeFromRight(24.0f), juce::Justification::centred);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int h = e.x > getWidth() - 28 && e.y < getHeight() - 30 ? static_cast<int>((static_cast<float>(e.y) - 4.0f) / kLineH) : -1;
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const int i = static_cast<int>((static_cast<float>(e.y) - 4.0f) / kLineH);
        const auto list = places::get(settings);
        if (i >= 0 && i < list.size() && e.x > getWidth() - 28 && e.y < getHeight() - 30)
            places::remove(settings, list[i]);
    }

private:
    static juce::String elideStart(const juce::String& s, const juce::Font& f, float width)
    {
        if (juce::GlyphArrangement::getStringWidth(f, s) <= width)
            return s;
        const auto dots = juce::String::fromUTF8("\xe2\x80\xa6");
        auto tail = s;
        while (tail.length() > 4 && juce::GlyphArrangement::getStringWidth(f, dots + tail) > width)
            tail = tail.substring(1);
        return dots + tail;
    }

    void timerCallback() override
    {
        if (shown != places::version())
            repaint();
    }

    static constexpr float kLineH = 20.0f;
    juce::PropertiesFile& settings;
    FlatButton add { "Add folder..." };
    std::unique_ptr<juce::FileChooser> chooser;
    int shown = -1;
    int hover = -1;
};

class MidiDeviceList final : public juce::Component, private juce::Timer
{
public:
    explicit MidiDeviceList(AppCore& c) : core(c)
    {
        rebuild();
        startTimer(1000);
    }

    int preferredHeight() const { return std::max(26, static_cast<int>(buttons.size()) * 30); }

    void resized() override
    {
        auto r = getLocalBounds();
        for (auto& b : buttons)
        {
            b->setBounds(r.removeFromTop(26).removeFromLeft(std::min(r.getWidth(), 360)));
            r.removeFromTop(4);
        }
    }

    void paint(juce::Graphics& g) override
    {
        if (! buttons.empty())
            return;
        g.setFont(font(12.0f));
        g.setColour(colour::textFaint());
        g.drawText(core.midiInputs == nullptr ? "MIDI inputs are handled by the DAW." : "No MIDI inputs are connected.", getLocalBounds(),
                   juce::Justification::centredLeft);
    }

private:
    void timerCallback() override
    {
        if (core.midiInputs != nullptr && signature() != shown)
            rebuild();
    }

    juce::String signature() const
    {
        juce::String sig;
        if (core.midiInputs != nullptr)
            for (const auto& d : core.midiInputs->getDevices())
                sig << d.info.identifier << (d.enabled ? "1" : "0");
        return sig;
    }

    void rebuild()
    {
        shown = signature();
        buttons.clear();
        if (core.midiInputs != nullptr)
            for (const auto& d : core.midiInputs->getDevices())
            {
                const auto id = d.info.identifier;
                buttons.push_back(toggle(d.info.name, d.enabled, [this, id](bool on) {
                    if (core.midiInputs != nullptr)
                        core.midiInputs->setEnabled(id, on);
                }));
                addAndMakeVisible(*buttons.back());
            }
        resized();
        repaint();
    }

    AppCore& core;
    std::vector<std::unique_ptr<FlatButton>> buttons;
    juce::String shown;
};

std::unique_ptr<FormPage> lookPage(AppCore& core, int width)
{
    auto page = std::make_unique<FormPage>();
    page->header("Theme");
    page->full(std::make_unique<ThemeGrid>(core), ThemeGrid::heightFor(width - 32));
    page->note("The terrain, meters and other displays stay dark in the light themes so the performance surface reads the same.", 20);
    page->header("Interface");
    if (! core.host.isPlugin())
    {
        const juce::Array<float> scales { 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f };
        int selected = 2;
        for (int i = 0; i < scales.size(); ++i)
            if (std::abs(scales[i] - interfaceScale(core)) < 0.01f)
                selected = i;
        page->row("Zoom", std::make_unique<ChoiceRow>(juce::StringArray { "80%", "90%", "100%", "110%", "125%", "150%" }, selected,
                                                       [&core, scales](int i) { setInterfaceScale(core, scales[i]); }));
    }
    page->row("Hover help", toggle("Explain controls in the status bar", hoverHelpEnabled(core), [&core](bool on) {
                  core.host.getSettings().setValue(kHelpKey, on);
                  core.host.getSettings().saveIfNeeded();
              }));
    return page;
}

std::unique_ptr<FormPage> audioPage(AppCore& core)
{
    auto page = std::make_unique<FormPage>();
    page->header("Audio device");
    if (auto* devices = core.host.getDeviceManager())
    {
        page->full(std::make_unique<juce::AudioDeviceSelectorComponent>(*devices, 0, 2, 2, 8, false, false, false, false), 360);
        page->note("Enable up to eight outputs for the speaker ring modes (Master tab, Space). A buffer of 256 samples is a good start; "
                   "raise it if the CPU figure turns orange or you hear clicks.");
    }
    else
        page->note("Tidefield is running inside a DAW, which owns the audio device and its settings.");
    page->header("Status");
    page->row("Output", std::make_unique<Label>([&core] {
                  const auto out = core.host.describeOutput();
                  return out.isEmpty() ? juce::String("No audio output") : out;
              }));
    page->row("CPU", std::make_unique<Label>([&core] {
                  const auto& f = core.latest();
                  const float load = f.dspLoad > 0.0f ? f.dspLoad : static_cast<float>(core.host.getCpuLoad());
                  return juce::String(juce::roundToInt(load * 100.0f)) + "%" + (f.guardLevel > 0 ? "  (lightening the load, level " + juce::String(f.guardLevel) + ")" : juce::String());
              }));
    return page;
}

std::unique_ptr<FormPage> midiPage(Model& model)
{
    auto& core = model.core;
    auto page = std::make_unique<FormPage>();
    page->header("MIDI inputs");
    auto list = std::make_unique<MidiDeviceList>(core);
    const int h = list->preferredHeight();
    page->row("Listen to", std::move(list), h);
    page->note("Learn knobs, buttons and notes on the MIDI tab: right-click any control and choose MIDI learn.", 20);
    page->header("Tempo source");
    page->row("Follow", std::make_unique<ChoiceRow>(juce::StringArray { "Internal tempo", "MIDI clock" },
                                                    juce::roundToInt(model.value(engine::P::SyncSource)),
                                                    [&model](int i) { model.set(engine::P::SyncSource, static_cast<float>(i)); }));
    if (core.cycleOut != nullptr)
    {
        page->header("Cycles MIDI out");
        page->row("Send to", createCyclesOutView(model), 50);
        page->note("Turn on MIDI Out and choose the channel on the Cycles page. Held notes are released when the Cycles stop, on panic and when a session opens.", 20);
    }
    page->header("Sync and remote control");
    page->full(createRemoteView(model), 230);
    if (! LinkSync::isAvailable())
        page->note("Ableton Link is not in this build. It is published under the GPL, so it is a build option (TIDEFIELD_WITH_LINK).", 20);
    return page;
}

std::unique_ptr<FormPage> pluginsPage(AppCore& core)
{
    auto page = std::make_unique<FormPage>();
    auto* host = core.plugins.get();
    if (host == nullptr)
    {
        page->header("Plug-ins");
        page->note("Hosting other plug-ins is part of the standalone app. Inside a DAW, use the DAW's own effects.");
        return page;
    }
    page->header("Formats");
    for (const auto& format : host->formatNames())
        page->row(format == "AudioUnit" ? juce::String("Audio Units") : format,
                  toggle("Use " + (format == "AudioUnit" ? juce::String("Audio Units") : format) + " effects", host->isFormatEnabled(format),
                         [host, format](bool on) { host->setFormatEnabled(format, on); }));
    page->header("Folders");
    page->row("Standard folders", toggle("Scan the system plug-in folders", host->usesSystemFolders(), [host](bool on) { host->setUseSystemFolders(on); }));
    page->row("Extra folders", std::make_unique<FolderList>(*host), 120);
    page->note("Extra folders are searched for VST3 plug-ins. Audio Units are registered with macOS and are always found in the standard way.", 20);
    page->header("Scan");
    page->row("Status", std::make_unique<Label>([host] {
                  if (host->isScanning())
                      return "Scanning... " + juce::String(juce::roundToInt(host->scanProgress() * 100.0f)) + "%";
                  if (! host->hasScanned())
                      return juce::String("Not scanned yet");
                  return juce::String(host->numEffects()) + " effect plug-ins available";
              }));
    page->row("Rescan", button("Rescan for new plug-ins", [host] { host->startScan(); }));
    page->row("Rescan all", button("Forget the list and rescan everything", [host] { host->clearAndRescan(); }));
    page->header("Skipped plug-ins");
    page->row("Crashed while scanning", std::make_unique<Label>([host] {
                  const auto skipped = host->skippedPlugins();
                  return skipped.isEmpty() ? juce::String("None") : skipped.joinIntoString(", ");
              }), 44);
    page->row("Try again", button("Retry skipped plug-ins", [host] { host->retrySkipped(); }));
    return page;
}

std::unique_ptr<FormPage> filesPage(Model& model)
{
    auto& core = model.core;
    auto page = std::make_unique<FormPage>();
    page->header("Folders");
    page->row("Recordings", std::make_unique<Label>([&core] { return core.getRecordingsFolder().getFullPathName(); }));
    auto chooserHolder = std::make_shared<std::unique_ptr<juce::FileChooser>>();
    page->row("", button("Choose recordings folder...", [&core, chooserHolder] {
                  *chooserHolder = std::make_unique<juce::FileChooser>("Recordings folder", core.getRecordingsFolder());
                  (*chooserHolder)->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                                [&core](const juce::FileChooser& fc) {
                                                    if (fc.getResult() != juce::File())
                                                        core.setRecordingsFolder(fc.getResult());
                                                });
              }));
    page->row("", button("Show recordings", [&core] {
                  core.getRecordingsFolder().createDirectory();
                  core.getRecordingsFolder().revealToUser();
              }));
    page->row("Presets", button("Show presets folder", [&core] {
                  const auto folder = core.presets.getFolder();
                  folder.createDirectory();
                  folder.revealToUser();
              }));
    page->header("Places");
    page->row("Places", std::make_unique<PlaceList>(core.host.getSettings()), 140);
    page->note("Places are your own sound folders. They appear in the browser, where each one opens to list its audio files, subfolders included. "
               "You can also drop a folder from Finder onto the browser.",
               34);
    page->header("Startup");
    page->row("When Tidefield opens", std::make_unique<ChoiceRow>(juce::StringArray { "Starter session", "Last session" }, core.getOpenLastSession() ? 1 : 0,
                                                                   [&core](int i) { core.setOpenLastSession(i == 1); }));
    page->row("Recent sessions", button("Clear the recent list", [&core] {
                  core.clearRecentSessions();
                  core.status("Recent sessions cleared.");
              }));
    if (auto* recovery = core.recovery.get())
    {
        page->header("Autosave");
        page->row("Recovery copy", toggle("Keep a copy to restore after a crash", recovery->isEnabled(), [recovery](bool on) { recovery->setEnabled(on); }));
        juce::StringArray every;
        int chosen = 0;
        for (std::size_t i = 0; i < std::size(Recovery::kIntervals); ++i)
        {
            every.add(juce::String(Recovery::kIntervals[i]) + " min");
            if (Recovery::kIntervals[i] == recovery->getMinutes())
                chosen = static_cast<int>(i);
        }
        page->row("Every", std::make_unique<ChoiceRow>(every, chosen, [recovery](int i) {
                      recovery->setMinutes(Recovery::kIntervals[static_cast<std::size_t>(i)]);
                  }));
        page->row("", std::make_unique<Label>([recovery] { return recovery->describe(); }));
        page->note("The copy is separate from your session file and is removed when Tidefield quits normally.", 20);
    }
    if (auto installation = createInstallationView(model))
    {
        page->header("Installation mode");
        page->row("Unattended running", std::move(installation), 200);
    }
    return page;
}

std::unique_ptr<FormPage> recordPage(AppCore& core)
{
    auto page = std::make_unique<FormPage>();
    page->header("Recording");
    page->row("Stems", toggle("Record every source and return as well as the master", core.getRecordStems(),
                              [&core](bool on) { core.setRecordStems(on); }));
    page->note("Recordings are 32-bit float WAV at the audio device's sample rate, one folder per take.", 20);
    page->header("Rendering");
    const double rate = core.getRenderSampleRate();
    page->row("Render sample rate", std::make_unique<ChoiceRow>(juce::StringArray { "44.1 kHz", "48 kHz", "96 kHz" }, rate == 44100.0 ? 0 : rate == 96000.0 ? 2 : 1,
                                                                 [&core](int i) { core.setRenderSampleRate(i == 0 ? 44100.0 : i == 2 ? 96000.0 : 48000.0); }));
    page->note("Timeline renders and seamless loops use this rate. A render sounds the same every time.", 20);
    return page;
}

std::unique_ptr<FormPage> aboutPage(AppCore& core)
{
    auto page = std::make_unique<FormPage>();
    page->header("Tidefield " + juce::String(TIDEFIELD_VERSION));
    page->note("An instrument for playing ambient music live.", 20);
    page->row("Manual", button("Open the user manual", [] { juce::URL(kManualUrl).launchInDefaultBrowser(); }));
    page->row("Settings file", button("Show settings file", [&core] { core.host.getSettings().getFile().revealToUser(); }));
    page->header("Credits");
    page->note("Built with JUCE. Inter and Quicksand typefaces under the SIL Open Font License. Every factory sound is synthesised by "
               "Tidefield's own tools.",
               48);
    return page;
}

class SettingsView final : public juce::Component, private juce::Timer
{
public:
    SettingsView(MainView& v, Model& m, SettingsTab t) : view(v), model(m), tab(t)
    {
        addAndMakeVisible(viewport);
        viewport.setScrollBarsShown(true, false);
        setSize(820, 600);
        build();
        startTimerHz(4);
    }

    void show(SettingsTab t)
    {
        if (t == tab)
            return;
        tab = t;
        build();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colour::panel());
        auto side = getLocalBounds().removeFromLeft(kTabW);
        g.setColour(colour::window());
        g.fillRect(side);
        g.setColour(colour::line());
        g.fillRect(side.removeFromRight(1));
        for (int i = 0; i < static_cast<int>(SettingsTab::Count); ++i)
        {
            auto r = tabBounds(i);
            const bool on = i == static_cast<int>(tab);
            if (on || i == hover)
            {
                g.setColour(on ? colour::panel() : colour::lift(colour::window(), 0.06f));
                g.fillRect(r);
            }
            if (on)
            {
                g.setColour(colour::accent());
                g.fillRect(r.removeFromLeft(3));
            }
            g.setFont(font(12.5f, on ? 600 : 400));
            g.setColour(on ? colour::text() : colour::textDim());
            g.drawText(tabName(static_cast<SettingsTab>(i)), tabBounds(i).withTrimmedLeft(16), juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        viewport.setBounds(getLocalBounds().withTrimmedLeft(kTabW));
        if (page != nullptr)
            page->setSize(viewport.getWidth() - viewport.getScrollBarThickness(), page->preferredHeight());
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int h = tabAt(e.getPosition());
        if (h != hover)
        {
            hover = h;
            repaint(0, 0, kTabW, getHeight());
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hover = -1;
        repaint(0, 0, kTabW, getHeight());
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (const int t = tabAt(e.getPosition()); t >= 0)
            later(this, [this, t] { show(static_cast<SettingsTab>(t)); });
    }

private:
    juce::Rectangle<int> tabBounds(int i) const { return { 0, 14 + i * 34, kTabW - 1, 32 }; }

    int tabAt(juce::Point<int> p) const
    {
        for (int i = 0; i < static_cast<int>(SettingsTab::Count); ++i)
            if (tabBounds(i).contains(p))
                return i;
        return -1;
    }

    void build()
    {
        shownTheme = theme();
        viewport.setViewedComponent(nullptr, false);
        const int width = getWidth() - kTabW - viewport.getScrollBarThickness();
        switch (tab)
        {
            case SettingsTab::Look: page = lookPage(model.core, width); break;
            case SettingsTab::Audio: page = audioPage(model.core); break;
            case SettingsTab::Midi: page = midiPage(model); break;
            case SettingsTab::Plugins: page = pluginsPage(model.core); break;
            case SettingsTab::Files: page = filesPage(model); break;
            case SettingsTab::Record: page = recordPage(model.core); break;
            default: page = aboutPage(model.core); break;
        }
        viewport.setViewedComponent(page.get(), false);
        resized();
    }

    void timerCallback() override
    {
        if (theme() != shownTheme)
        {
            if (auto* w = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
                w->setBackgroundColour(colour::panel());
            build();
            repaint();
        }
    }

    MainView& view;
    Model& model;
    SettingsTab tab;
    Theme shownTheme = Theme::slate;
    juce::Viewport viewport;
    std::unique_ptr<FormPage> page;
    int hover = -1;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow(MainView& view, Model& model, SettingsTab tab)
        : juce::DocumentWindow("Settings", colour::panel(), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        content = new SettingsView(view, model, tab);
        setContentOwned(content, true);
        setResizable(true, false);
        setResizeLimits(680, 420, 1400, 1200);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override { later(this, [] { closeSettings(); }); }

    SettingsView* content = nullptr;
};

std::unique_ptr<SettingsWindow>& window()
{
    static std::unique_ptr<SettingsWindow> w;
    return w;
}
}

void openSettings(MainView& view, Model& model, SettingsTab tab)
{
    auto& w = window();
    if (w != nullptr)
    {
        w->content->show(tab);
        w->toFront(true);
        return;
    }
    w = std::make_unique<SettingsWindow>(view, model, tab);
}

void closeSettings() { window().reset(); }

bool isSettingsOpen() { return window() != nullptr; }

float interfaceScale(AppCore& core)
{
    const float s = static_cast<float>(core.host.getSettings().getDoubleValue(kScaleKey, 1.0));
    return juce::jlimit(0.8f, 1.5f, s);
}

void setInterfaceScale(AppCore& core, float scale)
{
    if (core.host.isPlugin())
        return;
    scale = juce::jlimit(0.8f, 1.5f, scale);
    core.host.getSettings().setValue(kScaleKey, scale);
    core.host.getSettings().saveIfNeeded();
    juce::Desktop::getInstance().setGlobalScaleFactor(scale);
}

bool hoverHelpEnabled(AppCore& core) { return core.host.getSettings().getBoolValue(kHelpKey, true); }
}
