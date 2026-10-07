#include "TerrainView.h"

#include <cmath>

namespace tf::app::gui {

namespace {
constexpr float kSceneHit = 16.0f;
constexpr float kInset = 26.0f;
const std::array<juce::Colour, 4> kCloudTints { juce::Colour(0xff7fe6ff), juce::Colour(0xffffd9a0), juce::Colour(0xffc4b2ff), juce::Colour(0xffb4f08c) };

float ease(float dt, float tau) { return 1.0f - std::exp(-dt / tau); }
} // namespace

void showSceneMenu(Model& model, int scene)
{
    juce::PopupMenu m;
    m.addSectionHeader(juce::String(model.core.scenes.getScenes()[static_cast<std::size_t>(scene)].name));
    m.addItem(1, "Glide here");
    m.addItem(2, "Rename...");
    m.addItem(3, "Update with what you hear now");
    m.addItem(4, "Fold held controls into this scene");
    m.addSeparator();
    m.addItem(5, "Delete");
    m.showMenuAsync(juce::PopupMenu::Options(), [&model, scene](int r) {
        auto& sm = model.core.scenes;
        if (scene >= sm.size())
            return;
        const auto sc = sm.getScenes()[static_cast<std::size_t>(scene)];
        if (r == 1)
        {
            model.set(engine::P::TerrainX, sc.position.x);
            model.set(engine::P::TerrainY, sc.position.y);
        }
        else if (r == 2)
        {
            auto* w = new juce::AlertWindow("Rename scene", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", juce::String(sc.name));
            w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true, juce::ModalCallbackFunction::create([&model, scene, w](int ok) {
                                   if (ok == 1)
                                       model.core.scenes.renameScene(scene, w->getTextEditorContents("name").toStdString());
                               }),
                               true);
        }
        else if (r == 3)
        {
            // In place, so the scene keeps its index and colour.
            const auto& f = model.frame();
            for (engine::ParamIndex i = 0; i < engine::kNumParams; ++i)
                if ((model.registry.spec(i).flags & engine::ParamFlag::kTerrainBound) != 0)
                    sm.setSceneValue(scene, i, f.paramTargets[i]);
            sm.releaseLiveLayer();
        }
        else if (r == 4)
            sm.commitLiveLayer(scene, model.frame());
        else if (r == 5)
            sm.removeScene(scene);
    });
}

TerrainView::TerrainView(Model& m, bool present) : model(m), presentation(present)
{
    model.add(this);
    setOpaque(false);
    drawButton.setHelp(&model, "draw a loop on the terrain and the sound travels it by itself; Wander sets how closely (P)");
    drawButton.onClick = [this] { setDrawMode(! drawMode); };
    clearButton.setHelp(&model, "forget the drawn path; the sound wanders freely again");
    clearButton.onClick = [this] {
        model.core.paths.clear();
        if (juce::roundToInt(model.value(engine::P::TerrainWanderStyle)) == 4)
            model.set(engine::P::TerrainWanderStyle, 0.0f);
    };
    if (! presentation)
    {
        addAndMakeVisible(drawButton);
        addChildComponent(clearButton);
    }
    lastTime = juce::Time::getMillisecondCounterHiRes();
}

TerrainView::~TerrainView() { model.remove(this); }

juce::Rectangle<float> TerrainView::field() const { return getLocalBounds().toFloat(); }

juce::Point<float> TerrainView::toScreen(engine::Point2 t) const
{
    const auto f = field().reduced(kInset);
    return { f.getX() + t.x * f.getWidth(), f.getY() + (1.0f - t.y) * f.getHeight() };
}

engine::Point2 TerrainView::toTerrain(juce::Point<float> s) const
{
    const auto f = field().reduced(kInset);
    return { juce::jlimit(0.0f, 1.0f, (s.x - f.getX()) / f.getWidth()), juce::jlimit(0.0f, 1.0f, 1.0f - (s.y - f.getY()) / f.getHeight()) };
}

int TerrainView::sceneAt(juce::Point<float> s) const
{
    int best = -1;
    float bestD = kSceneHit;
    const auto& scenes = model.core.scenes.getScenes();
    for (int k = 0; k < static_cast<int>(scenes.size()); ++k)
    {
        const float d = s.getDistanceFrom(toScreen(scenes[static_cast<std::size_t>(k)].position));
        if (d < bestD)
        {
            bestD = d;
            best = k;
        }
    }
    return best;
}

juce::Colour TerrainView::soundColour() const
{
    // The blend of the scenes shaping the sound, so the light takes their colour.
    float r = 0.0f, g = 0.0f, b = 0.0f, total = 0.0f;
    for (std::size_t k = 0; k < shownWeights.size(); ++k)
    {
        const auto c = colour::forScene(static_cast<int>(k));
        const float w = shownWeights[k];
        r += c.getFloatRed() * w;
        g += c.getFloatGreen() * w;
        b += c.getFloatBlue() * w;
        total += w;
    }
    if (total < 1.0e-3f)
        return colour::tide;
    return juce::Colour::fromFloatRGBA(r / total, g / total, b / total, 1.0f);
}

void TerrainView::resized()
{
    auto r = getLocalBounds().reduced(8).removeFromTop(24);
    drawButton.setBounds(r.removeFromRight(86));
    r.removeFromRight(4);
    clearButton.setBounds(r.removeFromRight(86));
    renderBackdrop();
}

void TerrainView::setDrawMode(bool on)
{
    drawMode = on;
    drawButton.setToggleState(on, juce::dontSendNotification);
    drawButton.setButtonText(on ? "Drawing..." : "Draw path");
    setMouseCursor(on ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void TerrainView::renderBackdrop()
{
    const auto scale = juce::Component::getApproximateScaleFactorForComponent(this);
    const int w = std::max(1, getWidth()), h = std::max(1, getHeight());
    backdrop = juce::Image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(w) * scale), juce::roundToInt(static_cast<float>(h) * scale), true);
    juce::Graphics g(backdrop);
    g.addTransform(juce::AffineTransform::scale(scale));
    const auto f = field();
    // Deep water: lighter at the top, like looking down into a pool.
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff253842), f.getX(), f.getY(), juce::Colour(0xff151d23), f.getX(), f.getBottom(), false));
    g.fillRoundedRectangle(f, metric::radius + 2.0f);

    // A dot grid, like a pad surface.
    const auto inner = f.reduced(kInset);
    g.setColour(colour::wellLine);
    for (int i = 0; i <= 16; ++i)
        for (int j = 0; j <= 10; ++j)
        {
            const float x = inner.getX() + inner.getWidth() * static_cast<float>(i) / 16.0f;
            const float y = inner.getY() + inner.getHeight() * static_cast<float>(j) / 10.0f;
            const float s = (i % 4 == 0 && j % 5 == 0) ? 2.6f : 1.6f;
            g.fillEllipse(x - s * 0.5f, y - s * 0.5f, s, s);
        }
    // Gentle contour lines.
    g.setColour(colour::wellLine.withAlpha(0.6f));
    for (int k = 0; k < 9; ++k)
    {
        juce::Path p;
        const float y0 = inner.getY() + inner.getHeight() * (0.06f + static_cast<float>(k) * 0.11f);
        for (int i = 0; i <= 80; ++i)
        {
            const float u = static_cast<float>(i) / 80.0f;
            const float x = f.getX() + f.getWidth() * u;
            const float y = y0 + 7.0f * std::sin(u * 6.6f + static_cast<float>(k) * 0.8f) + 4.0f * std::sin(u * 15.0f + static_cast<float>(k));
            i == 0 ? p.startNewSubPath(x, y) : p.lineTo(x, y);
        }
        g.strokePath(p, juce::PathStrokeType(1.0f));
    }
}

