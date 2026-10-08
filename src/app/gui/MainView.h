#pragma once

#include "Pages.h"
#include "TerrainView.h"

#include <memory>

namespace tf::app::gui {
class TopBar;
class Browser;
class MacroPanel;
class PadRow;
class StatusBar;

class MainView final : public juce::Component, private juce::Timer
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

    void chooseSample(int slot);
    void loadFactory(int soundIndex, int slot);
    void showAudioSettings();
    void showPage(int page) { devices->show(page); }
    void toggleProjector();
    bool isProjectorOpen() const noexcept { return projector != nullptr; }
    static void switchTheme(AppCore& core, Theme t);

private:
    void buildInterface();
    void teardownInterface();
    void rebuildInterface();
    void frame();
    void timerCallback() override;
    void releaseHolds();
    bool handleNoteKey(const juce::KeyPress& key);
    void glideToScene(int index, bool jump);

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
    std::unique_ptr<juce::VBlankAttachment> vblank;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> projector;

    struct Hold
    {
        int keyCode;
        engine::P param;
        bool down = false;
    };
    std::array<Hold, 3> holds { { { 'S', engine::P::SwellHold }, { 'H', engine::P::HushHold }, { 'T', engine::P::SlowHold } } };

public:
    bool noteMode = false;
    int octave = 4;
    float velocity = 0.75f;

private:
    std::array<int, 128> keyNote {};
    double lastFrameTime = 0.0;
};
}
