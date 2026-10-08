#pragma once

#include "Controls.h"

#include <memory>

namespace tf::app::gui {

/** Stereo peak meter with a held peak line; reads the master or one strip. */
class Meter final : public juce::Component, public Animated
{
public:
    /** strip < 0: master. */
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

/** A sample's outline with the grains (or Bloom voices) currently reading it. Click
    to load a sound, right-click to clear it. */
class Waveform final : public juce::Component, public Animated
{
public:
    /** slot 0..3 = clouds, kNumClouds = Bloom. */
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

/** Colour and space on one surface: left dark / right bright, down close and dry /
    up far and wet. Double-click recentres. */
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

/** A playable keyboard for Bloom: click or drag across keys, or play the computer
    keyboard in note mode. Lights the notes Bloom is sounding and the notes in key. */
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
};

} // namespace tf::app::gui
