#include "Lessons.h"

#include "MainView.h"

#include <algorithm>
#include <array>

namespace tf::app::gui {
namespace {
using W = LessonWait;

constexpr LessonPage say(const char* title, const char* text)
{
    LessonPage p;
    p.title = title;
    p.text = text;
    return p;
}

constexpr LessonPage kFirstSound[] = {
    say("Welcome", "Tidefield is an instrument for playing ambient music live. A few sound sources run all the time and you steer them together. "
                   "Each lesson takes a few minutes. Use Next and Back below, or do what a page asks and it moves on by itself."),
    say("Fade in", "Everything starts silent. Press {key:fade} or click Fade in to bring the whole instrument up over a few seconds.")
        .show("button:fade")
        .until(W::FadeIn),
    say("The terrain", "The dark map in the middle is the terrain. Drag across it and the sound glides after your cursor.").show("terrain").until(W::Cursor),
    say("Scenes", "Each marker on the terrain is a scene: a snapshot of how the instrument sounds. Close to one you hear that scene. Between "
                  "several you hear a blend. Click a marker or press 1 to 6 to travel.")
        .show("terrain")
        .until(W::Cursor),
    say("Glide", "Watch the cursor: Tidefield is gliding to another scene for you. Glide sets how long the journey takes, from a blink to half a minute.")
        .show("param:terrain.glide")
        .glide(4),
    say("Keep what you hear", "When you find a sound you like, double-click an empty spot on the terrain or press {key:capture}. It becomes a new scene and "
                              "joins the others in the Browser.")
        .show("browser")
        .until(W::Capture),
    say("Fade out", "Press {key:fade} again to fade out. To silence everything at once, press {key:panic} for Panic and again to resume. "
                    "Hover over anything and the status bar says what it does.")
        .show("status"),
};

constexpr LessonPage kDrone[] = {
    say("The heart of the instrument", "The drone is a stack of up to six slowly drifting voices on one root note. Most pieces are built on it.")
        .on("Drone")
        .show("device:Drone"),
    say("Wave and chord", "Wave picks the oscillator each voice uses. Chord picks which notes the voices stack and wander between. Both change "
                          "smoothly, so try a few chords while it plays.")
        .on("Drone")
        .show("param:drone.chord")
        .until(W::Param, "drone.chord"),
    say("Root and density", "Root Note sets the lowest note and Density how many voices sound at once. Shape changes the character of the wave, "
                            "from bright to pure.")
        .on("Drone")
        .show("param:drone.density"),
    say("Tone", "Brightness opens and closes the filter. Turn it and hear the voices open up. Drive adds colour without getting louder.")
        .on("Drone")
        .show("param:drone.cutoff")
        .until(W::Param, "drone.cutoff"),
    say("Movement", "Evolve sets how often voices move to new notes. Drift Depth and Drift Rate let each voice wander in pitch, tone and "
                    "position. This is what keeps a held chord alive.")
        .on("Drone")
        .show("device:Motion"),
    say("Presets", "Every device has a Presets menu on its title bar. The drone has fifteen, from Low hum to Glass FM bells. A preset sets every "
                   "drone control, so it sounds the same each time.")
        .on("Drone")
        .show("device:Drone"),
    say("Held controls", "A control you touch turns yellow and stops following the terrain. Press {key:release} to hand every held control back, or "
                         "right-click one and choose Release to the terrain.")
        .on("Drone")
        .show("device:Tone"),
};

constexpr LessonPage kClouds[] = {
    say("Open the Clouds tab", "Click the Clouds tab along the bottom.").show("tab:Clouds").until(W::Page, "Clouds"),
    say("Four clouds", "A cloud plays a sound in tiny overlapping grains. There are four, each with its own sound. The dots on a waveform are "
                       "the grains playing right now.")
        .on("Clouds")
        .show("device:Cloud 1"),
    say("Shaping a cloud", "Density sets how thick the cloud is and Grain Size how long each grain lasts. Short grains sparkle and long grains "
                           "smear. Try Density now.")
        .on("Clouds")
        .show("param:cloud1.density")
        .until(W::Param, "cloud1.density"),
    say("The Browser", "The Browser lists ninety factory sounds in five groups. Click one and choose where it plays: a cloud or Bloom. The "
                       "triangle on a row previews it first.")
        .show("browser"),
    say("Search and favourites", "Type in the search box to filter by name or group. Right-click a sound to add it to your favourites, which "
                                 "stay at the top.")
        .show("browser"),
    say("Places", "Places are your own sound folders. Click Add folder in the Browser or drop a folder onto the window. Click a place to list "
                  "every audio file inside it.")
        .show("browser"),
    say("Drag and drop", "Drag a sound from the Browser, Finder or Explorer onto the window. Drop it on a cloud to load that cloud, on Bloom to load "
                         "Bloom or on the terrain to choose.")
        .on("Clouds")
        .show("devices"),
    say("Bloom", "Bloom is a keyboard sampler that turns one-shots into ambient material. Pick a Transform, then press {key:noteMode} and play "
                 "the letter keys like a piano. Press {key:noteMode} again to leave.")
        .on("Bloom")
        .show("param:bloom.transform"),
};

constexpr LessonPage kPerforming[] = {
    say("The pads", "The row under the terrain holds the gestures. Each pad shows its key in the corner and its level as rising water.")
        .show("pads"),
    say("Swell", "Hold {key:swell} for a few seconds, then let go. Everything blooms and ebbs back. Hold {key:hush} to Hush and {key:slow} to slow "
                 "time to a quarter.")
        .show("pad:Swell")
        .until(W::Param, "swell.hold"),
    say("Shape", "Drag the Shape pad. Left darkens and right brightens. Up sends the sound far into the reverb and down pulls it close. "
                 "Double-click to recentre.")
        .show("pad:Shape")
        .until(W::Param, "perform.colour"),
    say("Freeze and hold", "Press {key:freeze} to freeze the last two seconds as a cloud while the rest steps back. Press {key:inputFreeze} "
                            "to hold the live input as an endless pad.")
        .show("pad:Freeze all"),
    say("Cycles", "Press {key:cycles} to start the Cycles: sparse note loops of different lengths that never line up. Their notes always come from the key.")
        .show("pad:Cycles")
        .until(W::Param, "loops.on"),
    say("Catch", "Press {key:catch} and the last seconds you heard become a new cloud that keeps playing. The Input tab sets how long and where it goes.")
        .show("pad:Catch"),
    say("Takes", "Press {key:take} to record your moves, play a little, then press it again to stop. Press it once more and they play back "
                 "on a loop while you do something else.")
        .show("pad:Take")
        .until(W::Take),
    say("Wander and Tide", "Wander lets the sound roam near your cursor by itself. Tide sets the speed of everything that moves. Neither one "
                           "changes the pitch.")
        .show("param:terrain.wander")
        .until(W::Param, "terrain.wander"),
};

constexpr LessonPage kEffects[] = {
    say("Effect slots", "Every source has two effect slots, and so do the reverb bus, the delay bus and the master. Changing an effect while "
                        "you play crossfades.")
        .on("Effects")
        .show("device:Chain"),
    say("Choosing an effect", "Pick a chain on the left, then choose an effect type in a slot. Every slot has six controls and a Mix. The reverb "
                              "bus starts with Cloud Reverb.")
        .on("Effects")
        .show("devices"),
    say("Sends and returns", "Each source sends to the reverb and the delay. The Mixer shows every strip side by side with the two returns.")
        .on("Mixer")
        .show("device:Returns"),
    say("Macros", "A macro is one knob that moves several controls at once, each by its own amount. The starter session has four: Darken, "
                  "Wash, Thicken and Unsettle.")
        .on("Macros")
        .show("device:Macro 1"),
    say("Mapping a macro", "Right-click any knob and choose Map to macro. Here, drag the two dots on a row to set how far that control moves.")
        .on("Macros")
        .show("devices"),
    say("Recorded on", "Recorded on makes everything sound printed to cassette, vinyl or an old sampler. You hear it live and in recordings.")
        .show("param:medium.type")
        .until(W::Param, "medium.type"),
};

constexpr LessonPage kModulation[] = {
    say("Modulation", "Modulation lets one thing move another while you play: a slow LFO breathing a send, or your voice opening the drone's "
                      "filter.")
        .on("Modulation")
        .show("device:Routes"),
    say("Making a route", "Click Add a route, choose a source and the control it moves. Depth right of centre pushes the control up and left "
                          "pushes it down. Right-click any knob for Modulate with.")
        .on("Modulation")
        .show("device:Routes")
        .until(W::Route),
    say("Sources", "There are four LFOs and two randoms, followers for the input and the mix, velocity, the mod wheel and the terrain "
                   "position. The meters show each one moving.")
        .on("Modulation")
        .show("device:LFO 1"),
    say("Seasons", "A season moves one control slowly back and forth for as long as you play, over twenty seconds to an hour. Click Add a "
                   "season and choose a control.")
        .on("Seasons")
        .show("device:Seasons")
        .until(W::Season),
    say("All seasons", "Seasons scales every season together, so you can fade the slow movement in or out. Seasons follow Tide.")
        .on("Seasons")
        .show("param:seasons.depth"),
};

constexpr LessonPage kGuest[] = {
    say("Open the Guest tab", "Click the Guest tab along the bottom.").show("tab:Guest").until(W::Page, "Guest"),
    say("An instrument as a source", "The Guest plays one of your instrument plugins, AU or VST3, alongside everything else. Choose it from the "
                                     "menu at the top. If the menu is empty, choose Find my plugins. With no instrument loaded the Guest stays silent.")
        .on("Guest")
        .show("guest"),
    say("Play From", "Play From picks the notes the instrument hears: Bloom's notes from the keyboards and takes, MIDI input only, the Cycles "
                     "only or all of them. Transpose shifts every note in semitones.")
        .on("Guest")
        .show("param:guest.playFrom"),
    say("Its own controls", "Once an instrument is loaded six knobs appear. Choose controls picks the parameter each one moves and Plugin "
                            "window opens the instrument's own editor. The knobs learn, modulate and map to macros like any other.")
        .on("Guest")
        .show("guest"),
    say("A missing instrument", "Open a session on a computer without its instrument and the Guest says so, stays silent and keeps its "
                                "settings. Install the plugin or scan again, then choose it once more.")
        .on("Guest")
        .show("guest"),
    say("Note Length", "The Cycles can play the Guest. Note Length sets how long each of their notes holds there and on MIDI out. Bloom and "
                       "the resonator ring on their own.")
        .on("Cycles")
        .show("param:loops.gate"),
    say("MIDI out", "In the app the Cycles can also send their notes to another app or a synth. Choose an output, turn on MIDI Out and pick "
                    "one channel for all or one channel per cycle.")
        .on("Cycles")
        .show("device:MIDI out"),
};

constexpr LessonPage kControllers[] = {
    say("MIDI learn", "Any knob, fader or switch can follow a MIDI controller. Right-click it, choose MIDI learn, then move a control on "
                      "your controller.")
        .show("param:terrain.glide"),
    say("Controller templates", "Settings > Controllers has ready-made layouts for the nanoKONTROL2, the MIDImix, the Launch Control XL "
                                "and two generic ones. Apply one and its knobs move the macros and its faders the source levels.")
        .show("button:settings"),
    say("Your own templates", "When your mappings feel right, save them as a template in Settings > Controllers. Turn on a controller whose "
                              "name matches a template and the status bar suggests it. Nothing changes until you apply it.")
        .show("status"),
    say("Your own keys", "Settings > Keys lists every shortcut. Click one and press the new key, or clear it. Tab, the arrow keys and 1 to 9 "
                         "stay fixed.")
        .show("button:settings"),
    say("Keys follow you", "The menus, the hover help, the status bar and these lessons all show your keys. Right now {key:fade} fades in "
                           "and out and {key:capture} keeps a scene.")
        .show("status"),
};

constexpr LessonPage kRecording[] = {
    say("Record what you hear", "Press Rec or {key:record} to record to disk and again to stop. Right-click Rec to add stems or choose the folder.")
        .show("button:record"),
    say("The timeline", "The Timeline records a whole performance: every note, control and action. Press Record, play, then Stop.")
        .on("Timeline")
        .show("device:Performance")
        .until(W::Record),
    say("Playing back", "Play replays the performance from the start of the selection. Double-click the lanes to play from that moment.")
        .on("Timeline")
        .show("device:Performance"),
    say("Editing", "Drag across the lanes to select a stretch, then Erase, Smooth or Trim. Every edit can be undone with {key:undo}.")
        .on("Timeline")
        .show("device:Performance"),
    say("Rendering", "Render plays the performance offline into WAV files: the master, stems or a seamless loop. It can also loop the sound as "
                     "it is now, with no performance at all. A render sounds the same every time.")
        .on("Timeline")
        .show("device:Performance"),
};

constexpr LessonPage kSaving[] = {
    say("Saving", "Press {key:save} to save the session as one .tide file with every setting, scene and sound inside. Copy it to another computer and it "
                  "opens complete.")
        .show("button:session"),
    say("Unsaved changes", "Quitting, New and Open ask first when the session has changes: Save, Don't Save or Cancel.").show("button:session"),
    say("Autosave", "Every two minutes Tidefield keeps a copy of the session apart from your file. After a crash the next launch offers to "
                    "restore it. Settings > Files and Startup sets how often.")
        .show("button:settings"),
    say("Installation mode", "For galleries and long runs. Save the session, press Open this session at launch, then turn on Installation "
                             "mode. Tidefield then opens it and fades in by itself.")
        .on("Master")
        .show("device:Installation"),
    say("Daily schedule", "Daily fades in and out at two times every day. Keep awake stops the computer sleeping. If the audio interface goes "
                          "away, Tidefield keeps trying and fades back in.")
        .on("Master")
        .show("device:Installation"),
    say("Over to you", "That is the whole instrument. The manual covers every control and hover help explains whatever is under the mouse. "
                       "Open these lessons again from Help or the session menu."),
};

constexpr Lesson kLessons[] = {
    { "First sound", "Fade in, the terrain and scenes", kFirstSound },
    { "The drone", "Waves, chords, filter, movement and presets", kDrone },
    { "Clouds and the Browser", "Loading sounds, Places and drag and drop", kClouds },
    { "Performing", "Gestures, hold, capture, cycles and catch", kPerforming },
    { "Effects and macros", "Slots, sends, macros and the Medium", kEffects },
    { "Modulation and seasons", "Routes, sources and slow change", kModulation },
    { "The Guest and MIDI out", "Instruments, Play From and MIDI out", kGuest },
    { "Recording and rendering", "Rec, the timeline and offline renders", kRecording },
    { "Controllers and shortcuts", "MIDI learn, templates and your keys", kControllers },
    { "Saving and installations", "Sessions, autosave and installation mode", kSaving },
};

constexpr LessonPage kWhatsNewPages[] = {
    say("What's new", "Tidefield 1.4 adds macros, a Guest instrument, Places in the Browser, controller templates, your own shortcuts, "
                      "these lessons and autosave. Each page here shows where one of them lives."),
    say("Macros", "A macro is one knob that moves up to eight controls, each by its own amount. Right-click any knob and choose Map to "
                  "macro. The Macros tab holds all eight.")
        .show("tab:Macros"),
    say("The Guest", "The Guest tab plays one of your instrument plugins as another source, from Bloom's notes, MIDI or the Cycles. With no "
                     "plugin installed it simply stays silent.")
        .show("tab:Guest"),
    say("Cycles out", "The Cycles tab can send its notes to other apps and synths over MIDI out. Note Length sets how long each one holds.")
        .show("tab:Cycles"),
    say("Places", "Places in the Browser list your own sound folders. Drop files or folders from Finder or Explorer anywhere on the window "
                  "to load them.")
        .show("browser"),
    say("Controllers", "Settings > Controllers applies a ready-made layout for a common controller in one step, or saves your own mappings "
                       "as a template.")
        .show("button:settings"),
    say("Keys", "Settings > Keys lets you change any shortcut. The menus, the hover help and the lessons always show the keys you chose.")
        .show("button:settings"),
    say("Lessons", "Short lessons walk through the whole instrument, a few minutes each. Open them from Help, the session menu or Settings "
                   "> About.")
        .show("button:session"),
    say("Autosave", "Tidefield keeps a recovery copy of the session every two minutes. After a crash the next launch offers to restore it, "
                    "and quitting asks before unsaved changes are lost.")
        .show("button:settings"),
};

constexpr Lesson kWhatsNew { "What's new in 1.4", "Macros, the Guest, Places, keys and more", kWhatsNewPages };

constexpr std::array<const char*, 19> kKnownTargets { "terrain",       "browser",     "performance",    "pads",          "devices",
                                                      "topbar",        "status",      "button:session", "button:fade",   "button:record",
                                                      "button:settings", "pad:Swell", "pad:Shape",      "pad:Freeze all", "pad:Cycles",
                                                      "pad:Catch",     "pad:Take",    "button:drawpath", "guest" };

constexpr const char* kKeyOpen = "{key:";
constexpr int kKeyOpenLength = 5;
constexpr const char* kLessonKey = "lesson";
constexpr const char* kLessonPageKey = "lessonPage";
constexpr int kPad = 14, kRowH = 52, kFooterH = 42;
constexpr double kAdvanceMs = 1400.0;
}

std::span<const Lesson> lessons() { return kLessons; }

const Lesson& whatsNew() { return kWhatsNew; }

int lessonPageIndex(const juce::String& name)
{
    for (int p = 0; p < DeviceView::NumPages; ++p)
        if (DeviceView::pageName(p) == name)
            return p;
    return -1;
}

juce::String lessonText(const char* text, const KeyBindings& keys)
{
    const auto source = juce::String::fromUTF8(text);
    juce::String out;
    int from = 0;
    for (int open = source.indexOf(kKeyOpen); open >= 0; open = source.indexOf(from, kKeyOpen))
    {
        const int close = source.indexOfChar(open, '}');
        if (close < 0)
            break;
        out << source.substring(from, open);
        const auto id = source.substring(open + kKeyOpenLength, close);
        if (const auto action = keyActionFromId(id))
            out << (keys.get(*action).isValid() ? keys.text(*action)
                                                : "the " + juce::String(keyActionInfo(*action).name).upToFirstOccurrenceOf(" (", false, false) + " key (unassigned)");
        else
            out << id;
        from = close + 1;
    }
    return out + source.substring(from);
}

namespace {
juce::StringArray hardcodedKeys(const juce::String& text)
{
    juce::StringArray found;
    for (const char* modifier : { "Cmd+", "Ctrl+", "Shift+", "Option+", "Alt+" })
        if (text.contains(modifier))
            found.add(modifier);
    juce::StringArray words;
    words.addTokens(text, " .,:;!?()\"", {});
    words.removeEmptyStrings();
    for (int i = 0; i < words.size(); ++i)
    {
        const auto& w = words[i];
        const bool letter = w.length() == 1 && juce::CharacterFunctions::isUpperCase(w[0]) && w != "A";
        const bool always = w == "Esc" || w == "Backspace";
        const bool named = w == "Space" || w == "Return" || w == "Delete";
        const bool verb = i > 0 && (words[i - 1].equalsIgnoreCase("press") || words[i - 1].equalsIgnoreCase("hold"));
        if (letter || always || (named && verb))
            found.addIfNotAlreadyThere(w);
    }
    return found;
}

void checkLesson(const Lesson& l, const engine::ParamRegistry& registry, const KeyBindings& keys, juce::StringArray& problems)
{
    for (const auto& p : l.pages)
    {
        const auto where = juce::String(l.title) + " / " + p.title + ": ";
        for (const char* raw : { p.title, p.text })
        {
            const auto source = juce::String::fromUTF8(raw);
            for (int open = source.indexOf(kKeyOpen); open >= 0; open = source.indexOf(open + 1, kKeyOpen))
            {
                const int close = source.indexOfChar(open, '}');
                const auto id = close > open ? source.substring(open + kKeyOpenLength, close) : juce::String();
                if (! keyActionFromId(id).has_value())
                    problems.add(where + "no key action called " + (id.isNotEmpty() ? id : juce::String("(unclosed)")));
            }
            const auto shown = lessonText(raw, keys);
            for (const auto& key : hardcodedKeys(source.replace(kKeyOpen, " ")))
                problems.add(where + "names the key " + key + " directly; use {key:...} so it follows Settings > Keys");
            if (shown.contains("{") || shown.contains("}"))
                problems.add(where + "has a stray brace");
        }
        const auto text = lessonText(p.text, keys);
        if (text.isEmpty() || text.length() > 260)
            problems.add(where + "text is empty or longer than 260 characters");
        if (text.contains(juce::String::fromUTF8("\xe2\x80\x94")))
            problems.add(where + "text has an em dash");
        if (p.page != nullptr && lessonPageIndex(p.page) < 0)
            problems.add(where + "no device page called " + p.page);
        if (p.highlight != nullptr)
        {
            const juce::String target(p.highlight);
            if (target.startsWith("tab:"))
            {
                if (lessonPageIndex(target.substring(4)) < 0)
                    problems.add(where + "no device tab called " + target.substring(4));
            }
            else if (target.startsWith("param:"))
            {
                if (! registry.find(target.substring(6).toStdString()).has_value())
                    problems.add(where + "no parameter " + target.substring(6));
            }
            else if (target.startsWith("device:"))
            {
                if (target.length() <= 7)
                    problems.add(where + "device target without a name");
            }
            else if (std::find_if(kKnownTargets.begin(), kKnownTargets.end(), [&target](const char* t) { return target == t; }) == kKnownTargets.end())
                problems.add(where + "unknown highlight " + target);
        }
        if (p.wait == LessonWait::Page && (p.waitFor == nullptr || lessonPageIndex(p.waitFor) < 0))
            problems.add(where + "waits for a page that does not exist");
        if (p.wait == LessonWait::Param && (p.waitFor == nullptr || ! registry.find(p.waitFor).has_value()))
            problems.add(where + "waits for a parameter that does not exist");
        if (p.glideTo >= 9)
            problems.add(where + "glides to a scene beyond the number keys");
    }
}
}

juce::StringArray lessonProblems(const engine::ParamRegistry& registry)
{
    juce::StringArray problems;
    KeyBindings defaults;
    for (const auto& l : kLessons)
        checkLesson(l, registry, defaults, problems);
    checkLesson(kWhatsNew, registry, defaults, problems);
    return problems;
}

bool isNewerVersion(const juce::String& version, const juce::String& than)
{
    juce::StringArray a, b;
    a.addTokens(version.trim(), ".", {});
    b.addTokens(than.trim(), ".", {});
    for (int i = 0; i < std::max(a.size(), b.size()); ++i)
    {
        const int x = a[i].getIntValue(), y = b[i].getIntValue();
        if (x != y)
            return x > y;
    }
    return false;
}

bool shouldOfferWhatsNew(const juce::String& lastSeen, const juce::String& running)
{
    if (lastSeen.trim().isEmpty())
        return true;
    return isNewerVersion(running, lastSeen) && isNewerVersion(kWhatsNewVersion, lastSeen);
}

LessonPosition savedLessonPosition(juce::PropertiesFile& settings)
{
    const int l = juce::jlimit(0, static_cast<int>(std::size(kLessons)) - 1, settings.getIntValue(kLessonKey, 0));
    const int pages = static_cast<int>(kLessons[static_cast<std::size_t>(l)].pages.size());
    return { l, juce::jlimit(0, pages - 1, settings.getIntValue(kLessonPageKey, 0)) };
}

void saveLessonPosition(juce::PropertiesFile& settings, LessonPosition position)
{
    settings.setValue(kLessonKey, position.lesson);
    settings.setValue(kLessonPageKey, position.page);
}

LessonPanel::LessonPanel(Model& m, MainView& v, bool runActions) : model(m), view(v)
{
    model.add(this);
    for (auto* b : { &backButton, &nextButton, &indexButton, &closeButton })
    {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    }
    nextButton.setToggleState(true, juce::dontSendNotification);
    backButton.onClick = [this] { step(-1); };
    nextButton.onClick = [this] { step(1); };
    indexButton.onClick = [this] { showIndex(! indexShown); };
    closeButton.onClick = [this] {
        auto* owner = &view;
        later(owner, [owner] { owner->closeLessons(); });
    };
    closeButton.setHelp(&model, "close the lessons; open them again from Help or the session menu");
    const auto saved = savedLessonPosition(model.core.host.getSettings());
    lesson = saved.lesson;
    page = saved.page;
    if (runActions)
        enter();
    else
        highlightTarget = current().highlight != nullptr ? juce::String(current().highlight) : juce::String();
    refresh();
}

LessonPanel::~LessonPanel()
{
    model.remove(this);
    view.setLessonHighlight({});
}

const Lesson& LessonPanel::currentLesson() const { return news ? kWhatsNew : kLessons[static_cast<std::size_t>(lesson)]; }

const LessonPage& LessonPanel::current() const { return currentLesson().pages[static_cast<std::size_t>(page)]; }

int LessonPanel::pageCount() const { return static_cast<int>(currentLesson().pages.size()); }

void LessonPanel::show(int l, int p)
{
    if (l < 0)
        return showIndex(true);
    news = false;
    lesson = juce::jlimit(0, static_cast<int>(std::size(kLessons)) - 1, l);
    page = juce::jlimit(0, pageCount() - 1, p);
    indexShown = false;
    enter();
    refresh();
}

void LessonPanel::showWhatsNew(int p)
{
    news = true;
    page = juce::jlimit(0, pageCount() - 1, p);
    indexShown = false;
    enter();
    refresh();
}

void LessonPanel::restoreWhatsNew(int p)
{
    news = true;
    page = juce::jlimit(0, pageCount() - 1, p);
    indexShown = false;
    highlightTarget = current().highlight != nullptr ? juce::String(current().highlight) : juce::String();
    refresh();
}

juce::String LessonPanel::missingTarget() const
{
    if (highlightTarget.isEmpty())
        return {};
    return view.locateLessonTarget(highlightTarget, false).isEmpty() ? highlightTarget : juce::String();
}

void LessonPanel::enter()
{
    const auto& p = current();
    if (p.page != nullptr)
        if (const int index = lessonPageIndex(p.page); index >= 0)
            view.showPage(index);
    if (p.glideTo >= 0)
        view.glideTo(p.glideTo);
    highlightTarget = p.highlight != nullptr ? juce::String(p.highlight) : juce::String();
    view.setLessonHighlight(highlightTarget.isNotEmpty() ? view.locateLessonTarget(highlightTarget, true) : juce::Rectangle<int>());

    done = false;
    doneAt = 0.0;
    waitParam.reset();
    if (p.wait == LessonWait::Param && p.waitFor != nullptr)
        waitParam = model.registry.find(p.waitFor);
    baseValue = waitParam.has_value() ? model.toNorm(static_cast<engine::P>(*waitParam), model.value(*waitParam)) : 0.0f;
    baseX = model.value(engine::P::TerrainX);
    baseY = model.value(engine::P::TerrainY);
    baseCount = p.wait == LessonWait::Capture  ? model.core.scenes.getScenes().size()
                : p.wait == LessonWait::Route  ? model.core.mod.getRoutes().size()
                : p.wait == LessonWait::Season ? model.core.seasons.getSeasons().size()
                                               : 0;
    if (news)
        return;
    auto& settings = model.core.host.getSettings();
    saveLessonPosition(settings, { lesson, page });
    settings.saveIfNeeded();
}

void LessonPanel::step(int direction)
{
    const int count = static_cast<int>(std::size(kLessons));
    if (news)
    {
        if (direction > 0 && page + 1 >= pageCount())
        {
            auto* owner = &view;
            later(owner, [owner] { owner->closeLessons(); });
        }
        else if (page + direction >= 0)
            showWhatsNew(page + direction);
        return;
    }
    if (direction > 0)
    {
        if (page + 1 < pageCount())
            show(lesson, page + 1);
        else if (lesson + 1 < count)
            show(lesson + 1, 0);
        else
        {
            auto* owner = &view;
            later(owner, [owner] { owner->closeLessons(); });
        }
    }
    else if (page > 0)
        show(lesson, page - 1);
    else if (lesson > 0)
        show(lesson - 1, static_cast<int>(kLessons[static_cast<std::size_t>(lesson - 1)].pages.size()) - 1);
}

void LessonPanel::showIndex(bool on)
{
    indexShown = on;
    hoverRow = -1;
    refresh();
}

void LessonPanel::refresh()
{
    const bool first = page == 0 && (news || lesson == 0);
    const bool lastPage = page + 1 >= pageCount();
    const bool lastLesson = news || lesson + 1 >= static_cast<int>(std::size(kLessons));
    shownKeys = model.core.keys.getVersion();
    backButton.setVisible(! indexShown);
    nextButton.setVisible(! indexShown);
    backButton.setEnabled(! first);
    nextButton.setButtonText(! lastPage ? "Next" : lastLesson ? "Finish" : "Next lesson");
    indexButton.setButtonText(indexShown ? "Back to the lesson" : "All lessons");
    resized();
    repaint();
}

bool LessonPanel::waitDone() const
{
    const auto& f = model.frame();
    switch (current().wait)
    {
        case LessonWait::FadeIn: return f.fadeState == engine::FadeState::Open || f.fadeState == engine::FadeState::FadingIn;
        case LessonWait::Cursor: return std::abs(model.value(engine::P::TerrainX) - baseX) + std::abs(model.value(engine::P::TerrainY) - baseY) > 0.04f;
        case LessonWait::Page: return current().waitFor != nullptr && view.getPage() == lessonPageIndex(current().waitFor);
        case LessonWait::Param:
            return waitParam.has_value() && std::abs(model.toNorm(static_cast<engine::P>(*waitParam), model.value(*waitParam)) - baseValue) > 0.04f;
        case LessonWait::Capture: return model.core.scenes.getScenes().size() > baseCount;
        case LessonWait::Route: return model.core.mod.getRoutes().size() > baseCount;
        case LessonWait::Season: return model.core.seasons.getSeasons().size() > baseCount;
        case LessonWait::Take: return f.gestureState != engine::GestureState::Idle;
        case LessonWait::Record:
            return model.core.performance.getState() == PerformanceController::State::Recording
                   || model.core.recorder.getStatus().state == io::Recorder::State::Recording;
        case LessonWait::None:
        default: return false;
    }
}

void LessonPanel::tick()
{
    if (model.core.keys.getVersion() != shownKeys)
        refresh();
    if (indexShown)
        return view.setLessonHighlight({});
    view.setLessonHighlight(highlightTarget.isNotEmpty() ? view.locateLessonTarget(highlightTarget, false) : juce::Rectangle<int>());
    if (current().wait == LessonWait::None)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! done && waitDone())
    {
        done = true;
        doneAt = now;
        repaint(tryArea);
    }
    else if (done && doneAt > 0.0 && now - doneAt > kAdvanceMs && page + 1 < pageCount())
    {
        doneAt = 0.0;
        step(1);
    }
}

