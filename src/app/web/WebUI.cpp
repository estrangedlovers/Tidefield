#include "WebUI.h"

#include <io/AudioFileIO.h>
#include <io/Session.h>

#include <thread>

namespace tf::app {

namespace {

const char* mimeFor(const juce::String& path)
{
    const auto ext = path.fromLastOccurrenceOf(".", false, false).toLowerCase();
    if (ext == "html") return "text/html";
    if (ext == "js" || ext == "mjs") return "text/javascript";
    if (ext == "css") return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "ttf") return "font/ttf";
    return "application/octet-stream";
}

engine::Command commandFor(const juce::String& name, const engine::TelemetryFrame& f, bool& ok)
{
    ok = true;
    if (name == "fadeIn") return engine::Command::FadeIn;
    if (name == "fadeOut") return engine::Command::FadeOut;
    if (name == "fadeToggle")
        return f.fadeState == engine::FadeState::Silent || f.fadeState == engine::FadeState::FadingOut ? engine::Command::FadeIn
                                                                                                       : engine::Command::FadeOut;
    if (name == "panic") return engine::Command::Panic;
    if (name == "resume") return engine::Command::ResumeFromPanic;
    if (name == "panicToggle") return f.panicActive ? engine::Command::ResumeFromPanic : engine::Command::Panic;
    if (name == "catch") return engine::Command::Catch;
    if (name == "releaseLive") return engine::Command::ReleaseLiveLayer;
    if (name == "resetFeedback") return engine::Command::ResetFeedback;
    if (name == "loopRecord") return engine::Command::LoopRecord;
    if (name == "loopClear") return engine::Command::LoopClear;
    ok = false;
    return engine::Command::None;
}

engine::MidiAction actionFor(const juce::String& name)
{
    if (name == "catch") return engine::MidiAction::Catch;
    if (name == "fadeToggle") return engine::MidiAction::FadeToggle;
    if (name == "panic") return engine::MidiAction::Panic;
    if (name == "releaseLive") return engine::MidiAction::ReleaseLive;
    if (name == "captureScene") return engine::MidiAction::CaptureScene;
    if (name == "recordToggle") return engine::MidiAction::RecordToggle;
    if (name == "loopRecord") return engine::MidiAction::LoopRecord;
    if (name == "loopClear") return engine::MidiAction::LoopClear;
    if (name == "freezeToggle") return engine::MidiAction::FreezeToggle;
    if (name == "inputFreezeToggle") return engine::MidiAction::InputFreezeToggle;
    return engine::MidiAction::None;
}

int argInt(const juce::Array<juce::var>& a, int i, int def = 0) { return i < a.size() ? static_cast<int>(a[i]) : def; }
float argFloat(const juce::Array<juce::var>& a, int i, float def = 0.0f) { return i < a.size() ? static_cast<float>(static_cast<double>(a[i])) : def; }

} // namespace

juce::String WebUI::devServerUrl() { return juce::SystemStats::getEnvironmentVariable("TIDEFIELD_UI_DEV", {}); }

std::optional<juce::File> WebUI::findUiRoot()
{
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const juce::File candidates[] = {
        exe.getParentDirectory().getParentDirectory().getChildFile("Resources/ui"), // macOS bundle
        exe.getParentDirectory().getChildFile("ui"),                                 // next to the binary
#ifdef TIDEFIELD_UI_SOURCE_DIST
        juce::File(TIDEFIELD_UI_SOURCE_DIST),                                        // development fallback
#endif
    };
    for (const auto& c : candidates)
        if (c.getChildFile("index.html").existsAsFile())
            return c;
    return std::nullopt;
}

WebUI::WebUI(AppCore& c, juce::File uiRoot) : core(c), root(std::move(uiRoot))
{
    auto options = juce::WebBrowserComponent::Options {}
                       .withNativeIntegrationEnabled()
                       .withKeepPageLoadedWhenBrowserIsHidden()
                       .withNativeFunction("tidefield",
                                           [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done) {
                                               if (args.isEmpty())
                                               {
                                                   done({});
                                                   return;
                                               }
                                               juce::Array<juce::var> rest;
                                               for (int i = 1; i < args.size(); ++i)
                                                   rest.add(args[i]);
                                               done(call(args[0].toString(), rest));
                                           })
                       .withResourceProvider([this](const juce::String& path) { return serve(path); });
#if JUCE_MAC
    options = options.withAppleWkWebViewOptions(juce::WebBrowserComponent::Options::AppleWkWebView {}.withDisabledAcceptsFirstMouse());
#endif

    browser = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*browser);

    const auto dev = devServerUrl();
    browser->goToURL(dev.isNotEmpty() ? dev : juce::WebBrowserComponent::getResourceProviderRoot());

    core.onTelemetry = [this](const engine::TelemetryFrame& f) {
        if (pageReady)
            emit("telemetry", encoder.encode(f));
    };
    core.onMidiActivity = [this](const engine::RawMidi& m) {
        auto* o = new juce::DynamicObject();
        o->setProperty("status", static_cast<int>(m.status));
        o->setProperty("d1", static_cast<int>(m.data1));
        o->setProperty("d2", static_cast<int>(m.data2));
        o->setProperty("port", static_cast<int>(m.port));
        if (pageReady)
            emit("midiActivity", juce::var(o));
    };
    core.onStatus = [this](const juce::String& message, bool warning) {
        auto* o = new juce::DynamicObject();
        o->setProperty("message", message);
        o->setProperty("warning", warning);
        if (pageReady)
            emit("status", juce::var(o));
    };
    core.onSessionChanged = [this] {
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName("Tidefield - " + core.session.getName());
    };
    core.midiInputs.onDevicesChanged = [this] { lastMidi.clear(); };

    setSize(1440, 900);
    startTimerHz(4);
}

