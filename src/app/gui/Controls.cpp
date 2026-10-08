#include "Controls.h"

namespace tf::app::gui {

namespace {
constexpr float kArcStart = juce::MathConstants<float>::pi * -0.75f;
constexpr float kArcEnd = juce::MathConstants<float>::pi * 0.75f;
constexpr float kDragPixels = 180.0f; // full range per vertical drag
} // namespace

// --- ParamComponent -------------------------------------------------------------------

ParamComponent::ParamComponent(Model& m, engine::P p, juce::String h) : model(m), param(p), help(std::move(h))
{
    label = model.name(p);
    model.add(this);
    setRepaintsOnMouseActivity(true);
}

ParamComponent::~ParamComponent()
{
    // Destroyed mid-drag (a page rebuilt under the mouse): let the value follow the
    // engine again instead of freezing at the last local value.
    if (dragging)
        model.endTouch(param);
    model.remove(this);
}

void ParamComponent::tick()
{
    const float v = model.value(param);
    const int flags = (model.isLive(param) ? 1 : 0) | (model.isLearning(param) ? 2 : 0) | ((model.pickup(param) + 1) << 2);
    // A value easing toward its target changes by invisible amounts for a long time:
    // repaint for visible moves at once, and catch up on the last digits a few times a
    // second, so the shown number always ends exact.
    const bool visible = std::abs(model.toNorm(param, v) - model.toNorm(param, lastValue)) > 5.0e-4f;
    const bool settle = v != lastValue && ++framesSincePaint >= 15;
    if (visible || settle || flags != lastFlags)
    {
        lastValue = v;
        lastFlags = flags;
        framesSincePaint = 0;
        repaint();
    }
}

juce::Colour ParamComponent::valueColour() const
{
    if (model.isLearning(param))
        return colour::learn();
    if (model.isLive(param))
        return colour::live();
    return colour::accent();
}

void ParamComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        model.showParamMenu(param, this);
        return;
    }
    if (e.mods.isAltDown())
    {
        model.release(param);
        return;
    }
    dragging = true;
    dragStartNorm = norm();
    model.beginTouch(param);
}

void ParamComponent::mouseUp(const juce::MouseEvent&)
{
    if (dragging)
        model.endTouch(param);
    dragging = false;
}

void ParamComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        model.resetToDefault(param);
}

void ParamComponent::mouseEnter(const juce::MouseEvent&)
{
    if (model.onHover)
        model.onHover(label + ": " + valueText() + (help.isNotEmpty() ? "   " + help : juce::String()));
}

void ParamComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY > 0.0f ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
    setNorm(norm() + step);
}

// --- Knob -----------------------------------------------------------------------------

void Knob::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool over = isMouseOverOrDragging();

    g.setFont(font(11.0f, 500));
    g.setColour(over ? colour::text() : colour::textDim());
    g.drawFittedText(label, r.removeFromTop(15.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

    auto valueArea = r.removeFromBottom(15.0f);
    const float size = std::min(r.getWidth(), r.getHeight()) - 4.0f;
    const auto dial = juce::Rectangle<float>(size, size).withCentre(r.getCentre());
    const float radius = size * 0.5f - 2.0f;
    const auto c = dial.getCentre();
    const float n = norm();
    const float angle = kArcStart + n * (kArcEnd - kArcStart);

    juce::Path track;
    track.addCentredArc(c.x, c.y, radius, radius, 0.0f, kArcStart, kArcEnd, true);
    g.setColour(colour::track());
    g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters fill from the centre.
    const bool bipolar = model.spec(param).minValue < 0.0f && model.spec(param).maxValue > 0.0f;
    const float from = bipolar ? 0.0f : kArcStart;
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, radius, radius, 0.0f, std::min(from, angle), std::max(from, angle), true);
    g.setColour(valueColour());
    g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Needle.
    g.setColour(colour::text());
    g.drawLine(juce::Line<float>(c.getPointOnCircumference(radius * 0.25f, angle), c.getPointOnCircumference(radius - 4.0f, angle)), 2.0f);

    // Soft takeover: which way to turn the controller.
    const int pickup = model.pickup(param);
    if (pickup != 0)
    {
        g.setColour(colour::learn());
        g.setFont(font(10.0f, 600));
        g.drawText(pickup > 0 ? juce::String::fromUTF8("\xe2\x96\xb2") : juce::String::fromUTF8("\xe2\x96\xbc"),
                   dial.withSizeKeepingCentre(12.0f, 12.0f).translated(radius + 4.0f, -radius), juce::Justification::centred);
    }

    g.setFont(font(11.0f, dragging ? 600 : 400));
    g.setColour(dragging ? colour::accent() : colour::text());
    g.drawText(formatter ? formatter(model.value(param)) : valueText(), valueArea, juce::Justification::centred, true);
}

