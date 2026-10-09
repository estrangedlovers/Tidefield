#pragma once

#include "Instrument.h"

#include "../control/SpscQueue.h"

#include <algorithm>
#include <array>

namespace tf::engine {
class InstrumentSlot
{
public:
    static constexpr std::size_t kQueueSize = 4;
    static constexpr float kCrossfadeSeconds = 0.05f;
    static constexpr int kChannels = 16;

    struct Handoff
    {
        Instrument* instrument = nullptr;
    };

    InstrumentSlot() : incoming(kQueueSize), retired(kQueueSize * 2 + 2)
    {
        for (int c = 0; c < kChannels; ++c)
            allOff[static_cast<std::size_t>(c)] = { 0, static_cast<std::uint8_t>(0xb0 | c), 123, 0 };
    }

    ~InstrumentSlot()
    {
        Handoff h;
        while (incoming.pop(h))
            delete h.instrument;
        while (retired.pop(h))
            delete h.instrument;
        delete current;
        delete outgoing;
    }

    InstrumentSlot(const InstrumentSlot&) = delete;
    InstrumentSlot& operator=(const InstrumentSlot&) = delete;

    bool send(InstrumentPtr p)
    {
        if (! incoming.push({ p.get() }))
            return false;
        p.release();
        return true;
    }

    int collect()
    {
        int n = 0;
        Handoff h;
        while (retired.pop(h))
        {
            delete h.instrument;
            ++n;
        }
        return n;
    }

    void prepareAll(const dsp::ProcessSpec& spec)
    {
        Handoff h;
        while (incoming.pop(h))
        {
            retire(current);
            current = h.instrument;
        }
        retire(outgoing);
        outgoing = nullptr;
        fade = 1.0f;
        if (current != nullptr)
            current->prepare(spec);
        fadeStep = 1.0f / static_cast<float>(kCrossfadeSeconds * spec.sampleRate);
    }

    bool acquire() noexcept
    {
        if (outgoing != nullptr)
            return false;
        Handoff h;
        if (! incoming.pop(h))
            return false;
        outgoing = current;
        current = h.instrument;
        fade = current != nullptr && outgoing == nullptr ? 1.0f : 0.0f;
        outgoingSilenced = false;
        return true;
    }

    void setControls(const std::array<float, 6>& c) noexcept
    {
        if (current != nullptr)
            current->setControls(c);
    }

    void reset() noexcept
    {
        if (current != nullptr)
            current->reset();
        if (outgoing != nullptr)
            outgoing->reset();
    }

    bool isActive() const noexcept { return current != nullptr || outgoing != nullptr; }
    bool hasInstrument() const noexcept { return current != nullptr; }
    const Instrument* getInstrument() const noexcept { return current; }

    void process(const GuestEvent* events, int numEvents, float* left, float* right, int n, float* altL, float* altR) noexcept
    {
        std::fill_n(left, n, 0.0f);
        std::fill_n(right, n, 0.0f);
        if (current != nullptr)
            current->process(events, numEvents, left, right, n);

        if (outgoing == nullptr)
            return;
        std::fill_n(altL, n, 0.0f);
        std::fill_n(altR, n, 0.0f);
        outgoing->process(outgoingSilenced ? nullptr : allOff.data(), outgoingSilenced ? 0 : kChannels, altL, altR, n);
        outgoingSilenced = true;
        for (int i = 0; i < n; ++i)
        {
            fade = std::min(1.0f, fade + fadeStep);
            left[i] = left[i] * fade + altL[i] * (1.0f - fade);
            right[i] = right[i] * fade + altR[i] * (1.0f - fade);
        }
        if (fade >= 1.0f)
        {
            retire(outgoing);
            outgoing = nullptr;
        }
    }

private:
    void retire(Instrument* p) noexcept
    {
        if (p != nullptr)
            retired.push({ p });
    }

    SpscQueue<Handoff> incoming;
    SpscQueue<Handoff> retired;
    Instrument* current = nullptr;
    Instrument* outgoing = nullptr;
    std::array<GuestEvent, kChannels> allOff {};
    bool outgoingSilenced = false;
    float fade = 1.0f;
    float fadeStep = 0.001f;
};
}