WebUI::~WebUI()
{
    stopTimer();
    core.onTelemetry = nullptr;
    core.onMidiActivity = nullptr;
    core.onStatus = nullptr;
    core.onSessionChanged = nullptr;
    core.midiInputs.onDevicesChanged = nullptr;
}

void WebUI::resized() { browser->setBounds(getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> WebUI::serve(const juce::String& path) const
{
    auto relative = path.trimCharactersAtStart("/").upToFirstOccurrenceOf("?", false, false);
    if (relative.isEmpty())
        relative = "index.html";
    const auto file = root.getChildFile(relative);
    if (! file.isAChildOf(root) || ! file.existsAsFile())
        return std::nullopt;
    juce::MemoryBlock data;
    file.loadFileAsData(data);
    juce::WebBrowserComponent::Resource r;
    r.data.assign(static_cast<const std::byte*>(data.getData()), static_cast<const std::byte*>(data.getData()) + data.getSize());
    r.mimeType = mimeFor(relative);
    return r;
}

void WebUI::emit(const juce::String& event, const juce::var& value) { browser->emitEventIfBrowserIsVisible(juce::Identifier(event), value); }

void WebUI::pushIfChanged(const juce::String& event, const juce::var& value, juce::String& last)
{
    const auto text = juce::JSON::toString(value, true);
    if (text == last)
        return;
    last = text;
    emit(event, value);
}

juce::var WebUI::describeMidi() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> bindings;
    for (const auto& b : core.midi.getBindings())
    {
        auto* bo = new juce::DynamicObject();
        bo->setProperty("text", juce::String(core.midi.describe(b)));
        bo->setProperty("param", b.action == engine::MidiAction::None ? juce::var(static_cast<int>(b.param)) : juce::var());
        bindings.add(juce::var(bo));
    }
    o->setProperty("bindings", bindings);
    o->setProperty("learning", core.midi.isLearning());
    const auto lp = core.midi.getLearnParam();
    o->setProperty("learnParam", lp ? juce::var(static_cast<int>(*lp)) : juce::var());
    o->setProperty("noteChannel", core.midi.getNoteChannel());
    o->setProperty("notesToDrone", core.midi.getNotesToDrone());
    juce::Array<juce::var> devices;
    for (const auto& d : core.midiInputs.getDevices())
    {
        auto* dobj = new juce::DynamicObject();
        dobj->setProperty("id", d.info.identifier);
        dobj->setProperty("name", d.info.name);
        dobj->setProperty("enabled", d.enabled);
        dobj->setProperty("open", d.open);
        devices.add(juce::var(dobj));
    }
    o->setProperty("devices", devices);
    return juce::var(o);
}

juce::var WebUI::describeSession() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("name", core.session.getName());
    o->setProperty("busy", core.session.isBusy());
    auto* device = core.host.getDeviceManager().getCurrentAudioDevice();
    o->setProperty("device", device != nullptr ? device->getName() : juce::String());
    o->setProperty("sampleRate", device != nullptr ? device->getCurrentSampleRate() : 0.0);
    o->setProperty("blockSize", device != nullptr ? device->getCurrentBufferSizeSamples() : 0);
    o->setProperty("cpu", core.host.getCpuLoad());
    o->setProperty("xruns", core.host.getXrunCount());
    return juce::var(o);
}

