#include "Timeline.h"

#include "Style.h"

#include <cmath>

namespace tf::app::gui {
namespace {
constexpr int kBarH = 26, kRulerH = 16, kLaneH = 22, kNameW = 150;
using Lane = io::Performance::Lane;
using Kind = io::Performance::LaneKind;
using State = PerformanceController::State;

juce::String clock(double s)
{
    const int minutes = static_cast<int>(s / 60.0);
    return juce::String(minutes) + ":" + juce::String(s - minutes * 60.0, 1).paddedLeft('0', 4);
}
}

TimelineView::TimelineView(Model& m)
    : model(m), perf(m.core.performance), recordButton("Record", colour::live()), playButton("Play"), stopButton("Stop"), eraseButton("Erase"),
      muteButton("Mute lane"), smoothButton("Smooth"), trimButton("Trim"), clearButton("Clear"), saveButton("Save"), renderButton("Render")
{
    model.add(this);
    recordButton.setHelp(&model, "record everything you play and move from now on, starting from the sound as it is");
    playButton.setHelp(&model, "play the performance from the start of the selection; double-click the lanes to play from there");
    stopButton.setHelp(&model, "stop recording or playing");
    eraseButton.setHelp(&model, "remove the moves inside the selection, in the selected lane or in every lane");
    muteButton.setHelp(&model, "silence the selected lane on playback and in renders without deleting it");
    smoothButton.setHelp(&model, "soften the selected lane's movement; press again to smooth further");
    trimButton.setHelp(&model, "keep only the selection, so the performance starts and ends there");
    clearButton.setHelp(&model, "throw the performance away (undo brings it back)");
    saveButton.setHelp(&model, "save the performance and the sound it started from as a session; opening it loads the timeline");
    renderButton.setHelp(&model, "play the performance offline into WAV files: master, stems or a seamless loop");

    recordButton.onClick = [this] { perf.record(); };
    playButton.onClick = [this] { perf.play(perf.selection.from); };
    stopButton.onClick = [this] { perf.stop(); };
    eraseButton.onClick = [this] {
        const auto sel = perf.selection;
        if (! sel.hasRange())
            return model.core.status("Drag across the lanes to select a stretch of time first.", true);
        perf.edit("Erase", [sel](io::Performance& p) { p.erase(sel.from, sel.to, sel.lane ? &*sel.lane : nullptr); });
    };
    muteButton.onClick = [this] {
        if (! perf.selection.lane)
            return model.core.status("Click a lane name to choose a lane first.", true);
        const auto lane = *perf.selection.lane;
        perf.edit("Mute lane", [lane](io::Performance& p) { p.setMuted(lane, ! p.isMuted(lane)); });
        perf.selection.lane = lane;
    };
    smoothButton.onClick = [this] {
        if (! perf.selection.lane || perf.selection.lane->kind != Kind::Param)
            return model.core.status("Click the name of a control's lane to smooth it.", true);
        const auto lane = *perf.selection.lane;
        perf.edit("Smooth lane", [lane](io::Performance& p) { p.smooth(lane, 0.5); });
        perf.selection.lane = lane;
    };
    trimButton.onClick = [this] {
        const auto sel = perf.selection;
        if (! sel.hasRange())
            return model.core.status("Drag across the lanes to select what to keep first.", true);
        perf.edit("Trim", [sel](io::Performance& p) { p.trim(sel.from, sel.to); });
    };
    clearButton.onClick = [this] { perf.clear(); };
    saveButton.onClick = [this] { perf.save(); };
    renderButton.onClick = [this] {
        if (perf.isRendering())
            perf.cancelRender();
        else
            showRenderMenu();
    };
    for (auto* b : { &recordButton, &playButton, &stopButton, &eraseButton, &muteButton, &smoothButton, &trimButton, &clearButton, &saveButton, &renderButton })
        addAndMakeVisible(*b);
    refreshButtons();
}

TimelineView::~TimelineView() { model.remove(this); }

void TimelineView::showRenderMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader("Render the performance");
    m.addItem(1, "Master");
    m.addItem(2, "Master and stems");
    m.addSeparator();
    m.addSectionHeader("Seamless loop");
    m.addItem(3, "Loop with a 2 s crossfade");
    m.addItem(4, "Loop with an 8 s crossfade");
    showMenu(m, this, [this](int r) {
        if (r == 1 || r == 2)
            perf.render(r == 2, 0.0);
        else if (r == 3 || r == 4)
            perf.render(false, r == 3 ? 2.0 : 8.0);
    });
}