juce::Rectangle<int> LessonPanel::indexRow(int i) const { return { kPad / 2, metric::header + 8 + i * kRowH, getWidth() - kPad, kRowH - 4 }; }

void LessonPanel::resized()
{
    auto r = getLocalBounds();
    closeButton.setBounds(r.removeFromTop(metric::header).removeFromRight(30).reduced(4, 2));
    auto footer = r.removeFromBottom(kFooterH).reduced(kPad - 4, 8);
    if (indexShown)
        indexButton.setBounds(footer);
    else
    {
        backButton.setBounds(footer.removeFromLeft(64));
        nextButton.setBounds(footer.removeFromRight(juce::jmin(104, footer.getWidth() / 2)));
        footer.reduce(6, 0);
        indexButton.setBounds(footer);
    }

    const int w = getWidth() - 2 * kPad;
    if (indexShown || w <= 0)
        return;
    juce::AttributedString text;
    text.setLineSpacing(4.0f);
    text.setWordWrap(juce::AttributedString::byWord);
    text.append(lessonText(current().text, model.core.keys), font(13.5f), colour::text());
    body.createLayout(text, static_cast<float>(w));
    textArea = { kPad, metric::header + 98, w, static_cast<int>(std::ceil(body.getHeight())) };
    tryArea = current().wait != LessonWait::None ? juce::Rectangle<int>(kPad, textArea.getBottom() + 16, w, 46) : juce::Rectangle<int>();
}

