#include "KnobPanel.h"

#include "Theme.h"

namespace tf::app {

int KnobPanel::addSection(const juce::String& title, std::initializer_list<engine::P> params)
{
    sections.push_back({ title, {}, {}, {} });
    addParamsToSection(static_cast<int>(sections.size()) - 1, params);
    return static_cast<int>(sections.size()) - 1;
}

void KnobPanel::addParamsToSection(int section, std::initializer_list<engine::P> params)
{
    for (auto p : params)
    {
        knobs.push_back(std::make_unique<ParamKnob>(engine, p));
        addAndMakeVisible(*knobs.back());
        sections[static_cast<std::size_t>(section)].knobs.push_back(knobs.back().get());
    }
}

void KnobPanel::addHeaderComponent(int section, juce::Component& component, int width)
{
    sections[static_cast<std::size_t>(section)].header.emplace_back(&component, width);
    addAndMakeVisible(component);
}

ParamKnob* KnobPanel::findKnob(engine::P param) const
{
    for (const auto& k : knobs)
        if (k->getParam() == param)
            return k.get();
    return nullptr;
}

void KnobPanel::update(const engine::TelemetryFrame& frame)
{
    for (auto& k : knobs)
        k->update(frame);
}

int KnobPanel::layout(int width, bool apply)
{
    int y = 0;
    const int inner = std::max(kKnobW, width - 16);
    const int perRow = std::max(1, inner / kKnobW);
    for (auto& s : sections)
    {
        // Hidden knobs (e.g. an empty FX slot) take no space; a section with none
        // collapses to its header.
        std::vector<ParamKnob*> shown;
        for (auto* k : s.knobs)
            if (k->isVisible())
                shown.push_back(k);
        const int count = static_cast<int>(shown.size());
        const int rows = (count + perRow - 1) / perRow;
        const int h = kHeader + rows * kKnobH + (rows > 0 ? 8 : 0);
        if (apply)
        {
            s.bounds = { 0, y, width, h };
            int hx = width - 8;
            for (auto& [comp, w] : s.header)
            {
                hx -= w;
                comp->setBounds(hx, y + 3, w, kHeader - 6);
                hx -= 6;
            }
            for (int i = 0; i < count; ++i)
                shown[static_cast<std::size_t>(i)]->setBounds(8 + (i % perRow) * kKnobW, y + kHeader + (i / perRow) * kKnobH, kKnobW,
                                                                kKnobH);
        }
        y += h + 8;
    }
    return y;
}

void KnobPanel::paint(juce::Graphics& g)
{
    for (const auto& s : sections)
    {
        g.setColour(theme::panel);
        g.fillRoundedRectangle(s.bounds.toFloat(), 6.0f);
        g.setColour(theme::textDim);
        g.setFont(juce::FontOptions(12.5f));
        g.drawText(s.title.toUpperCase(), s.bounds.reduced(12, 0).removeFromTop(kHeader), juce::Justification::centredLeft);
    }
}

} // namespace tf::app
