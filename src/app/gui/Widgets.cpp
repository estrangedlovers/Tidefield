#include "Widgets.h"

#include <cmath>

namespace tf::app::gui {

namespace {
float meterNorm(float linear)
{
    // -60..+3 dB onto 0..1, with more room at the top where it matters.
    const float db = juce::Decibels::gainToDecibels(linear, -60.0f);
    return std::pow(juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 63.0f), 1.6f);
}
} // namespace

// --- Meter ----------------------------------------------------------------------------

Meter::Meter(Model& m, int s, bool h) : model(m), strip(s), horizontal(h) { model.add(this); }
Meter::~Meter() { model.remove(this); }

void Meter::tick()
{
    const auto& f = model.frame();
    const float l = strip < 0 ? f.peakL : f.stripPeakL[static_cast<std::size_t>(strip)];
    const float r = strip < 0 ? f.peakR : f.stripPeakR[static_cast<std::size_t>(strip)];
    auto follow = [](float& shown, float& hold, int& frames, float v) {
        shown = v > shown ? v : shown * 0.86f + v * 0.14f; // fast up, smooth fall
        if (v >= hold)
        {
            hold = v;
            frames = 50;
        }
        else if (--frames < 0)
            hold *= 0.94f;
    };
    const float pl = shownL, pr = shownR;
    follow(shownL, holdL, holdFramesL, l);
    follow(shownR, holdR, holdFramesR, r);
    if (std::abs(pl - shownL) > 1.0e-4f || std::abs(pr - shownR) > 1.0e-4f || holdFramesL >= 0)
        repaint();
}

void Meter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawWell(g, r);
    r = r.reduced(2.0f);
    auto lane = [&](juce::Rectangle<float> a, float v, float hold) {
        const float n = meterNorm(v), hn = meterNorm(hold);
        const auto col = v > 0.89f ? colour::warn : (v > 0.5f ? colour::live : colour::good);
        g.setColour(col.withAlpha(0.9f));
        if (horizontal)
        {
            g.fillRect(a.withWidth(a.getWidth() * n));
            g.setColour(colour::text.withAlpha(0.7f));
            g.fillRect(juce::Rectangle<float>(a.getX() + a.getWidth() * hn - 1.0f, a.getY(), 1.5f, a.getHeight()));
        }
        else
        {
            g.fillRect(a.withTop(a.getBottom() - a.getHeight() * n));
            g.setColour(colour::text.withAlpha(0.7f));
            g.fillRect(juce::Rectangle<float>(a.getX(), a.getBottom() - a.getHeight() * hn, a.getWidth(), 1.5f));
        }
    };
    if (horizontal)
    {
        auto top = r.removeFromTop(r.getHeight() * 0.5f);
        lane(top.withTrimmedBottom(0.5f), shownL, holdL);
        lane(r.withTrimmedTop(0.5f), shownR, holdR);
    }
    else
    {
        auto left = r.removeFromLeft(r.getWidth() * 0.5f);
        lane(left.withTrimmedRight(0.5f), shownL, holdL);
        lane(r.withTrimmedLeft(0.5f), shownR, holdR);
    }
}

// --- Waveform -------------------------------------------------------------------------

