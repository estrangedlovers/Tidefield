#pragma once

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <array>

namespace tf::app::gui::logo {
inline const juce::Colour sky { 0xffc6dcdb };
inline const juce::Colour mist { 0xffa2bfbc };
inline const juce::Colour sage { 0xff92ab9f };
inline const juce::Colour sea { 0xff6e9091 };
inline const juce::Colour deep { 0xff4a6368 };
inline const juce::Colour foam { 0xfff6f5ef };
inline const juce::Colour ink { 0xff4a6368 };

inline constexpr float kCorner = 0.225f;

namespace detail {
struct Pt { float x, y; };
struct Line { Pt start; std::array<Pt, 9> seg; int numSegs; };

inline constexpr std::array<Line, 4> kLines { {
    { { -0.05f, 0.37f }, { { { 0.22f, 0.29f }, { 0.42f, 0.38f }, { 0.57f, 0.355f },
                             { 0.72f, 0.33f }, { 0.80f, 0.245f }, { 1.05f, 0.25f } } }, 2 },
    { { -0.05f, 0.52f }, { { { 0.20f, 0.40f }, { 0.44f, 0.36f }, { 0.57f, 0.37f },
                             { 0.68f, 0.38f }, { 0.70f, 0.47f }, { 0.78f, 0.50f },
                             { 0.86f, 0.53f }, { 0.95f, 0.515f }, { 1.05f, 0.52f } } }, 3 },
    { { -0.05f, 0.64f }, { { { 0.22f, 0.52f }, { 0.44f, 0.45f }, { 0.55f, 0.46f },
                             { 0.63f, 0.47f }, { 0.58f, 0.60f }, { 0.67f, 0.65f },
                             { 0.76f, 0.69f }, { 0.92f, 0.675f }, { 1.05f, 0.68f } } }, 3 },
    { { -0.05f, 0.76f }, { { { 0.25f, 0.62f }, { 0.47f, 0.64f }, { 0.60f, 0.76f },
                             { 0.70f, 0.85f }, { 0.84f, 0.95f }, { 1.05f, 0.97f } } }, 2 },
} };

inline juce::Path linePath(const Line& l, juce::Rectangle<float> r)
{
    auto at = [&](Pt p) { return juce::Point<float>(r.getX() + p.x * r.getWidth(), r.getY() + p.y * r.getHeight()); };
    juce::Path p;
    p.startNewSubPath(at(l.start));
    for (int i = 0; i < l.numSegs; ++i)
    {
        const auto k = static_cast<std::size_t>(i * 3);
        p.cubicTo(at(l.seg[k]), at(l.seg[k + 1]), at(l.seg[k + 2]));
    }
    return p;
}
}

inline juce::Path outline(juce::Rectangle<float> r)
{
    const float s = std::min(r.getWidth(), r.getHeight());
    const auto sq = r.withSizeKeepingCentre(s, s);
    juce::Path p;
    p.addRoundedRectangle(sq, s * kCorner);
    return p;
}

inline void drawMark(juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = std::min(r.getWidth(), r.getHeight());
    const auto sq = r.withSizeKeepingCentre(s, s);
    const std::array<juce::Colour, 4> below { mist, sage, sea, deep };

    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(outline(sq));
    g.setColour(sky);
    g.fillRect(sq);
    for (std::size_t i = 0; i < detail::kLines.size(); ++i)
    {
        auto p = detail::linePath(detail::kLines[i], sq);
        p.lineTo(sq.getRight() + s, sq.getBottom() + s);
        p.lineTo(sq.getX() - s, sq.getBottom() + s);
        p.closeSubPath();
        g.setColour(below[i]);
        g.fillPath(p);
    }
    const float stroke = std::max(1.0f, s * 0.012f);
    g.setColour(foam);
    for (const auto& l : detail::kLines)
        g.strokePath(detail::linePath(l, sq), juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}