void TerrainView::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = juce::jlimit(0.0f, 0.05f, static_cast<float>((now - lastTime) * 0.001));
    lastTime = now;
    const auto& f = model.frame();
    const auto& scenes = model.core.scenes.getScenes();
    const float tide = f.tide;

    const float audible = f.panicActive ? 0.0f : std::pow(f.fadeGain, 1.5f);
    energy += (audible - energy) * ease(dt, 0.6f);
    shownPos.x += (f.position.x - shownPos.x) * ease(dt, 0.12f);
    shownPos.y += (f.position.y - shownPos.y) * ease(dt, 0.12f);
    shownCursor.x += (f.cursor.x - shownCursor.x) * ease(dt, 0.06f);
    shownCursor.y += (f.cursor.y - shownCursor.y) * ease(dt, 0.06f);
    shownWeights.resize(scenes.size(), 0.0f);
    for (std::size_t k = 0; k < scenes.size(); ++k)
    {
        const float target = k < static_cast<std::size_t>(f.numScenes) ? f.sceneWeights[k] : 0.0f;
        shownWeights[k] += (target - shownWeights[k]) * ease(dt, 0.25f);
    }
    if (model.core.paths.getVersion() != shownPathVersion)
    {
        shownPathVersion = model.core.paths.getVersion();
        const auto loop = engine::TerrainPath::build(model.core.paths.getStroke(), 0);
        shownPath.assign(loop.points.begin(), loop.points.begin() + loop.count);
        clearButton.setVisible(loop.count > 0 && ! presentation);
    }
    tidePhase += dt * 0.12f * tide;
    orbit += dt * 0.35f * tide;

    const auto at = toScreen(shownPos);
    trailClock += dt;
    if (trailClock > 0.05f)
    {
        trailClock = 0.0f;
        trail.push_back(at);
        while (trail.size() > 50)
            trail.pop_front();
    }

    // Grains: particles emitted in proportion to each cloud's active grains.
    auto& rng = juce::Random::getSystemRandom();
    for (int c = 0; c < engine::kNumClouds; ++c)
    {
        const auto uc = static_cast<std::size_t>(c);
        emitCarry[uc] += static_cast<float>(f.cloudGrainCount[uc]) * dt * 1.4f * energy;
        while (emitCarry[uc] >= 1.0f && particles.size() < 260)
        {
            emitCarry[uc] -= 1.0f;
            Particle p;
            const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const float speed = 8.0f + rng.nextFloat() * 22.0f;
            p.p = at + juce::Point<float>(std::cos(a), std::sin(a)) * (4.0f + rng.nextFloat() * 10.0f);
            p.v = { std::cos(a) * speed, std::sin(a) * speed - 4.0f };
            p.life = 1.2f + rng.nextFloat() * 2.2f;
            p.size = 1.4f + rng.nextFloat() * 2.2f;
            p.colour = kCloudTints[uc];
            particles.push_back(p);
        }
        emitCarry[uc] = std::min(emitCarry[uc], 4.0f);
    }
    for (auto& p : particles)
    {
        p.age += dt;
        p.p += p.v * dt;
        p.v *= 1.0f - dt * 0.6f;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.age >= p.life; }), particles.end());

    // Ripples: resonator strikes (a mode jumping up) and new Bloom notes.
    for (std::size_t m = 0; m < lastModes.size(); ++m)
    {
        const float lv = f.modeLevel[m];
        if (lv > lastModes[m] * 1.6f + 0.02f && ripples.size() < 40)
            ripples.push_back({ at + juce::Point<float>(rng.nextFloat() * 40.0f - 20.0f, rng.nextFloat() * 40.0f - 20.0f), 0.0f,
                                juce::jlimit(0.2f, 1.0f, lv * 4.0f), colour::live });
        lastModes[m] = lv;
    }
    for (std::size_t v = 0; v < lastBloom.size(); ++v)
    {
        const bool active = f.bloomVoices[v].active;
        if (active && ! lastBloom[v] && ripples.size() < 40)
            ripples.push_back({ at + juce::Point<float>(rng.nextFloat() * 60.0f - 30.0f, rng.nextFloat() * 60.0f - 30.0f), 0.0f, 1.0f,
                                colour::forScene(static_cast<int>(v) + 2) });
        lastBloom[v] = active;
    }
    for (auto& r : ripples)
        r.age += dt;
    ripples.erase(std::remove_if(ripples.begin(), ripples.end(), [](const Ripple& r) { return r.age > 2.6f; }), ripples.end());

    repaint();
}

