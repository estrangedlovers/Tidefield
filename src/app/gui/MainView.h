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

/** The whole window, native and custom-drawn:

      top bar      session, fade, panic, record, auto master, CPU, meter, audio
      browser      scenes and sounds (left)
      terrain      the performance surface (centre)
      macros       Tide, Wander, Gravity, key, scale, recording type, shape (right)
      pads         Swell, Hush, Slow, Freeze all, Hold input, Loop, Loops, Catch
      devices      every engine page, in tabs (bottom)
      status bar   what the thing under the mouse does, and messages

    Owns keyboard handling: every key is consumed, so macOS never beeps, and held
    gestures (S, H, T) release when the key comes up or the app loses focus. */
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
    /** Shows a device page (also used by `--ui-test` to visit every page). */
    void showPage(int page) { devices->show(page); }
    /** Opens or closes the projector window (a full-screen terrain for the audience). */
    void toggleProjector();
    bool isProjectorOpen() const noexcept { return projector != nullptr; }

private:
    void frame();
    void timerCallback() override;
    void releaseHolds();
    bool handleNoteKey(const juce::KeyPress& key);
    void glideToScene(int index, bool jump);

    AppCore& core;
    juce::SharedResourcePointer<LookAndFeel> lookAndFeel; // one per process, shared by every open window
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
    // Computer-keyboard note mode (M): the home row plays Bloom, like a studio's
    // computer MIDI keyboard. Z/X change octave, C/V velocity.
    bool noteMode = false;
    int octave = 4;
    float velocity = 0.75f;

private:
    std::array<int, 128> keyNote {}; // key code -> sounding note + 1 (0 = none)
    double lastFrameTime = 0.0;
};

} // namespace tf::app::gui