juce::var WebUI::describeRecording() const
{
    const auto st = core.recorder.getStatus();
    auto* o = new juce::DynamicObject();
    o->setProperty("state", st.state == io::Recorder::State::Recording   ? "recording"
                            : st.state == io::Recorder::State::Finishing ? "finishing"
                                                                         : "idle");
    o->setProperty("seconds", std::floor(st.seconds));
    o->setProperty("stems", st.state == io::Recorder::State::Idle ? core.getRecordStems() : st.stems);
    o->setProperty("dropped", static_cast<double>(st.droppedFrames));
    o->setProperty("folder", core.getRecordingsFolder().getFullPathName());
    o->setProperty("last", st.folder.getFileName());
    return juce::var(o);
}

void WebUI::chooseRecordingsFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Where should recordings go?", core.getRecordingsFolder());
    juce::Component::SafePointer<WebUI> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [safe](const juce::FileChooser& fc) {
                             if (safe != nullptr && fc.getResult() != juce::File())
                                 safe->core.setRecordingsFolder(fc.getResult());
                         });
}

juce::var WebUI::hello()
{
    pageReady = true;
    encoder.reset();
    lastScenes = lastFx = lastSamples = lastMidi = lastSession = lastRecord = {};
    auto* o = new juce::DynamicObject();
    o->setProperty("schema", io::buildSchema(core.engine));
    o->setProperty("version", JUCE_APPLICATION_VERSION_STRING);
    return juce::var(o);
}

void WebUI::timerCallback()
{
    if (! pageReady)
        return;
    pushIfChanged("scenes", io::describeScenes(core.scenes), lastScenes);
    pushIfChanged("fx", io::describeFx(core.fx), lastFx);
    pushIfChanged("samples", io::describeSamples(core.engine), lastSamples);
    pushIfChanged("midi", describeMidi(), lastMidi);
    pushIfChanged("session", describeSession(), lastSession);
    pushIfChanged("record", describeRecording(), lastRecord);
}

void WebUI::chooseSample(int slot)
{
    const bool isBloom = slot == engine::kNumClouds;
    chooser = std::make_unique<juce::FileChooser>(isBloom ? juce::String("Load a one-shot into Bloom") : "Load a sample into Cloud " + juce::String(slot + 1),
                                                  juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<WebUI> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, slot](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (safe == nullptr || file == juce::File())
            return;
        std::thread([safe, slot, file] {
            juce::String error;
            std::shared_ptr<dsp::SampleBuffer> buffer(io::loadSample(file, error).release());
            juce::MessageManager::callAsync([safe, slot, buffer, error] {
                if (safe == nullptr)
                    return;
                if (buffer == nullptr)
                {
                    safe->core.status(error, true);
                    return;
                }
                if (slot == engine::kNumClouds)
                    safe->core.engine.loadBloomSample(buffer);
                else
                    safe->core.engine.loadCloudSample(slot, buffer);
                safe->core.status("Loaded " + juce::String(buffer->name));
            });
        }).detach();
    });
}

void WebUI::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(core.host.getDeviceManager(), 0, 2, 2, 2, false, false, true, false);
    selector->setSize(520, 420);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = juce::Colour(0xff161a20);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