void TimelineView::refreshButtons()
{
    const auto state = perf.getState();
    const bool has = ! perf.get().empty();
    recordButton.setToggleState(state == State::Recording, juce::dontSendNotification);
    playButton.setToggleState(state == State::Playing, juce::dontSendNotification);
    playButton.setEnabled(has && state != State::Recording);
    stopButton.setEnabled(state != State::Idle);
    const bool editable = has && state != State::Recording;
    for (auto* b : { &eraseButton, &muteButton, &smoothButton, &trimButton, &clearButton, &saveButton })
        b->setEnabled(editable);
    renderButton.setEnabled(editable || perf.isRendering());
    renderButton.setButtonText(perf.isRendering() ? "Cancel " + juce::String(juce::roundToInt(perf.getRenderProgress() * 100.0f)) + "%" : juce::String("Render"));
    muteButton.setToggleState(perf.selection.lane && perf.get().isMuted(*perf.selection.lane), juce::dontSendNotification);
}

void TimelineView::tick()
{
    const int state = static_cast<int>(perf.getState());
    const float position = static_cast<float>(perf.getPosition());
    const float progress = perf.isRendering() ? perf.getRenderProgress() : -1.0f;
    const auto count = perf.get().events.size();
    const bool content = perf.getRevision() != shownRevision || count != shownCount;
    if (! content && state == shownState && std::abs(position - shownPosition) < 0.04f && std::abs(progress - shownProgress) < 0.005f)
        return;
    if (content)
    {
        lanes = perf.get().lanes();
        laneOffset = juce::jlimit(0, std::max(0, static_cast<int>(lanes.size()) - visibleLanes()), laneOffset);
    }
    shownRevision = perf.getRevision();
    shownCount = count;
    shownState = state;
    shownPosition = position;
    shownProgress = progress;
    refreshButtons();
    repaint();
}

void TimelineView::resized()
{
    auto bar = getLocalBounds().removeFromTop(kBarH - 4);
    auto place = [&bar](juce::Button& b, int w) {
        b.setBounds(bar.removeFromLeft(w));
        bar.removeFromLeft(4);
    };
    place(recordButton, 70);
    place(playButton, 56);
    place(stopButton, 56);
    bar.removeFromLeft(12);
    place(eraseButton, 60);
    place(muteButton, 78);
    place(smoothButton, 66);
    place(trimButton, 52);
    place(clearButton, 56);
    bar.removeFromLeft(12);
    place(saveButton, 56);
    place(renderButton, 92);
}

juce::Rectangle<float> TimelineView::rulerArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop(static_cast<float>(kBarH)).removeFromTop(static_cast<float>(kRulerH)).withTrimmedLeft(static_cast<float>(kNameW));
}

juce::Rectangle<float> TimelineView::laneArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop(static_cast<float>(kBarH + kRulerH));
}

int TimelineView::visibleLanes() const { return std::max(1, static_cast<int>(laneArea().getHeight()) / kLaneH); }

double TimelineView::totalSeconds() const { return std::max(1.0, std::max(perf.get().seconds(), perf.getState() == State::Recording ? perf.getPosition() : 0.0)); }

float TimelineView::xOf(double seconds) const
{
    const auto r = rulerArea();
    return r.getX() + r.getWidth() * static_cast<float>(seconds / totalSeconds());
}

double TimelineView::secondsAt(float x) const
{
    const auto r = rulerArea();
    return std::clamp(static_cast<double>((x - r.getX()) / std::max(1.0f, r.getWidth())), 0.0, 1.0) * totalSeconds();
}

juce::String TimelineView::laneName(const Lane& lane) const
{
    if (lane.kind == Kind::Notes)
        return "Notes";
    if (lane.kind == Kind::Actions)
        return "Actions";
    return model.longName(lane.param);
}

