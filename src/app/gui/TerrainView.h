#pragma once

#include "Model.h"
#include "Style.h"

#include <array>
#include <deque>
#include <vector>

namespace tf::app::gui {

/** Glide, rename, update, fold in, delete: the menu for one scene. */
void showSceneMenu(Model& model, int scene);

/** The performance surface. Scenes are coloured places; each one's light grows with
    how much of the sound it is shaping right now. The performer's cursor is the
    crosshair; the sound itself (cursor plus wander) is the bright point, with a trail,
    grains drifting off it, drone voices orbiting it, ripples when the resonator is
    struck and blooms when Bloom plays.

      drag empty space     move the sound there (it glides)
      click a scene        glide to it      Shift-click: jump
      drag a scene         move it
      double-click         capture what you hear as a new scene, right there
      right-click a scene  rename, update, delete
      Draw mode            draw a path; the sound travels it on its own */
class TerrainView final : public juce::Component, public Animated
{
public:
    explicit TerrainView(Model& model);
    ~TerrainView() override;

    void tick() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    /** Path drawing: while on, a drag records a path instead of moving the cursor. */
    void setDrawMode(bool on) { drawMode = on; repaint(); }
    bool isDrawMode() const noexcept { return drawMode; }
    std::function<void(const std::vector<engine::Point2>&)> onPathDrawn;

private:
    struct Particle
    {
        juce::Point<float> p, v;
        float age = 0.0f, life = 1.0f, size = 2.0f;
        juce::Colour colour;
    };
    struct Ripple
    {
        juce::Point<float> p;
        float age = 0.0f, strength = 1.0f;
        juce::Colour colour;
    };

    juce::Rectangle<float> field() const;
    juce::Point<float> toScreen(engine::Point2 t) const;
    engine::Point2 toTerrain(juce::Point<float> s) const;
    int sceneAt(juce::Point<float> s) const;
    juce::Colour soundColour() const;
    void renderBackdrop();
    void setCursorTo(juce::Point<float> s);

    Model& model;
    juce::Image backdrop; // grid and contours, redrawn on resize only
    double lastTime = 0.0;
    float energy = 0.0f, tidePhase = 0.0f, orbit = 0.0f;
    engine::Point2 shownPos { 0.5f, 0.5f }, shownCursor { 0.5f, 0.5f };
    std::vector<float> shownWeights;
    std::deque<juce::Point<float>> trail;
    float trailClock = 0.0f;
    std::vector<Particle> particles;
    std::vector<Ripple> ripples;
    std::array<float, 24> lastModes {};
    std::array<bool, 8> lastBloom {};
    std::array<float, 4> emitCarry {};

    enum class Drag { None, Cursor, Scene, Path } drag = Drag::None;
    int dragScene = -1, hoverScene = -1;
    bool sceneMoved = false;
    bool drawMode = false;
    std::vector<engine::Point2> drawing;
    std::vector<engine::Point2> shownPath; // the path the engine follows, if any
public:
    void setShownPath(std::vector<engine::Point2> p) { shownPath = std::move(p); }
};

} // namespace tf::app::gui
