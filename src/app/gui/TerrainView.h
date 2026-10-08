#pragma once

#include "Controls.h"

#include <array>
#include <deque>
#include <vector>

namespace tf::app::gui {
void showSceneMenu(Model& model, int scene, juce::Component* owner);

class TerrainView final : public juce::Component, public Animated
{
public:
    explicit TerrainView(Model& model, bool presentation = false);
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

    void setDrawMode(bool on);
    bool isDrawMode() const noexcept { return drawMode; }

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
    juce::Image backdrop;
    juce::Image glowLayer;
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
    const bool presentation;
public:
    std::function<void()> onDoubleClick;
private:
    std::vector<engine::Point2> drawing;
    std::vector<engine::Point2> shownPath;
    std::uint64_t shownPathVersion = 0;
    std::uint64_t shownSceneVersion = ~std::uint64_t { 0 };
    bool needsRepaint = true;
    FlatButton drawButton { "Draw path", display::tide() }, clearButton { "Clear path" };
};
}