Waveform::Waveform(Model& m, int s, juce::Colour c) : model(m), slot(s), colour(c)
{
    model.add(this);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

Waveform::~Waveform() { model.remove(this); }

void Waveform::rebuildPeaks()
{
    peaks.assign(240, 0.0f);
    if (shown == nullptr || shown->size() == 0)
        return;
    const auto n = shown->size();
    float overall = 1.0e-6f;
    for (std::size_t b = 0; b < peaks.size(); ++b)
    {
        const auto from = n * b / peaks.size();
        const auto to = std::max(from + 1, n * (b + 1) / peaks.size());
        const auto step = std::max<std::size_t>(1, (to - from) / 256); // sparse read of long files
        float p = 0.0f;
        for (auto i = from; i < to; i += step)
            p = std::max({ p, std::fabs(shown->left[i]), shown->isStereo() ? std::fabs(shown->right[i]) : 0.0f });
        peaks[b] = p;
        overall = std::max(overall, p);
    }
    for (auto& p : peaks)
        p /= overall;
}

void Waveform::tick()
{
    if (! isShowing())
        return;
    const auto current = slot == engine::kNumClouds ? model.engine.getBloomSample() : model.engine.getCloudSample(slot);
    if (current != shown)
    {
        shown = current;
        rebuildPeaks();
    }
    repaint();
}

void Waveform::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawWell(g, r);
    r = r.reduced(4.0f);
    if (shown == nullptr)
    {
        g.setFont(font(11.5f, 500));
        g.setColour(isMouseOver() ? colour::text : colour::textFaint);
        g.drawText("Click to load a sound", r, juce::Justification::centred);
        return;
    }
    const float mid = r.getCentreY();
    const float w = r.getWidth() / static_cast<float>(peaks.size());
    g.setColour(colour.withAlpha(0.55f));
    for (std::size_t b = 0; b < peaks.size(); ++b)
    {
        const float h = std::max(1.0f, peaks[b] * r.getHeight() * 0.48f);
        g.fillRect(r.getX() + static_cast<float>(b) * w, mid - h, std::max(1.0f, w - 0.5f), h * 2.0f);
    }

    const auto& f = model.frame();
    if (slot < engine::kNumClouds)
    {
        const auto s = static_cast<std::size_t>(slot);
        const int views = f.cloudGrainViews[s];
        for (int k = 0; k < views; ++k)
        {
            const auto& gv = f.cloudGrains[s][static_cast<std::size_t>(k)];
            const float x = r.getX() + gv.position * r.getWidth();
            const float y = mid - gv.pan * r.getHeight() * 0.35f;
            const float a = juce::jlimit(0.15f, 1.0f, gv.amplitude * 2.0f);
            g.setColour(colour::text.withAlpha(a));
            g.fillEllipse(x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }
        // Where the cloud is reading from.
        const float pos = model.value(static_cast<engine::P>(engine::idx(engine::kCloudFirstParam[s]) + 2));
        g.setColour(colour::accent);
        g.fillRect(r.getX() + pos * r.getWidth() - 0.75f, r.getY(), 1.5f, r.getHeight());
    }
    else
    {
        const float pos = model.value(engine::P::BloomPosition);
        g.setColour(colour::accent);
        g.fillRect(r.getX() + pos * r.getWidth() - 0.75f, r.getY(), 1.5f, r.getHeight());
    }

    const auto caption = juce::String(shown->name) + "  " + juce::String(shown->seconds(), 1) + " s";
    g.setFont(font(10.5f, 600));
    const auto pill = juce::Rectangle<float>(r.getX(), r.getY(), juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), caption) + 12.0f, 16.0f);
    g.setColour(colour::well.withAlpha(0.8f));
    g.fillRoundedRectangle(pill, 3.0f);
    g.setColour(colour::text);
    g.drawText(caption, pill, juce::Justification::centred, false);
}

void Waveform::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem(1, "Load a sound...");
        m.addItem(2, "Clear", shown != nullptr);
        m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
            if (r == 1 && onLoad)
                onLoad(slot);
            else if (r == 2)
                slot == engine::kNumClouds ? model.engine.loadBloomSample(nullptr) : model.engine.loadCloudSample(slot, nullptr);
        });
        return;
    }
    if (onLoad)
        onLoad(slot);
}

void Waveform::mouseEnter(const juce::MouseEvent&)
{
    if (model.onHover)
        model.onHover("Sample: click to load a sound (or drop one from the browser), right-click to clear. Dots are grains reading it.");
}

// --- ShapePad -------------------------------------------------------------------------