void TerrainView::paint(juce::Graphics& g)
{
    const auto f = field();
    if (backdrop.isValid())
        g.drawImage(backdrop, f, juce::RectanglePlacement::stretchToFit);
    juce::Graphics::ScopedSaveState clip(g);
    {
        juce::Path round;
        round.addRoundedRectangle(f, metric::radius + 2.0f);
        g.reduceClipRegion(round);
    }
    const auto& scenes = model.core.scenes.getScenes();
    const auto& fr = model.frame();
    const auto at = toScreen(shownPos);
    const auto tint = soundColour();
    const float big = std::max(f.getWidth(), f.getHeight());

    // Scene light: each scene glows by its share of the sound.
    for (std::size_t k = 0; k < scenes.size(); ++k)
    {
        const auto c = colour::forScene(static_cast<int>(k));
        const auto s = toScreen(scenes[k].position);
        const float w = k < shownWeights.size() ? shownWeights[k] : 0.0f;
        const float radius = big * (0.12f + 0.28f * w);
        juce::ColourGradient grad(c.withAlpha(0.10f + 0.35f * w * (0.4f + 0.6f * energy)), s.x, s.y, c.withAlpha(0.0f), s.x + radius, s.y, true);
        g.setGradientFill(grad);
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(s));
    }

    // The sound's own glow.
    {
        const float radius = big * (0.10f + 0.12f * energy);
        juce::ColourGradient grad(tint.withAlpha(0.22f + 0.33f * energy), at.x, at.y, tint.withAlpha(0.0f), at.x + radius, at.y, true);
        g.setGradientFill(grad);
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(at));
    }

    // Drawn path the sound follows.
    auto drawPath = [&](const std::vector<engine::Point2>& pts, juce::Colour c, float alpha, bool closed) {
        if (pts.size() < 2)
            return;
        juce::Path p;
        p.startNewSubPath(toScreen(pts.front()));
        for (std::size_t i = 1; i < pts.size(); ++i)
            p.lineTo(toScreen(pts[i]));
        if (closed)
            p.closeSubPath();
        g.setColour(c.withAlpha(alpha));
        const juce::PathStrokeType stroke(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (closed)
        {
            // The loop being followed: dashed. (The stroke being drawn is solid.)
            const float dashes[] = { 6.0f, 5.0f };
            juce::Path dashed;
            stroke.createDashedStroke(dashed, p, dashes, 2);
            g.fillPath(dashed);
        }
        else
            g.strokePath(p, stroke);
    };
    const bool following = juce::roundToInt(model.value(engine::P::TerrainWanderStyle)) == 4;
    drawPath(shownPath, colour::tide, following ? 0.7f : 0.3f, true);
    drawPath(drawing, colour::accent, 0.9f, false);

    // Trail of where the sound has been.
    for (std::size_t i = 1; i < trail.size(); ++i)
    {
        const float a = static_cast<float>(i) / static_cast<float>(trail.size());
        g.setColour(tint.withAlpha(0.28f * a * (0.3f + 0.7f * energy)));
        g.drawLine(juce::Line<float>(trail[i - 1], trail[i]), 1.0f + 2.0f * a);
    }

    // Ripples.
    for (const auto& r : ripples)
    {
        const float t = r.age / 2.6f;
        const float rad = 6.0f + 70.0f * std::sqrt(t);
        g.setColour(r.colour.withAlpha((1.0f - t) * 0.55f * r.strength));
        g.drawEllipse(juce::Rectangle<float>(rad * 2.0f, rad * 2.0f).withCentre(r.p), 1.4f);
    }

    // Grains.
    for (const auto& p : particles)
    {
        const float t = p.age / p.life;
        const float a = std::sin(t * juce::MathConstants<float>::pi) * 0.85f;
        g.setColour(p.colour.withAlpha(a));
        g.fillEllipse(juce::Rectangle<float>(p.size, p.size).withCentre(p.p));
    }

    // Drone voices: lights orbiting the sound.
    for (int v = 0; v < 6; ++v)
    {
        const float level = fr.droneVoiceLevel[static_cast<std::size_t>(v)] * energy;
        if (level < 0.01f)
            continue;
        const float ang = orbit * (0.6f + 0.13f * static_cast<float>(v)) + static_cast<float>(v) * 1.047f;
        const float rad = 22.0f + 9.0f * static_cast<float>(v);
        const auto p = at + juce::Point<float>(std::cos(ang) * rad, std::sin(ang) * rad * 0.7f);
        const float s = 3.0f + 4.0f * level;
        g.setColour(colour::text.withAlpha(0.25f + 0.6f * level));
        g.fillEllipse(juce::Rectangle<float>(s, s).withCentre(p));
    }

    // Performer's cursor: crosshair and ring (on the projector, only a faint ring).
    const auto cur = toScreen(shownCursor);
    if (! presentation)
    {
        g.setColour(colour::tide.withAlpha(0.22f));
        g.drawLine(f.getX(), cur.y, f.getRight(), cur.y, 1.0f);
        g.drawLine(cur.x, f.getY(), cur.x, f.getBottom(), 1.0f);
    }
    g.setColour(colour::tide.withAlpha(presentation ? 0.35f : 1.0f));
    g.drawEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(cur), 2.0f);

    // The sound.
    g.setColour(tint.withAlpha(0.5f));
    g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre(at));
    g.setColour(colour::text);
    g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(at));

    // Scenes: coloured markers with a weight ring and a name pill.
    for (std::size_t k = 0; k < scenes.size(); ++k)
    {
        const auto c = colour::forScene(static_cast<int>(k));
        const auto s = toScreen(scenes[k].position);
        const float w = k < shownWeights.size() ? shownWeights[k] : 0.0f;
        const bool hover = static_cast<int>(k) == hoverScene;
        g.setColour(colour::well);
        g.fillEllipse(juce::Rectangle<float>(20.0f, 20.0f).withCentre(s));
        g.setColour(c);
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre(s));
        if (w > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc(s.x, s.y, 13.0f, 13.0f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * w, true);
            g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (hover)
            g.drawEllipse(juce::Rectangle<float>(30.0f, 30.0f).withCentre(s), 1.5f);

        const auto name = juce::String(scenes[k].name);
        if (presentation)
        {
            // The audience sees places, not buttons: a faint name under the dot.
            g.setFont(font(13.0f, 500));
            g.setColour(c.withAlpha(0.35f + 0.5f * w));
            g.drawText(name, juce::Rectangle<float>(200.0f, 18.0f).withCentre(s.translated(0.0f, 24.0f)), juce::Justification::centred, false);
            continue;
        }
        g.setFont(font(11.0f, 600));
        const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name) + 14.0f;
        auto pill = juce::Rectangle<float>(tw, 18.0f).withCentre(s.translated(0.0f, 24.0f));
        if (pill.getBottom() > f.getBottom() - 2.0f) // no room below: label above the dot
            pill = pill.withCentre(s.translated(0.0f, -24.0f));
        pill = pill.withX(juce::jlimit(f.getX() + 2.0f, std::max(f.getX() + 2.0f, f.getRight() - pill.getWidth() - 2.0f), pill.getX()));
        g.setColour(c.withAlpha(hover ? 1.0f : 0.85f));
        g.fillRoundedRectangle(pill, 3.0f);
        g.setColour(colour::well);
        g.drawText(name, pill, juce::Justification::centred, false);
    }

    // Guidance.
    if (presentation)
        return;
    g.setFont(font(12.0f, 500));
    g.setColour(colour::textFaint);
    if (drawMode)
        g.drawText("Draw a loop: the sound will travel it on its own.", f.reduced(12.0f), juce::Justification::topLeft, true);
    else if (scenes.empty())
        g.drawText("Shape a sound, then double-click anywhere to place it here as a scene.", f.reduced(12.0f), juce::Justification::centredBottom, true);
}