void Knob::tick()
{
    ParamComponent::tick();
    if (formatter && isShowing())
        if (auto t = formatter(model.value(param)); t != lastText)
        {
            lastText = t;
            repaint();
        }
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
    setNorm(dragStartNorm - static_cast<float>(e.getDistanceFromDragStartY()) / kDragPixels * scale);
    if (model.onHover)
        model.onHover(label + ": " + (formatter ? formatter(model.value(param)) : valueText()));
}

// --- Fader ----------------------------------------------------------------------------

void Fader::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto nameArea = r.removeFromBottom(18.0f);
    auto valueArea = r.removeFromTop(18.0f);
    r.removeFromTop(4.0f);
    drawWell(g, r);

    const float n = norm();
    auto fill = r.reduced(3.0f);
    const float h = fill.getHeight() * n;
    auto filled = fill.removeFromBottom(h);
    const auto col = valueColour();
    g.setGradientFill(juce::ColourGradient(col.withAlpha(0.95f), filled.getX(), filled.getY(), col.withAlpha(0.45f), filled.getX(), filled.getBottom(), false));
    g.fillRoundedRectangle(filled, 2.0f);
    // Handle line.
    g.setColour(display::text());
    g.fillRect(juce::Rectangle<float>(filled.getX(), filled.getY() - 1.0f, filled.getWidth(), 2.0f));

    g.setFont(font(12.0f, 600));
    g.setColour(dragging ? colour::accent() : colour::text());
    g.drawText(valueText(), valueArea, juce::Justification::centred, true);
    g.setFont(font(11.5f, 500));
    g.setColour(isMouseOverOrDragging() ? colour::text() : colour::textDim());
    g.drawText(label, nameArea, juce::Justification::centred, true);
}

void Fader::mouseDown(const juce::MouseEvent& e)
{
    ParamComponent::mouseDown(e);
}

void Fader::mouseDrag(const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
    const float travel = std::max(60.0f, static_cast<float>(getHeight() - 40));
    setNorm(dragStartNorm - static_cast<float>(e.getDistanceFromDragStartY()) / travel * scale);
    if (model.onHover)
        model.onHover(label + ": " + valueText());
}

// --- Toggle ---------------------------------------------------------------------------

Toggle::Toggle(Model& m, engine::P p, juce::String t, juce::String h, juce::Colour on)
    : ParamComponent(m, p, std::move(h)), text(std::move(t)), onColour(on)
{
}

void Toggle::paint(juce::Graphics& g)
{
    const bool on = model.value(param) > 0.5f;
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(on ? onColour : (isMouseOver() ? colour::lift(colour::panelHi(), 0.08f) : colour::panelHi()));
    g.fillRoundedRectangle(r, metric::radius);
    g.setColour(on ? colour::well() : colour::text());
    g.setFont(font(12.0f, 600));
    g.drawText(text, r.reduced(6.0f, 0.0f), juce::Justification::centred, true);
}

