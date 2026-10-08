#pragma once

#include "Controls.h"

#include <memory>

namespace tf::app::gui {
class Meter final : public juce::Component, public Animated
{
public:
    Meter(Model& m, int strip = -1, bool horizontal = true);
    ~Meter() override;
    void tick() override;
    void paint(juce::Graphics& g) override;

private:
    Model& model;
    int strip;
    bool horizontal;
    float shownL = 0.0f, shownR = 0.0f, holdL = 0.0f, holdR = 0.0f;
    int holdFramesL = 0, holdFramesR = 0;
};

class Waveform final : public juce::Component, public Animated
{
public:
    Waveform(Model& m, int slot, juce::Colour colour);
    ~Waveform() override;
    void tick() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;

    std::function<void(int slot)> onLoad;

private:
    void rebuildPeaks();

    Model& model;
    int slot;
    juce::Colour colour;
    std::shared_ptr<const dsp::SampleBuffer> shown;
    std::vector<float> peaks;
    int shownViews = 0;
    float shownPos = -1.0f;
};

class ShapePad final : public juce::Component, public Animated
{
public:
    explicit ShapePad(Model& m);
    ~ShapePad() override;
    void tick() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;

private:
    void setFrom(juce::Point<float> p);
    Model& model;
    float shownC = 0.0f, shownS = 0.0f;
};

class KeyboardStrip final : public juce::Component, public Animated
{
public:
    explicit KeyboardStrip(Model& m);
    ~KeyboardStrip() override;
    void tick() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    int lowNote = 48, numOctaves = 3;

private:
    int noteAt(juce::Point<float> p) const;
    juce::Rectangle<float> keyRect(int note) const;
    static bool isBlack(int note) { const int n = note % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
    Model& model;
    int heldNote = -1;
    std::array<float, 128> lit {};
    std::vector<float> zoneRoots;
    int zoneFrames = 0;
};
}