ShapePad::ShapePad(Model& m) : model(m)
{
    model.add(this);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

ShapePad::~ShapePad() { model.remove(this); }

void ShapePad::tick()
{
    const float c = model.value(engine::P::PerformColour), s = model.value(engine::P::PerformSpace);
    if (c != shownC || s != shownS)
    {
        shownC = c;
        shownS = s;
        repaint();
    }
}

void ShapePad::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawWell(g, r);
    r = r.reduced(1.0f);
    // A soft gradient: dark to bright left to right, close to far bottom to top.
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x332c4a8a), r.getX(), r.getCentreY(), juce::Colour(0x33ffcf7a), r.getRight(), r.getCentreY(), false));
    g.fillRoundedRectangle(r, metric::radius);
    g.setColour(colour::wellLine);
    g.drawLine(r.getCentreX(), r.getY() + 4.0f, r.getCentreX(), r.getBottom() - 4.0f, 1.0f);
    g.drawLine(r.getX() + 4.0f, r.getCentreY(), r.getRight() - 4.0f, r.getCentreY(), 1.0f);

    g.setFont(font(9.5f, 600));
    g.setColour(colour::textFaint);
    g.drawText("DARK", r.reduced(5.0f), juce::Justification::centredLeft);
    g.drawText("BRIGHT", r.reduced(5.0f), juce::Justification::centredRight);
    g.drawText("FAR", r.reduced(5.0f), juce::Justification::centredTop);
    g.drawText("CLOSE", r.reduced(5.0f), juce::Justification::centredBottom);

    const juce::Point<float> p { r.getX() + (shownC + 1.0f) * 0.5f * r.getWidth(), r.getY() + (1.0f - shownS) * 0.5f * r.getHeight() };
    const bool live = model.isLive(engine::P::PerformColour) || model.isLive(engine::P::PerformSpace);
    const auto c = live ? colour::live : colour::accent;
    g.setColour(c.withAlpha(0.25f));
    g.fillEllipse(juce::Rectangle<float>(26.0f, 26.0f).withCentre(p));
    g.setColour(c);
    g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre(p));
}

void ShapePad::setFrom(juce::Point<float> p)
{
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    const float x = juce::jlimit(0.0f, 1.0f, (p.x - r.getX()) / r.getWidth());
    const float y = juce::jlimit(0.0f, 1.0f, (p.y - r.getY()) / r.getHeight());
    model.set(engine::P::PerformColour, x * 2.0f - 1.0f);
    model.set(engine::P::PerformSpace, 1.0f - y * 2.0f);
}

void ShapePad::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        model.showParamMenu(e.position.x < static_cast<float>(getWidth()) * 0.5f ? engine::P::PerformColour : engine::P::PerformSpace);
        return;
    }
    model.beginTouch(engine::P::PerformColour);
    model.beginTouch(engine::P::PerformSpace);
    setFrom(e.position);
}

void ShapePad::mouseDrag(const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        setFrom(e.position);
}

void ShapePad::mouseUp(const juce::MouseEvent&)
{
    model.endTouch(engine::P::PerformColour);
    model.endTouch(engine::P::PerformSpace);
}

void ShapePad::mouseDoubleClick(const juce::MouseEvent&)
{
    model.set(engine::P::PerformColour, 0.0f);
    model.set(engine::P::PerformSpace, 0.0f);
}

void ShapePad::mouseEnter(const juce::MouseEvent&)
{
    if (model.onHover)
        model.onHover("Shape: left darkens everything, right brightens; up pushes it far into the reverb, down pulls it close and dry. Double-click recentres.");
}

// --- KeyboardStrip --------------------------------------------------------------------

KeyboardStrip::KeyboardStrip(Model& m) : model(m) { model.add(this); }
KeyboardStrip::~KeyboardStrip() { model.remove(this); }

