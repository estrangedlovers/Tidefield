#pragma once

#include "ParamKnob.h"

#include <memory>
#include <vector>

namespace tf::app {

/** Titled groups of knobs laid out in flowing rows. Extra components (buttons,
    menus) can be attached to a section's header. */
class KnobPanel : public juce::Component
{
public:
    explicit KnobPanel(engine::Engine& engine) : engine(engine) {}

    /** Returns the index of the new section. */
    int addSection(const juce::String& title, std::initializer_list<engine::P> params);
    void addParamsToSection(int section, std::initializer_list<engine::P> params);
    void addHeaderComponent(int section, juce::Component& component, int width);

    ParamKnob* findKnob(engine::P param) const;
    virtual void update(const engine::TelemetryFrame& frame);

    /** Height needed for a given width (for scroll viewports). */
    int layout(int width, bool apply);

    void paint(juce::Graphics&) override;
    void resized() override { layout(getWidth(), true); }

    static constexpr int kKnobW = 96;
    static constexpr int kKnobH = 96;
    static constexpr int kHeader = 28;

protected:
    struct Section
    {
        juce::String title;
        std::vector<ParamKnob*> knobs;
        std::vector<std::pair<juce::Component*, int>> header;
        juce::Rectangle<int> bounds;
    };

    engine::Engine& engine;
    std::vector<std::unique_ptr<ParamKnob>> knobs;
    std::vector<Section> sections;
};

} // namespace tf::app
