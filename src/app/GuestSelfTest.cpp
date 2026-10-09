#include "GuestSelfTest.h"

#include <engine/Engine.h>
#include <engine/guest/GuestManager.h>

#include <cmath>

namespace tf::app {
namespace {
class SineInstrument final : public engine::Instrument
{
public:
    void prepare(const dsp::ProcessSpec& spec) override { rate = spec.sampleRate; }
    void reset() noexcept override { held = false; }
    void setControls(const std::array<float, 6>&) noexcept override {}

    void process(const engine::GuestEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept override
    {
        for (int k = 0; k < numEvents; ++k)
        {
            notes += events[k].isNoteOn() ? 1 : 0;
            if (events[k].isNoteOn())
            {
                held = true;
                step = 6.283185307179586 * 440.0 * std::pow(2.0, (events[k].data1 - 69) / 12.0) / rate;
            }
            else if (events[k].isNoteOff() || (events[k].type() == 0xb0 && events[k].data1 == 123))
                held = false;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = right[i] = held ? 0.2f * static_cast<float>(std::sin(phase)) : 0.0f;
            phase = std::fmod(phase + step, 6.283185307179586);
        }
    }

    int notes = 0;

private:
    double rate = 48000.0, phase = 0.0, step = 0.0;
    bool held = false;
};
}

void checkGuestPath(const std::function<void(bool, const juce::String&)>& check)
{
    using engine::P;
    engine::Engine engine;
    engine.prepare(48000.0, 512);
    for (const auto& s : engine::kStrips)
        engine.post(engine::ControlEvent::snapParam(engine::idx(s.level), -60.0f));
    engine.post(engine::ControlEvent::snapParam(engine::idx(P::GuestLevel), 0.0f));
    engine.setParam(P::MasterFadeSecs, 0.5f);
    engine.setParam(P::GuestPlayFrom, static_cast<float>(engine::Engine::kFromAll));
    engine.setParam(P::LoopsOn, 1.0f);
    engine.setParam(P::LoopsRate, 4.0f);
    engine.setParam(P::TideRate, 8.0f);
    engine.setParam(P::LoopsTarget, 3.0f);
    engine.setParam(P::LoopsMidiOut, 1.0f);
    engine.command(engine::Command::FadeIn);
    auto sine = std::make_unique<SineInstrument>();
    sine->prepare(engine.getGuestSpec());
    const auto* raw = sine.get();
    engine.sendInstrument(std::move(sine));
    engine.noteOn(57, 0.8f);

    std::vector<float> l(512), r(512);
    float* outs[2] = { l.data(), r.data() };
    double energy = 0.0;
    int midiNotes = 0;
    for (int b = 0; b < 48000 * 3 / 512; ++b)
    {
        engine.process(nullptr, 0, outs, 2, 512);
        for (float x : l)
            energy += static_cast<double>(x) * x;
        engine::TelemetryFrame f;
        while (engine.popTelemetry(f)) {}
        engine::MidiOutEvent m;
        while (engine.popMidiOut(m))
            midiNotes += (m.status & 0xf0) == 0x90 ? 1 : 0;
        engine.collectInstruments();
        engine.collectGarbage();
    }
    const double rmsDb = 10.0 * std::log10(energy / (48000.0 * 3.0) + 1.0e-20);
    check(rmsDb > -50.0 && raw->notes >= 3, "Guest strip plays an instrument from the keyboard and the Cycles (" + juce::String(raw->notes) + " notes, "
                                                 + juce::String(rmsDb, 1) + " dB RMS)");
    check(midiNotes >= 2, "Cycles MIDI out produces notes (" + juce::String(midiNotes) + ")");

    engine::GuestManager missing(engine);
    missing.setType("plugin:VST3-Not Installed-0-0", false, "kept", "Not Installed");
    check(missing.isMissing() && missing.getState() == "kept", "a missing instrument plugin keeps its identity and state");
}
}