juce::var WebUI::call(const juce::String& method, const juce::Array<juce::var>& a)
{
    auto& engine = core.engine;
    const auto paramIndex = [&](int i) -> std::optional<engine::ParamIndex> {
        const int p = argInt(a, i, -1);
        if (p < 0 || p >= static_cast<int>(engine::kNumParams))
            return std::nullopt;
        return static_cast<engine::ParamIndex>(p);
    };

    if (method == "hello")
        return hello();
    if (method == "setParam")
    {
        if (const auto p = paramIndex(0))
            engine.post(engine::ControlEvent::setParam(*p, argFloat(a, 1)));
        return {};
    }
    if (method == "releaseParam")
    {
        if (const auto p = paramIndex(0))
            engine.post(engine::ControlEvent::releaseParam(*p));
        return {};
    }
    if (method == "command")
    {
        bool ok = false;
        const auto c = commandFor(a[0].toString(), core.latest(), ok);
        if (ok)
            engine.command(c);
        return ok;
    }
    if (method == "noteOn")
        return engine.noteOn(argInt(a, 0), argFloat(a, 1, 0.8f));
    if (method == "noteOff")
        return engine.noteOff(argInt(a, 0));

    // Scenes.
    if (method == "scene.capture")
    {
        const int i = core.scenes.captureScene({}, { argFloat(a, 0, core.latest().cursor.x), argFloat(a, 1, core.latest().cursor.y) }, core.latest());
        if (i < 0)
            core.status("The terrain is full (32 scenes). Remove one to capture another.", true);
        return i;
    }
    if (method == "scene.move")
        core.scenes.moveScene(argInt(a, 0), { argFloat(a, 1), argFloat(a, 2) });
    else if (method == "scene.remove")
        core.scenes.removeScene(argInt(a, 0));
    else if (method == "scene.rename")
        core.scenes.renameScene(argInt(a, 0), a[1].toString().toStdString());
    else if (method == "scene.commit")
        core.scenes.commitLiveLayer(argInt(a, 0), core.latest());
    else if (method == "scene.replace")
    {
        const int i = argInt(a, 0);
        if (i >= 0 && i < core.scenes.size())
        {
            const auto old = core.scenes.getScenes()[static_cast<std::size_t>(i)];
            core.scenes.removeScene(i);
            core.scenes.captureScene(old.name, old.position, core.latest());
        }
    }
    else if (method == "scene.releaseLive")
        core.scenes.releaseLiveLayer();
    // FX.
    else if (method == "fx.setType")
        core.fx.setType(argInt(a, 0), a[1].toString().toStdString());
    // Samples.
    else if (method == "sample.load")
        chooseSample(argInt(a, 0));
    else if (method == "sample.peaks")
    {
        // A sample's outline for the waveform view: max |x| per bucket.
        const int slot = argInt(a, 0);
        const int buckets = juce::jlimit(16, 4000, argInt(a, 1, 400));
        const auto buffer = slot == engine::kNumClouds ? engine.getBloomSample() : engine.getCloudSample(slot);
        juce::Array<juce::var> peaks;
        if (buffer != nullptr && buffer->size() > 0)
        {
            const auto n = buffer->size();
            float overall = 1.0e-6f;
            std::vector<float> values(static_cast<std::size_t>(buckets), 0.0f);
            for (int b = 0; b < buckets; ++b)
            {
                const auto from = n * static_cast<std::size_t>(b) / static_cast<std::size_t>(buckets);
                const auto to = std::max(from + 1, n * static_cast<std::size_t>(b + 1) / static_cast<std::size_t>(buckets));
                float peak = 0.0f;
                for (auto i = from; i < to && i < n; ++i)
                {
                    peak = std::max(peak, std::fabs(buffer->left[i]));
                    if (buffer->isStereo())
                        peak = std::max(peak, std::fabs(buffer->right[i]));
                }
                values[static_cast<std::size_t>(b)] = peak;
                overall = std::max(overall, peak);
            }
            for (float v : values)
                peaks.add(std::round(v / overall * 1000.0f) / 1000.0f);
        }
        return peaks;
    }
    else if (method == "sample.clear")
    {
        const int slot = argInt(a, 0);
        slot == engine::kNumClouds ? engine.loadBloomSample(nullptr) : engine.loadCloudSample(slot, nullptr);
    }
    // Session.
    else if (method == "session.new")
        core.session.newSession();
    else if (method == "session.open")
        core.session.open();
    else if (method == "session.save")
        core.session.save();
    else if (method == "session.saveAs")
        core.session.saveAs();
    // MIDI.
    else if (method == "midi.learnParam")
    {
        if (const auto p = paramIndex(0))
            core.midi.learnParam(*p);
    }
    else if (method == "midi.learnAction")
        core.midi.learnAction(actionFor(a[0].toString()));
    else if (method == "midi.cancelLearn")
        core.midi.cancelLearn();
    else if (method == "midi.clearParam")
    {
        if (const auto p = paramIndex(0))
            core.midi.clearParam(*p);
    }
    else if (method == "midi.remove")
        core.midi.removeBinding(argInt(a, 0));
    else if (method == "midi.defaults")
        core.midi.loadDefaultLayout();
    else if (method == "midi.clearAll")
        core.midi.clearAll();
    else if (method == "midi.setNoteChannel")
        core.midi.setNoteChannel(argInt(a, 0, -1));
    else if (method == "midi.setNotesToDrone")
        core.midi.setNotesToDrone(static_cast<bool>(a[0]));
    else if (method == "midi.setDevice")
    {
        const auto id = a[0].toString();
        const bool enabled = static_cast<bool>(a[1]);
        juce::MessageManager::callAsync([this, id, enabled] { core.midiInputs.setEnabled(id, enabled); });
    }
    // Recording.
    else if (method == "record.toggle")
        core.toggleRecording();
    else if (method == "record.start")
        core.startRecording();
    else if (method == "record.stop")
        core.stopRecording();
    else if (method == "record.setStems")
        core.setRecordStems(static_cast<bool>(a[0]));
    else if (method == "record.chooseFolder")
        chooseRecordingsFolder();
    else if (method == "record.reveal")
    {
        const auto last = core.getLastRecording();
        (last.exists() ? last : core.getRecordingsFolder()).revealToUser();
    }
    // Audio.
    else if (method == "audio.settings")
        showAudioSettings();
    else
        return juce::var("unknown method: " + method);

    lastMidi.clear(); // MIDI and other state may have changed: refresh on the next tick
    lastRecord.clear();
    return {};
}

} // namespace tf::app