void TerrainView::setCursorTo(juce::Point<float> s)
{
    const auto t = toTerrain(s);
    model.set(engine::P::TerrainX, t.x);
    model.set(engine::P::TerrainY, t.y);
}

void TerrainView::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.position;
    const int scene = sceneAt(pos);
    if (e.mods.isPopupMenu())
    {
        if (scene >= 0)
            showSceneMenu(model, scene);
        return;
    }
    if (drawMode)
    {
        drag = Drag::Path;
        drawing = { toTerrain(pos) };
        return;
    }
    if (scene >= 0)
    {
        drag = Drag::Scene;
        dragScene = scene;
        sceneMoved = false;
        return;
    }
    drag = Drag::Cursor;
    model.beginTouch(engine::P::TerrainX);
    model.beginTouch(engine::P::TerrainY);
    setCursorTo(pos);
}

void TerrainView::mouseDrag(const juce::MouseEvent& e)
{
    switch (drag)
    {
        case Drag::Cursor: setCursorTo(e.position); break;
        case Drag::Scene:
            if (e.getDistanceFromDragStart() > 3)
            {
                sceneMoved = true;
                model.core.scenes.moveScene(dragScene, toTerrain(e.position));
            }
            break;
        case Drag::Path:
        {
            const auto t = toTerrain(e.position);
            const auto& last = drawing.back();
            if (std::hypot(t.x - last.x, t.y - last.y) > 0.01f && drawing.size() < 256)
                drawing.push_back(t);
            break;
        }
        case Drag::None: break;
    }
}