void LessonPanel::paint(juce::Graphics& g)
{
    const int total = static_cast<int>(std::size(kLessons));
    drawPanel(g, getLocalBounds().toFloat(),
              indexShown ? juce::String("Lessons") : news ? juce::String("What's new") : "Lesson " + juce::String(lesson + 1) + " of " + juce::String(total));
    g.setColour(colour::line());
    g.fillRect(getLocalBounds().removeFromBottom(kFooterH).removeFromTop(1).reduced(kPad, 0));

    if (indexShown)
    {
        for (int i = 0; i <= total; ++i)
        {
            const bool extra = i == total;
            const auto row = indexRow(i).toFloat();
            const bool on = extra ? news : i == lesson && ! news;
            if (on || i == hoverRow)
            {
                g.setColour(on ? colour::panelHi() : colour::lift(colour::panel(), 0.05f));
                g.fillRoundedRectangle(row, metric::radius);
            }
            const auto badge = juce::Rectangle<float>(24.0f, 24.0f).withCentre({ row.getX() + 20.0f, row.getCentreY() });
            g.setColour(on ? colour::accent() : colour::textFaint());
            if (extra)
                g.fillEllipse(badge.reduced(7.0f));
            else
            {
                g.drawEllipse(badge.reduced(0.75f), 1.5f);
                g.setFont(font(11.5f, 600));
                g.drawText(juce::String(i + 1), badge, juce::Justification::centred);
            }
            auto text = row.withTrimmedLeft(42.0f).reduced(0.0f, 7.0f);
            const auto& l = extra ? kWhatsNew : kLessons[static_cast<std::size_t>(i)];
            g.setColour(colour::textFaint());
            g.setFont(font(11.0f, 500));
            g.drawText(juce::String(static_cast<int>(l.pages.size())) + " pages", text.removeFromRight(56.0f).withTrimmedRight(6.0f).removeFromTop(18.0f),
                       juce::Justification::centredRight);
            g.setColour(colour::text());
            g.setFont(font(13.0f, 600));
            g.drawText(l.title, text.removeFromTop(18.0f), juce::Justification::centredLeft, true);
            g.setColour(colour::textDim());
            g.setFont(font(11.5f));
            g.drawText(l.summary, row.withTrimmedLeft(42.0f).withTrimmedRight(6.0f).withTrimmedTop(25.0f).withHeight(18.0f), juce::Justification::centredLeft,
                       true);
        }
        return;
    }

    const auto& l = currentLesson();
    const int w = getWidth() - 2 * kPad;
    int y = metric::header + 14;
    g.setColour(colour::accent());
    g.setFont(caps(11.0f));
    g.drawText(juce::String(l.title).toUpperCase(), kPad, y, w, 16, juce::Justification::centredLeft, true);
    y += 20;
    g.setColour(colour::text());
    g.setFont(font(18.0f, 600));
    g.drawText(lessonText(current().title, model.core.keys), kPad, y, w, 26, juce::Justification::centredLeft, true);
    y += 34;
    const int pages = pageCount();
    for (int i = 0; i < pages; ++i)
    {
        const auto dot = juce::Rectangle<float>(7.0f, 7.0f).withCentre({ static_cast<float>(kPad + 4 + i * 14), static_cast<float>(y + 6) });
        g.setColour(i == page ? colour::accent() : i < page ? colour::textDim() : colour::line());
        g.fillEllipse(dot);
    }
    g.setColour(colour::textFaint());
    g.setFont(font(11.0f, 500));
    g.drawText("Page " + juce::String(page + 1) + " of " + juce::String(pages), kPad, y, w, 12, juce::Justification::centredRight);

    body.draw(g, textArea.toFloat());

    if (! tryArea.isEmpty())
    {
        auto box = tryArea.toFloat();
        g.setColour(colour::panelHi());
        g.fillRoundedRectangle(box, metric::radius);
        g.setColour(done ? colour::good() : colour::accent());
        g.fillRoundedRectangle(box.removeFromLeft(3.0f), 1.5f);
        box.reduce(12.0f, 6.0f);
        g.setFont(font(12.5f, 600));
        g.drawText(done ? "Done" : "Your turn", box.removeFromTop(17.0f), juce::Justification::centredLeft, true);
        g.setColour(colour::textDim());
        g.setFont(font(11.5f));
        g.drawText(done ? (page + 1 < pages ? "On to the next page." : "Press Next for the next lesson.") : "This page moves on when you have done it.", box,
                   juce::Justification::centredLeft, true);
    }
}

void LessonPanel::mouseMove(const juce::MouseEvent& e)
{
    if (! indexShown)
        return;
    int h = -1;
    for (int i = 0; i <= static_cast<int>(std::size(kLessons)); ++i)
        if (indexRow(i).contains(e.getPosition()))
            h = i;
    if (h != hoverRow)
    {
        hoverRow = h;
        repaint();
    }
}

void LessonPanel::mouseExit(const juce::MouseEvent&)
{
    if (hoverRow >= 0)
    {
        hoverRow = -1;
        repaint();
    }
}

void LessonPanel::mouseUp(const juce::MouseEvent& e)
{
    if (! indexShown || e.mods.isPopupMenu())
        return;
    const int total = static_cast<int>(std::size(kLessons));
    if (indexRow(total).contains(e.getPosition()))
        return showWhatsNew(news ? page : 0);
    for (int i = 0; i < total; ++i)
        if (indexRow(i).contains(e.getPosition()))
            return show(i, i == lesson && ! news ? page : 0);
}
}