void KeyboardStrip::tick()
{
    std::array<float, 128> next {};
    for (const auto& v : model.frame().bloomVoices)
        if (v.active)
        {
            const int n = juce::roundToInt(v.note);
            if (n >= 0 && n < 128)
                next[static_cast<std::size_t>(n)] = std::max(next[static_cast<std::size_t>(n)], juce::jlimit(0.3f, 1.0f, v.level * 3.0f));
        }
    if (next != lit)
    {
        lit = next;
        repaint();
    }
}

juce::Rectangle<float> KeyboardStrip::keyRect(int note) const
{
    const auto r = getLocalBounds().toFloat();
    const int whites = numOctaves * 7 + 1;
    const float ww = r.getWidth() / static_cast<float>(whites);
    static constexpr int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const int rel = note - lowNote;
    const int oct = rel / 12, n = rel % 12;
    const float x = r.getX() + static_cast<float>(oct * 7 + whiteIndex[n]) * ww;
    if (! isBlack(note))
        return { x, r.getY(), ww, r.getHeight() };
    return { x + ww * 0.68f, r.getY(), ww * 0.64f, r.getHeight() * 0.6f };
}

int KeyboardStrip::noteAt(juce::Point<float> p) const
{
    const int high = lowNote + numOctaves * 12;
    for (int n = lowNote; n <= high; ++n)
        if (isBlack(n) && keyRect(n).contains(p))
            return n;
    for (int n = lowNote; n <= high; ++n)
        if (! isBlack(n) && keyRect(n).contains(p))
            return n;
    return -1;
}

void KeyboardStrip::paint(juce::Graphics& g)
{
    const int high = lowNote + numOctaves * 12;
    const auto& f = model.frame();
    const int root = f.harmonyRoot;
    auto draw = [&](int n) {
        const auto k = keyRect(n).reduced(0.5f);
        const bool black = isBlack(n);
        const float l = lit[static_cast<std::size_t>(n)];
        auto base = black ? colour::well : juce::Colour(0xffdcdbd5);
        if (n % 12 == root)
            base = black ? base.brighter(0.25f) : base.darker(0.06f);
        g.setColour(l > 0.0f ? base.interpolatedWith(colour::accent, 0.45f + 0.55f * l) : base);
        g.fillRoundedRectangle(k, 2.0f);
        if (n == heldNote)
        {
            g.setColour(colour::tide);
            g.drawRoundedRectangle(k.reduced(1.0f), 2.0f, 2.0f);
        }
        if (! black && n % 12 == 0)
        {
            g.setColour(colour::well.withAlpha(0.6f));
            g.setFont(font(9.5f, 600));
            g.drawText(Model::noteName(static_cast<float>(n)), k.withTop(k.getBottom() - 14.0f), juce::Justification::centred);
        }
    };
    for (int n = lowNote; n <= high; ++n)
        if (! isBlack(n))
            draw(n);
    for (int n = lowNote; n <= high; ++n)
        if (isBlack(n))
            draw(n);
}

void KeyboardStrip::mouseDown(const juce::MouseEvent& e)
{
    heldNote = noteAt(e.position);
    if (heldNote >= 0)
    {
        // Lower on the key plays louder, the way a key is struck.
        const auto k = keyRect(heldNote);
        const float vel = juce::jlimit(0.25f, 1.0f, 0.35f + 0.65f * (e.position.y - k.getY()) / k.getHeight());
        model.engine.noteOn(heldNote, vel);
    }
    repaint();
}

void KeyboardStrip::mouseDrag(const juce::MouseEvent& e)
{
    const int n = noteAt(e.position);
    if (n != heldNote)
    {
        if (heldNote >= 0)
            model.engine.noteOff(heldNote);
        heldNote = n;
        if (n >= 0)
            model.engine.noteOn(n, 0.7f);
        repaint();
    }
}

void KeyboardStrip::mouseUp(const juce::MouseEvent&)
{
    if (heldNote >= 0)
        model.engine.noteOff(heldNote);
    heldNote = -1;
    repaint();
}

} // namespace tf::app::gui
