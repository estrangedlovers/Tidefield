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
    // Flow layout: small sections sit side by side; a section with more knobs than fit
    // across takes the full width and wraps its knobs.
    constexpr int kGap = 8;
    int x = 0, y = 0, rowHeight = 0;
    for (auto& s : sections)
    {
        std::vector<ParamKnob*> shown; // hidden knobs (e.g. an empty FX slot) take no space
        for (auto* k : s.knobs)
            if (k->isVisible())
                shown.push_back(k);
        const int count = static_cast<int>(shown.size());

        int headerWidth = 170;
        for (const auto& [comp, w] : s.header)
            headerWidth += w + 6;
        const int natural = std::max(count * kKnobW + 16, headerWidth);
        const int w = std::min(width, natural);
        const int perRow = std::max(1, (w - 16) / kKnobW);
        const int rows = (count + perRow - 1) / perRow;
        const int h = kHeader + rows * kKnobH + (rows > 0 ? 8 : 0);

        if (x > 0 && x + w > width)
        {
            x = 0;
            y += rowHeight + kGap;
            rowHeight = 0;
        }

        if (apply)
        {
            s.bounds = { x, y, w, h };
            int hx = x + w - 8;
            for (auto& [comp, cw] : s.header)
            {
                hx -= cw;
                comp->setBounds(hx, y + 3, cw, kHeader - 6);
                hx -= 6;
            }
            for (int i = 0; i < count; ++i)
                shown[static_cast<std::size_t>(i)]->setBounds(x + 8 + (i % perRow) * kKnobW, y + kHeader + (i / perRow) * kKnobH, kKnobW,
                                                              kKnobH);
        }
        x += w + kGap;
        rowHeight = std::max(rowHeight, h);
    }
    return y + rowHeight;
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