void TerrainView::mouseUp(const juce::MouseEvent& e)
{
    if (drag == Drag::Cursor)
    {
        model.endTouch(engine::P::TerrainX);
        model.endTouch(engine::P::TerrainY);
    }
    else if (drag == Drag::Scene && ! sceneMoved && dragScene >= 0 && dragScene < model.core.scenes.size())
    {
        // A click on a scene glides there (Shift: arrive at once).
        const auto p = model.core.scenes.getScenes()[static_cast<std::size_t>(dragScene)].position;
        if (e.mods.isShiftDown())
        {
            const float glide = model.value(engine::P::TerrainGlide);
            model.set(engine::P::TerrainGlide, 0.05f);
            model.set(engine::P::TerrainX, p.x);
            model.set(engine::P::TerrainY, p.y);
            juce::Timer::callAfterDelay(120, [this, glide] { model.set(engine::P::TerrainGlide, glide); });
        }
        else
        {
            model.set(engine::P::TerrainX, p.x);
            model.set(engine::P::TerrainY, p.y);
        }
    }
    else if (drag == Drag::Path)
    {
        if (drawing.size() >= 3)
        {
            model.core.paths.set(drawing);
            // Follow it: Path style, and enough Wander to hear it.
            model.set(engine::P::TerrainWanderStyle, 4.0f);
            if (model.value(engine::P::TerrainWander) < 0.6f)
                model.set(engine::P::TerrainWander, 1.0f);
            model.core.status("The sound now travels your path. Wander sets how closely; Wander Rate how fast.");
        }
        drawing.clear();
        setDrawMode(false);
    }
    drag = Drag::None;
}

void TerrainView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (onDoubleClick)
        return onDoubleClick();
    if (drawMode || sceneAt(e.position) >= 0)
        return;
    if (model.core.scenes.captureScene({}, toTerrain(e.position), model.frame()) < 0)
        model.core.status("The terrain is full (32 scenes). Remove one to capture another.", true);
}

void TerrainView::mouseMove(const juce::MouseEvent& e)
{
    const int s = sceneAt(e.position);
    if (s != hoverScene)
    {
        hoverScene = s;
        setMouseCursor(s >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
        if (model.onHover)
            model.onHover(s >= 0 ? "Scene \"" + juce::String(model.core.scenes.getScenes()[static_cast<std::size_t>(s)].name)
                                       + "\": click to glide there, Shift-click to jump, drag to move, right-click for more"
                                 : juce::String("Terrain: drag to move the sound, double-click to capture it as a scene"));
    }
}

void TerrainView::mouseExit(const juce::MouseEvent&) { hoverScene = -1; }

} // namespace tf::app::gui
