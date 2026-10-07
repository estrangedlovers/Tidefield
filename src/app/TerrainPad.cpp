#include "TerrainPad.h"

namespace tf::app {

namespace {
const juce::Colour kPad { 0xff161a21 };
const juce::Colour kGrid { 0xff222833 };
const juce::Colour kScene { 0xffd9c38f };
const juce::Colour kCursor { 0xff7fb4c9 };
const juce::Colour kText { 0xffc9d1d9 };
constexpr float kSceneHitRadius = 14.0f;
} // namespace

TerrainPad::TerrainPad(engine::Engine& e, engine::SceneManager& s) : engine(e), scenes(s)
{
    setTooltip("Drag to move. Double-click to capture the current sound as a scene. "
               "Drag scenes to move them; right-click a scene for more.");
}

void TerrainPad::update(const engine::TelemetryFrame& f)
{
    frame = f;
    repaint();
}

juce::Rectangle<float> TerrainPad::padBounds() const
{
    const auto b = getLocalBounds().toFloat().reduced(4.0f);
    const float side = std::min(b.getWidth(), b.getHeight());
    return b.withSizeKeepingCentre(side, side);
}

juce::Point<float> TerrainPad::toScreen(engine::Point2 p) const
{
    const auto b = padBounds();
    return { b.getX() + p.x * b.getWidth(), b.getBottom() - p.y * b.getHeight() };
}

engine::Point2 TerrainPad::toTerrain(juce::Point<float> p) const
{
    const auto b = padBounds();
    return { juce::jlimit(0.0f, 1.0f, (p.x - b.getX()) / b.getWidth()),
             juce::jlimit(0.0f, 1.0f, (b.getBottom() - p.y) / b.getHeight()) };
}

int TerrainPad::sceneAt(juce::Point<float> screen) const
{
    const auto& list = scenes.getScenes();
    for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i)
        if (toScreen(list[static_cast<size_t>(i)].position).getDistanceFrom(screen) < kSceneHitRadius)
            return i;
    return -1;
}

void TerrainPad::moveCursor(juce::Point<float> screen)
{
    const auto t = toTerrain(screen);
    engine.setParam(engine::P::TerrainX, t.x);
    engine.setParam(engine::P::TerrainY, t.y);
}

void TerrainPad::paint(juce::Graphics& g)
{
    const auto b = padBounds();
    g.setColour(kPad);
    g.fillRoundedRectangle(b, 8.0f);

    g.setColour(kGrid);
    for (int i = 1; i < 4; ++i)
    {
        const float t = static_cast<float>(i) / 4.0f;
        g.drawLine(b.getX() + t * b.getWidth(), b.getY(), b.getX() + t * b.getWidth(), b.getBottom(), 1.0f);
        g.drawLine(b.getX(), b.getY() + t * b.getHeight(), b.getRight(), b.getY() + t * b.getHeight(), 1.0f);
    }

    // Scenes: halo grows with how much each one contributes right now.
    const auto& list = scenes.getScenes();
    const bool weightsCurrent = frame.numScenes == static_cast<int>(list.size());
    g.setFont(juce::FontOptions(12.0f));
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto c = toScreen(list[i].position);
        const float w = weightsCurrent ? frame.sceneWeights[i] : 0.0f;
        const float halo = 10.0f + 50.0f * std::sqrt(w);
        g.setColour(kScene.withAlpha(0.08f + 0.25f * w));
        g.fillEllipse(c.x - halo, c.y - halo, 2.0f * halo, 2.0f * halo);
        g.setColour(kScene.withAlpha(0.6f + 0.4f * w));
        g.fillEllipse(c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
        g.setColour(kText.withAlpha(0.75f));
        g.drawText(list[i].name, juce::Rectangle<float>(c.x - 60.0f, c.y + 8.0f, 120.0f, 16.0f), juce::Justification::centredTop);
    }

    // Performer's cursor (ring) and where the wander has actually taken the sound (dot).
    const auto cursor = toScreen(frame.cursor);
    const auto pos = toScreen(frame.position);
    g.setColour(kCursor.withAlpha(0.35f));
    g.drawLine(cursor.x, cursor.y, pos.x, pos.y, 1.0f);
    g.setColour(kCursor);
    g.drawEllipse(cursor.x - 9.0f, cursor.y - 9.0f, 18.0f, 18.0f, 1.5f);
    g.fillEllipse(pos.x - 4.0f, pos.y - 4.0f, 8.0f, 8.0f);

    if (list.empty())
    {
        g.setColour(kText.withAlpha(0.45f));
        g.setFont(juce::FontOptions(14.0f));
        g.drawFittedText("Shape a sound with the knobs,\nthen double-click here to place it as a scene.",
                         b.reduced(20.0f).removeFromBottom(60.0f).toNearestInt(), juce::Justification::centred, 3);
    }
}

void TerrainPad::mouseDown(const juce::MouseEvent& e)
{
    const int hit = sceneAt(e.position);
    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
            showSceneMenu(hit);
        return;
    }
    if (hit >= 0)
    {
        draggingScene = hit;
        return;
    }
    draggingCursor = true;
    moveCursor(e.position);
}

void TerrainPad::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingScene >= 0)
        scenes.moveScene(draggingScene, toTerrain(e.position));
    else if (draggingCursor)
        moveCursor(e.position);
}

void TerrainPad::mouseUp(const juce::MouseEvent&)
{
    draggingScene = -1;
    draggingCursor = false;
}

void TerrainPad::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (sceneAt(e.position) >= 0 || scenes.isFull())
        return;
    scenes.captureScene({}, toTerrain(e.position), frame);
    repaint();
}

void TerrainPad::showSceneMenu(int index)
{
    juce::PopupMenu menu;
    const bool hasLive = std::any_of(frame.live.begin(), frame.live.end(), [](auto v) { return v != 0; });
    menu.addItem(1, "Commit live layer to this scene", hasLive);
    menu.addItem(2, "Replace with current sound");
    menu.addItem(3, "Rename...");
    menu.addSeparator();
    menu.addItem(4, "Delete");

    juce::Component::SafePointer<TerrainPad> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, index](int result) {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        switch (result)
        {
            case 1: self.scenes.commitLiveLayer(index, self.frame); break;
            case 2:
            {
                const auto old = self.scenes.getScenes()[static_cast<size_t>(index)];
                self.scenes.removeScene(index);
                const int added = self.scenes.captureScene(old.name, old.position, self.frame);
                juce::ignoreUnused(added);
                break;
            }
            case 3:
            {
                auto* box = new juce::AlertWindow("Rename scene", {}, juce::MessageBoxIconType::NoIcon);
                box->addTextEditor("name", self.scenes.getScenes()[static_cast<size_t>(index)].name);
                box->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                box->enterModalState(true, juce::ModalCallbackFunction::create([safe, index, box](int r) {
                    if (r == 1 && safe != nullptr)
                        safe->scenes.renameScene(index, box->getTextEditorContents("name").toStdString());
                }), true);
                break;
            }
            case 4: self.scenes.removeScene(index); break;
            default: break;
        }
        self.repaint();
    });
}

} // namespace tf::app
