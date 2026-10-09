#pragma once

#include "Widgets.h"

#include <io/Presets.h>

#include <memory>
#include <vector>

namespace tf::app::gui {
class Device : public juce::Component
{
public:
    Device(Model& m, juce::String title, juce::Colour tab = colour::accent());

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
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    void setPresets(std::string kind, std::string prefix, std::vector<engine::P> params);

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

    juce::Rectangle<int> presetButton() const;
    virtual void showPresetMenu();
    virtual void applyPreset(const io::Preset& p);
    virtual io::Preset capturePreset(const std::string& name) const;
    std::string presetKind, presetPrefix;
    std::vector<engine::P> presetParams;
    bool presetHover = false;
};

class DeviceView final : public juce::Component
{
public:
    explicit DeviceView(Model& m);
    ~DeviceView() override;

    enum Page { Drone, Clouds, Resonator, Bloom, Input, Looper, Weather, Guest, Gestures, Loops, Seasons, Modulation, Macros, Timeline, Mixer, Effects, Master, Midi, NumPages };
    static juce::String pageName(int p);

    void show(int page);
    int getPage() const noexcept { return page; }
    int getEffectsChain() const noexcept { return fxChain; }
    void showEffectsFor(int chain);
    juce::Rectangle<int> tabBounds(int i) const;

    std::function<void(int slot)> onLoadSample;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
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
std::unique_ptr<juce::Component> createRemoteView(Model& model);
std::unique_ptr<juce::Component> createCyclesOutView(Model& model);
std::unique_ptr<juce::Component> createInstallationView(Model& model);
std::unique_ptr<juce::Component> createSpaceView(Model& model);
}