void TimelineView::drawLane(juce::Graphics& g, const Lane& lane, juce::Rectangle<float> r) const
{
    const auto& p = perf.get();
    const bool muted = p.isMuted(lane);
    const auto tint = muted ? display::textFaint() : (lane.kind == Kind::Param ? display::tide() : lane.kind == Kind::Notes ? display::accent() : display::warn());
    const auto inner = r.reduced(0.0f, 3.0f);
    if (lane.kind == Kind::Param)
    {
        const auto& spec = model.registry.spec(lane.param);
        const auto startIt = p.start.params.find(spec.id);
        float v = spec.toNormalised(startIt != p.start.params.end() ? spec.clamp(startIt->second) : spec.defaultValue);
        auto yOf = [&inner](float n) { return inner.getBottom() - inner.getHeight() * n; };
        juce::Path line;
        line.startNewSubPath(inner.getX(), yOf(v));
        for (const auto& e : p.events)
        {
            if (! (io::Performance::laneOf(e.event) == lane) || e.event.type != engine::ControlEvent::Type::SetParam)
                continue;
            const float x = xOf(static_cast<double>(e.time) / p.sampleRate);
            line.lineTo(x, yOf(v));
            v = spec.toNormalised(e.event.value);
            line.lineTo(x, yOf(v));
        }
        line.lineTo(inner.getRight(), yOf(v));
        g.setColour(tint);
        g.strokePath(line, juce::PathStrokeType(1.3f));
        return;
    }
    if (lane.kind == Kind::Notes)
    {
        std::array<double, 128> on {};
        on.fill(-1.0);
        int lo = 127, hi = 0;
        for (const auto& e : p.events)
            if (e.event.type == engine::ControlEvent::Type::Note)
            {
                lo = std::min(lo, static_cast<int>(e.event.param & 127u));
                hi = std::max(hi, static_cast<int>(e.event.param & 127u));
            }
        const float span = static_cast<float>(std::max(1, hi - lo));
        g.setColour(tint);
        auto bar = [&](int note, double from, double to) {
            const float y = inner.getBottom() - inner.getHeight() * static_cast<float>(note - lo) / span;
            g.fillRect(juce::Rectangle<float>(xOf(from), y - 1.0f, std::max(2.0f, xOf(to) - xOf(from)), 2.0f));
        };
        for (const auto& e : p.events)
        {
            if (e.event.type != engine::ControlEvent::Type::Note)
                continue;
            const int note = static_cast<int>(e.event.param & 127u);
            const double t = static_cast<double>(e.time) / p.sampleRate;
            if (e.event.value > 0.0f)
                on[static_cast<std::size_t>(note)] = t;
            else if (on[static_cast<std::size_t>(note)] >= 0.0)
            {
                bar(note, on[static_cast<std::size_t>(note)], t);
                on[static_cast<std::size_t>(note)] = -1.0;
            }
        }
        for (int n = 0; n < 128; ++n)
            if (on[static_cast<std::size_t>(n)] >= 0.0)
                bar(n, on[static_cast<std::size_t>(n)], totalSeconds());
        return;
    }
    g.setColour(tint);
    for (const auto& e : p.events)
        if (e.event.type == engine::ControlEvent::Type::Command)
            g.fillRect(juce::Rectangle<float>(xOf(static_cast<double>(e.time) / p.sampleRate) - 1.0f, inner.getY(), 2.0f, inner.getHeight()));
}

