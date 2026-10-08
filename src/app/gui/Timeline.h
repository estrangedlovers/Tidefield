#pragma once

#include "Controls.h"
#include "Model.h"

namespace tf::app::gui {
class TimelineView final : public juce::Component, public Animated
{
public:
    explicit TimelineView(Model& m);
    ~TimelineView() override;

    void tick() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    juce::Rectangle<float> laneArea() const;
    juce::Rectangle<float> rulerArea() const;
    float xOf(double seconds) const;
    double secondsAt(float x) const;
    double totalSeconds() const;
    int visibleLanes() const;
    juce::String laneName(const io::Performance::Lane& lane) const;
    void drawLane(juce::Graphics& g, const io::Performance::Lane& lane, juce::Rectangle<float> r) const;
    void refreshButtons();
    void showRenderMenu();

    Model& model;
    PerformanceController& perf;
    FlatButton recordButton, playButton, stopButton, eraseButton, muteButton, smoothButton, trimButton, clearButton, saveButton, renderButton;
    std::vector<io::Performance::Lane> lanes;
    int laneOffset = 0;
    double dragAnchor = 0.0;
    std::uint64_t shownRevision = ~std::uint64_t { 0 };
    std::size_t shownCount = 0;
    int shownState = -1;
    float shownPosition = -1.0f, shownProgress = -1.0f;
};
}