void Toggle::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        model.showParamMenu(param, this);
        return;
    }
    model.toggle(param);
}

// --- Choice ---------------------------------------------------------------------------

Choice::Choice(Model& m, engine::P p, int cols, juce::String h) : ParamComponent(m, p, std::move(h)), columns(cols)
{
    items = model.choices(p);
    if (items.isEmpty())
    {
        const auto& s = model.spec(p);
        for (int v = static_cast<int>(s.minValue); v <= static_cast<int>(s.maxValue); ++v)
            items.add(juce::String(v));
    }
}

juce::Rectangle<int> Choice::cell(int i) const
{
    const int cols = columns > 0 ? columns : items.size();
    const int rows = (items.size() + cols - 1) / cols;
    const float w = static_cast<float>(getWidth()) / static_cast<float>(cols);
    const float h = static_cast<float>(getHeight()) / static_cast<float>(std::max(1, rows));
    const int c = i % cols, row = i / cols;
    return juce::Rectangle<float>(static_cast<float>(c) * w, static_cast<float>(row) * h, w, h).toNearestInt();
}

int Choice::indexAt(juce::Point<int> p) const
{
    for (int i = 0; i < items.size(); ++i)
        if (cell(i).contains(p))
            return i;
    return -1;
}

int Choice::preferredHeight(int) const
{
    const int cols = columns > 0 ? columns : items.size();
    const int rows = (items.size() + cols - 1) / cols;
    return rows * (captions.isEmpty() ? 24 : 38);
}

void Choice::paint(juce::Graphics& g)
{
    const int selected = juce::roundToInt(model.value(param) - model.spec(param).minValue);
    const auto mouse = getMouseXYRelative();
    for (int i = 0; i < items.size(); ++i)
    {
        const auto r = cell(i).toFloat().reduced(1.0f);
        const bool on = i == selected;
        const bool hover = isMouseOver() && cell(i).contains(mouse);
        g.setColour(on ? valueColour() : (hover ? colour::lift(colour::panelHi(), 0.08f) : colour::panelHi()));
        g.fillRoundedRectangle(r, metric::radius);
        g.setColour(on ? colour::well() : colour::text());
        if (captions.isEmpty())
        {
            g.setFont(font(11.5f, on ? 600 : 500));
            g.drawText(items[i], r.reduced(4.0f, 0.0f), juce::Justification::centred, true);
        }
        else
        {
            auto t = r.reduced(8.0f, 4.0f);
            g.setFont(font(12.0f, 600));
            g.drawText(items[i], t.removeFromTop(t.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
            g.setFont(font(10.0f));
            g.setColour(on ? colour::well().withAlpha(0.8f) : colour::textFaint());
            g.drawText(captions[i], t, juce::Justification::topLeft, true);
        }
    }
}

void Choice::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        model.showParamMenu(param, this);
        return;
    }
    const int i = indexAt(e.getPosition());
    if (i >= 0)
        model.set(param, model.spec(param).minValue + static_cast<float>(i));
}

// --- Pad ------------------------------------------------------------------------------

Pad::Pad(Model& m, juce::String t, juce::String s, juce::Colour c, juce::String h)
    : model(m), title(std::move(t)), sub(std::move(s)), help(std::move(h)), colour(c)
{
    model.add(this);
}

Pad::~Pad()
{
    // Destroyed while held (the window closed): end the gesture so it cannot latch on.
    if (pressed && onRelease)
        onRelease();
    model.remove(this);
}

void Pad::tick()
{
    const float l = level ? level() : 0.0f;
    const bool on = lit ? lit() : false;
    const auto s = subText ? subText() : sub;
    if (std::abs(l - shownLevel) > 0.004f || on != shownLit || s != shownSub)
    {
        shownLevel = l;
        shownLit = on;
        shownSub = s;
        repaint();
    }
}

