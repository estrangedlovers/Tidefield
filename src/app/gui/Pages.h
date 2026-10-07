#pragma once

#include "Widgets.h"

#include <memory>
#include <vector>

namespace tf::app::gui {

/** One device box in the bottom panel: a title bar and controls flowing into
    columns, top to bottom, like a device in a studio's device chain. An optional
    full-width widget (a waveform, a type menu) sits above the controls. */
class Device : public juce::Component
{
public:
    Device(Model& m, juce::String title, juce::Colour tab = colour::accent);

    /** Adds the natural control for a parameter: a knob, a switch or a row of choices. */
    ParamComponent* add(engine::P p, juce::String help = {});
    ParamComponent* addKnob(engine::P p, juce::String label = {}, juce::String help = {}, int w = metric::knobW, int h = metric::knobH);
    template <typename T>
    T* add(std::unique_ptr<T> c, int w, int h)
    {
        auto* raw = c.get();
        addAndMakeVisible(*raw);
        items.push_back({ raw, w, h });
        owned.push_back(std::move(c));
        return raw;
    }
    template <typename T>
    T* setTop(std::unique_ptr<T> c, int h, int minWidth)
    {
        auto* raw = c.get();
        addAndMakeVisible(*raw);
        top = { raw, minWidth, h };
        owned.push_back(std::move(c));
        return raw;
    }

    int preferredWidth(int height) const;
    void paint(juce::Graphics& g) override;
    void resized() override;

protected:
    struct Item
    {
        juce::Component* c = nullptr;
        int w = 0, h = 0;
    };
    juce::Rectangle<int> content() const;
    Model& model;
    juce::String title;
    juce::Colour tab;
    std::vector<Item> items;
    Item top;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

/** The bottom panel: a row of tabs (Drone, Clouds, ... MIDI) and the selected page's
    devices side by side, scrolling sideways when they do not fit. */
class DeviceView final : public juce::Component
{
public:
    explicit DeviceView(Model& m);
    ~DeviceView() override;

    enum Page { Drone, Clouds, Resonator, Bloom, Input, Looper, Weather, Gestures, Loops, Seasons, Mixer, Effects, Master, Midi, NumPages };
    static juce::String pageName(int p);

    void show(int page);
    int getPage() const noexcept { return page; }
    /** Effects page: which chain (strip index, or kNumStrips + 0/1/2 for the buses and master). */
    void showEffectsFor(int chain);

    std::function<void(int slot)> onLoadSample;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> tabBounds(int i) const;
    void build();
    void layoutRow();

    Model& model;
    int page = Drone;
    int hoverTab = -1;
    int fxChain = 0;
    juce::Viewport viewport;
    juce::Component row;
    std::vector<std::unique_ptr<Device>> devices;
};

} // namespace tf::app::gui