void TimelineView::paint(juce::Graphics& g)
{
    const auto& p = perf.get();
    auto body = getLocalBounds().toFloat().withTrimmedTop(static_cast<float>(kBarH));
    drawWell(g, body);
    const auto ruler = rulerArea();

    g.setFont(font(10.0f));
    g.setColour(display::textFaint());
    const double total = totalSeconds();
    const double step = total > 600.0 ? 60.0 : total > 120.0 ? 15.0 : total > 30.0 ? 5.0 : 1.0;
    for (double t = 0.0; t <= total + 1.0e-6; t += step)
    {
        const float x = xOf(t);
        g.setColour(display::wellLine());
        g.drawVerticalLine(juce::roundToInt(x), ruler.getY(), body.getBottom());
        g.setColour(display::textFaint());
        if (x + 40.0f < ruler.getRight())
            g.drawText(clock(t), juce::Rectangle<float>(x + 3.0f, ruler.getY(), 60.0f, ruler.getHeight()), juce::Justification::centredLeft, false);
    }

    const auto& sel = perf.selection;
    if (sel.hasRange())
    {
        g.setColour(display::accent().withAlpha(0.16f));
        g.fillRect(juce::Rectangle<float>(xOf(sel.from), ruler.getY(), xOf(sel.to) - xOf(sel.from), body.getBottom() - ruler.getY()));
    }

    const auto area = laneArea();
    if (lanes.empty())
    {
        g.setFont(font(11.5f));
        g.setColour(display::textFaint());
        g.drawText(perf.getState() == State::Recording ? "Recording. Play notes, move controls, fade in and out."
                                                      : "Press Record and perform: every note, move and action is kept here to replay, edit and render.",
                   area.reduced(10.0f), juce::Justification::centred, true);
    }
    for (int k = 0; k < visibleLanes() && laneOffset + k < static_cast<int>(lanes.size()); ++k)
    {
        const auto& lane = lanes[static_cast<std::size_t>(laneOffset + k)];
        auto row = juce::Rectangle<float>(area.getX(), area.getY() + static_cast<float>(k * kLaneH), area.getWidth(), static_cast<float>(kLaneH));
        const bool chosen = sel.lane && *sel.lane == lane;
        if (chosen)
        {
            g.setColour(display::panelHi().withAlpha(0.6f));
            g.fillRect(row);
        }
        g.setColour(display::wellLine());
        g.drawHorizontalLine(juce::roundToInt(row.getBottom()), row.getX(), row.getRight());
        auto name = row.removeFromLeft(static_cast<float>(kNameW)).reduced(8.0f, 0.0f);
        g.setFont(font(11.0f, chosen ? 600 : 400));
        g.setColour(p.isMuted(lane) ? display::textFaint() : display::text());
        g.drawText(laneName(lane) + (p.isMuted(lane) ? "  (muted)" : ""), name, juce::Justification::centredLeft, true);
        drawLane(g, lane, row);
    }
    if (static_cast<int>(lanes.size()) > visibleLanes())
    {
        g.setFont(font(10.0f));
        g.setColour(display::textFaint());
        g.drawText(juce::String(laneOffset + 1) + "-" + juce::String(std::min(static_cast<int>(lanes.size()), laneOffset + visibleLanes())) + " of "
                       + juce::String(static_cast<int>(lanes.size())) + " lanes",
                   juce::Rectangle<float>(body.getX() + 8.0f, ruler.getY(), static_cast<float>(kNameW) - 10.0f, ruler.getHeight()),
                   juce::Justification::centredLeft, false);
    }

    const double at = perf.getState() == State::Idle ? sel.from : perf.getPosition();
    const float x = xOf(std::min(at, total));
    g.setColour(perf.getState() == State::Recording ? display::live() : display::text());
    g.drawVerticalLine(juce::roundToInt(x), ruler.getY(), body.getBottom());

    g.setFont(font(11.5f, 600));
    g.setColour(colour::textDim());
    const juce::String readout = perf.getState() == State::Recording ? "Recording " + clock(perf.getPosition())
                                 : p.empty()                         ? juce::String("No performance")
                                                                     : clock(at) + " / " + clock(p.seconds());
    g.drawText(readout, getLocalBounds().removeFromTop(kBarH - 4).withTrimmedLeft(renderButton.getRight() + 12), juce::Justification::centredRight, true);
}

void TimelineView::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.position;
    const auto area = laneArea();
    if (pos.y < static_cast<float>(kBarH))
        return;
    if (pos.x < area.getX() + static_cast<float>(kNameW) && pos.y >= area.getY())
    {
        const int k = laneOffset + static_cast<int>((pos.y - area.getY()) / static_cast<float>(kLaneH));
        if (k < static_cast<int>(lanes.size()))
        {
            const auto lane = lanes[static_cast<std::size_t>(k)];
            if (e.mods.isPopupMenu())
            {
                perf.selection.lane = lane;
                perf.edit("Mute lane", [lane](io::Performance& p) { p.setMuted(lane, ! p.isMuted(lane)); });
                perf.selection.lane = lane;
            }
            else
                perf.selection.lane = perf.selection.lane && *perf.selection.lane == lane ? std::nullopt : std::optional<Lane>(lane);
        }
        else
            perf.selection.lane.reset();
        refreshButtons();
        repaint();
        return;
    }
    dragAnchor = secondsAt(pos.x);
    perf.selection.from = perf.selection.to = dragAnchor;
    repaint();
}

void TimelineView::mouseDrag(const juce::MouseEvent& e)
{
    if (e.mouseDownPosition.y < static_cast<float>(kBarH) || e.mouseDownPosition.x < laneArea().getX() + static_cast<float>(kNameW))
        return;
    const double t = secondsAt(e.position.x);
    perf.selection.from = std::min(dragAnchor, t);
    perf.selection.to = std::max(dragAnchor, t);
    repaint();
}

void TimelineView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.position.y < static_cast<float>(kBarH) || e.position.x < laneArea().getX() + static_cast<float>(kNameW))
        return;
    perf.selection = { secondsAt(e.position.x), secondsAt(e.position.x), perf.selection.lane };
    perf.play(perf.selection.from);
}

void TimelineView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int maxOffset = std::max(0, static_cast<int>(lanes.size()) - visibleLanes());
    const int next = juce::jlimit(0, maxOffset, laneOffset - (wheel.deltaY > 0.0f ? 1 : wheel.deltaY < 0.0f ? -1 : 0));
    if (next != laneOffset)
    {
        laneOffset = next;
        repaint();
    }
}
}
