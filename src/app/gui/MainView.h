#pragma once

#include "Pages.h"
#include "Settings.h"
#include "TerrainView.h"

#include <memory>

namespace tf::app::gui {
class TopBar;
class Browser;
class MacroPanel;
class PadRow;
class StatusBar;
class LessonPanel;

class MainView final : public juce::Component,
                       public juce::DragAndDropContainer,
                       public juce::DragAndDropTarget,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit MainView(AppCore& core);
    ~MainView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType cause) override;
    void parentHierarchyChanged() override;
    void paintOverChildren(juce::Graphics& g) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    bool shouldDropFilesWhenDraggedExternally(const SourceDetails& details, juce::StringArray& files, bool& canMoveFiles) override;

    void chooseSample(int slot);
    void loadFactory(int soundIndex, int slot);
    void loadFiles(const juce::Array<juce::File>& files, int slot);
    void showLoadMenu(const juce::String& title, bool canAddToKeyboard, std::function<void(int slot)> chosen);
    static juce::var dragFactorySound(int soundIndex);
    static juce::var dragFile(const juce::File& file);
    void showAudioSettings();
    void openSettings(SettingsTab tab = SettingsTab::Look);
    void showPage(int page) { devices->show(page); }
    int getPage() const { return devices->getPage(); }
    void performAction(KeyAction action);
    static void keysChanged();
    void glideTo(int scene) { glideToScene(scene, false); }
    void toggleProjector();
    bool isProjectorOpen() const noexcept { return projector != nullptr; }
    static void switchTheme(AppCore& core, Theme t);

    void openLessons();
    void closeLessons();
    bool areLessonsOpen() const noexcept { return lessons != nullptr; }
    void showLesson(int lesson, int page);
    juce::String missingLessonTarget() const;
    void offerLessons();
    void dismissLessonOffer();
    bool isOfferingLessons() const noexcept { return lessonOffer; }
    static void showLessonsFor(AppCore& core);
    juce::Rectangle<int> locateLessonTarget(const juce::String& target, bool reveal);
    void setLessonHighlight(juce::Rectangle<int> area);

private:
    void buildInterface();
    void teardownInterface();
    void rebuildInterface();
    void frame();
    void timerCallback() override;
    void releaseHolds();
    bool handleNoteKey(const juce::KeyPress& key);
    void glideToScene(int index, bool jump);

    struct DropTarget
    {
        enum Kind { None, Ask, Slot, Open, Place } kind = None;
        int slot = -1;
        juce::Rectangle<int> area;
    };
    enum class DragContent { Nothing, Audio, Session, Folder };
    static DragContent classify(const juce::StringArray& files);
    DropTarget dropTargetAt(juce::Point<int> p, DragContent content) const;
    void showDropTarget(const DropTarget& target);
    void dropAudio(const juce::Array<juce::File>& files, const DropTarget& target);
    void dropFactory(int soundIndex, const DropTarget& target);

    AppCore& core;
    juce::SharedResourcePointer<LookAndFeel> lookAndFeel;
    Model model;
    std::unique_ptr<TopBar> topBar;
    std::unique_ptr<Browser> browser;
    std::unique_ptr<TerrainView> terrain;
    std::unique_ptr<MacroPanel> macros;
    std::unique_ptr<PadRow> pads;
    std::unique_ptr<DeviceView> devices;
    std::unique_ptr<StatusBar> status;
    std::unique_ptr<LessonPanel> lessons;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> projector;
    std::function<void(int, bool)> oscSceneFallback;

    struct Hold
    {
        KeyAction action;
        engine::P param;
        int keyCode = 0;
        bool down = false;
    };
    std::array<Hold, 3> holds { { { KeyAction::Swell, engine::P::SwellHold }, { KeyAction::Hush, engine::P::HushHold }, { KeyAction::Slow, engine::P::SlowHold } } };

public:
    bool noteMode = false;
    int octave = 4;
    float velocity = 0.75f;

private:
    std::array<int, 128> keyNote {};
    double lastFrameTime = 0.0;
    juce::Rectangle<int> dropHighlight;
    DropTarget::Kind dropKind = DropTarget::None;
    int dropSlot = -1;
    int dropCount = 0;
    juce::String dropLabel;
    juce::Rectangle<int> lessonHighlight;
    juce::Component::SafePointer<juce::Component> lessonTarget;
    juce::String lessonTargetName;
    int lessonRetry = 0;
    bool lessonOffer = false;
};
}
