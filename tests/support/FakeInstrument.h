#pragma once

#include <engine/guest/Instrument.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tf::test {
class FakeInstrument final : public engine::Instrument
{
public:
    static constexpr int kMaxLog = 4096;

    struct Logged
    {
        std::uint64_t time = 0;
        engine::GuestEvent event;
    };

    void prepare(const dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        preparedBlock = spec.maxBlockSize;
        ++prepares;
    }

    void reset() noexcept override
    {
        for (auto& v : voices)
            v = {};
        ++resets;
    }

    void setControls(const std::array<float, 6>& c) noexcept override { controls = c; }

    void process(const engine::GuestEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept override
    {
        ++blocks;
        largestBlock = numSamples > largestBlock ? numSamples : largestBlock;
        int next = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            while (next < numEvents && static_cast<int>(events[next].offset) <= i)
                handle(events[next++], time + static_cast<std::uint64_t>(i));
            float l = 0.0f, r = 0.0f;
            for (auto& v : voices)
            {
                if (v.gain <= 0.0f && ! v.held)
                    continue;
                v.gain = v.held ? std::min(1.0f, v.gain + 0.002f) : std::max(0.0f, v.gain - 0.0005f);
                const double s = std::sin(v.phase) + static_cast<double>(controls[1]) * 0.5 * std::sin(3.0 * v.phase);
                v.phase += v.step;
                if (v.phase > 6.283185307179586)
                    v.phase -= 6.283185307179586;
                const float x = static_cast<float>(s) * v.gain * v.velocity * 0.2f * controls[0];
                l += x * (1.0f - 0.5f * controls[2]);
                r += x * (0.5f + 0.5f * controls[2]);
            }
            left[i] = l;
            right[i] = r;
        }
        while (next < numEvents)
            handle(events[next++], time + static_cast<std::uint64_t>(numSamples));
        time += static_cast<std::uint64_t>(numSamples);
        int sounding = 0;
        for (const auto& v : voices)
            sounding += v.held ? 1 : 0;
        heldSum += sounding;
    }

    int held() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            n += v.held ? 1 : 0;
        return n;
    }

    int noteOns() const noexcept
    {
        int n = 0;
        for (int k = 0; k < logged; ++k)
            n += log[static_cast<std::size_t>(k)].event.isNoteOn() ? 1 : 0;
        return n;
    }

    std::array<float, 6> controls { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    std::array<Logged, kMaxLog> log {};
    int logged = 0;
    int blocks = 0;
    int largestBlock = 0;
    int prepares = 0;
    int resets = 0;
    int preparedBlock = 0;
    double heldSum = 0.0;
    std::uint64_t time = 0;
    double sampleRate = 48000.0;

private:
    struct Voice
    {
        int note = -1;
        int channel = 0;
        bool held = false;
        float gain = 0.0f;
        float velocity = 0.0f;
        double phase = 0.0;
        double step = 0.0;
    };

    void handle(const engine::GuestEvent& e, std::uint64_t at) noexcept
    {
        if (logged < kMaxLog)
            log[static_cast<std::size_t>(logged++)] = { at, e };
        if (e.type() == 0xb0 && e.data1 == 123)
        {
            for (auto& v : voices)
                if (v.channel == e.channel())
                    v.held = false;
            return;
        }
        if (e.isNoteOn())
        {
            Voice* slot = &voices[0];
            for (auto& v : voices)
                if (! v.held && v.gain <= 0.0f)
                {
                    slot = &v;
                    break;
                }
            slot->note = e.data1;
            slot->channel = e.channel();
            slot->held = true;
            slot->velocity = static_cast<float>(e.data2) / 127.0f;
            slot->phase = 0.0;
            slot->step = 6.283185307179586 * 440.0 * std::pow(2.0, (e.data1 - 69) / 12.0) / sampleRate;
        }
        else if (e.isNoteOff())
            for (auto& v : voices)
                if (v.held && v.note == e.data1 && v.channel == e.channel())
                    v.held = false;
    }

    std::array<Voice, 16> voices {};
};
}