void Pad::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = shownLit || pressed;
    const float corner = tileCorner(r);
    juce::Path tile;
    tile.addRoundedRectangle(r, corner);
    g.setColour(isMouseOver() ? colour::lift(colour::panelHi(), 0.06f) : colour::panelHi());
    g.fillPath(tile);

    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(tile);
        const float swell = std::min(4.0f, r.getHeight() * 0.06f);
        const float y = r.getBottom() - 5.0f - (r.getHeight() - 5.0f) * shownLevel;
        juce::Path surface;
        surface.startNewSubPath(r.getX() - 2.0f, y + swell);
        surface.cubicTo(r.getX() + r.getWidth() * 0.35f, y - swell, r.getX() + r.getWidth() * 0.6f, y + swell * 1.5f, r.getRight() + 2.0f, y - swell);
        auto water = surface;
        water.lineTo(r.getRight() + 2.0f, r.getBottom() + 2.0f);
        water.lineTo(r.getX() - 2.0f, r.getBottom() + 2.0f);
        water.closeSubPath();
        g.setColour(colour.withAlpha(0.2f + 0.45f * shownLevel));
        g.fillPath(water);
        g.setColour(colour.withAlpha(on || shownLevel > 0.01f ? 1.0f : 0.7f));
        g.strokePath(surface, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (on)
    {
        g.setColour(colour);
        g.drawRoundedRectangle(r.reduced(1.0f), corner - 1.0f, 2.0f);
    }

    auto t = r.reduced(10.0f, 7.0f);
    t.removeFromBottom(6.0f);
    if (keyCap.isNotEmpty())
    {
        g.setFont(font(10.0f, 600));
        const auto cap = juce::Rectangle<float>(16.0f, 16.0f).withPosition(t.getRight() - 16.0f, t.getY() + 1.0f);
        g.setColour(colour::textFaint().withAlpha(0.6f));
        g.drawRoundedRectangle(cap, 4.0f, 1.0f);
        g.setColour(colour::textDim());
        g.drawText(keyCap, cap, juce::Justification::centred, false);
    }
    g.setColour(colour::text());
    g.setFont(font(13.0f, 600));
    g.drawText(title, t.removeFromTop(t.getHeight() * 0.5f).withTrimmedRight(keyCap.isNotEmpty() ? 20.0f : 0.0f), juce::Justification::bottomLeft, true);
    g.setColour(on ? colour::text() : colour::textDim());
    g.setFont(font(10.5f, 500));
    g.drawText(shownSub.isNotEmpty() ? shownSub : sub, t, juce::Justification::topLeft, true);
}

void Pad::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
        return;
    }
    pressed = true;
    repaint();
    if (onPress)
        onPress();
}

void Pad::mouseUp(const juce::MouseEvent&)
{
    pressed = false;
    repaint();
    if (onRelease)
        onRelease();
}

void Pad::mouseEnter(const juce::MouseEvent&)
{
    if (model.onHover)
        model.onHover(title + ": " + help);
    repaint();
}

// --- FlatButton -----------------------------------------------------------------------

FlatButton::FlatButton(const juce::String& text, juce::Colour on) : juce::Button(text), onColour(on) {}

void FlatButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    auto bg = on ? onColour : colour::panelHi();
    if (down)
        bg = colour::lift(bg, 0.15f);
    else if (over)
        bg = colour::lift(bg, 0.07f);
    g.setColour(isEnabled() ? bg : bg.withAlpha(0.4f));
    g.fillRoundedRectangle(r, metric::radius);
    g.setColour(on ? colour::well() : colour::text());
    g.setFont(font(12.0f, 600));
    g.drawText(getButtonText(), r.reduced(6.0f, 0.0f), juce::Justification::centred, true);
}

void FlatButton::mouseEnter(const juce::MouseEvent& e)
{
    juce::Button::mouseEnter(e);
    if (model != nullptr && model->onHover && help.isNotEmpty())
        model->onHover(getButtonText() + ": " + help);
}

} // namespace tf::app::gui
