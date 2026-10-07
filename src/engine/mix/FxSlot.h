#pragma once

#include "../control/SpscQueue.h"

#include <dsp/fx/Processor.h>

#include <algorithm>
#include <array>
#include <memory>

namespace tf::engine {

/** One FX slot on the audio thread. Processors arrive prepared from the message
    thread (FxManager) and are swapped in with a short crossfade; the outgoing one
    goes back through a retire queue to be deleted on the message thread. A null
    processor means "empty slot" and passes audio through.

    The in-flight bound lives in FxManager: it never has more than kQueueSize
    processors outstanding per slot, so these queues cannot overflow. */
class FxSlot
{
public:
    static constexpr std::size_t kQueueSize = 4;
    static constexpr float kCrossfadeSeconds = 0.05f;

    /** Wrapper so "empty slot" can travel through the queue too. */
    struct Handoff
    {
        dsp::Processor* processor = nullptr;
    };

    FxSlot() : incoming(kQueueSize), retired(kQueueSize * 2 + 2) {}

    ~FxSlot()
    {
        Handoff h;
        while (incoming.pop(h))
            delete h.processor;
        while (retired.pop(h))
            delete h.processor;
        delete current;
        delete outgoing;
    }

    FxSlot(const FxSlot&) = delete;
    FxSlot& operator=(const FxSlot&) = delete;

    // --- Message thread ---------------------------------------------------------------
    bool send(dsp::ProcessorPtr p)
    {
        if (! incoming.push({ p.get() }))
            return false;
        p.release();
        return true;
    }

    /** Deletes retired processors; returns how many. */
    int collect()
    {
        int n = 0;
        Handoff h;
        while (retired.pop(h))
        {
            delete h.processor;
            ++n;
        }
        return n;
    }

    /** Message thread while audio is stopped (Engine::prepare). */
    void prepareAll(const dsp::ProcessSpec& spec)
    {
        Handoff h;
        while (incoming.pop(h))
        {
            retire(current);
            current = h.processor;
        }
        retire(outgoing);
        outgoing = nullptr;
        fade = 1.0f;
        if (current != nullptr)
            current->prepare(spec);
        fadeStep = 1.0f / static_cast<float>(kCrossfadeSeconds * spec.sampleRate);
    }

    // --- Audio thread -------------------------------------------------------------------
    /** Adopts a newly published processor, if any, starting a crossfade. */
    void acquire() noexcept
    {
        if (outgoing != nullptr) // finish one swap before starting the next
            return;
        Handoff h;
        if (! incoming.pop(h))
            return;
        outgoing = current;
        current = h.processor;
        fade = 0.0f;
    }

    void setControls(const std::array<float, 6>& c, const dsp::ModContext& ctx) noexcept
    {
        if (current != nullptr)
            current->setControls(c, ctx);
        if (outgoing != nullptr)
            outgoing->setControls(c, ctx);
    }

    void reset() noexcept
    {
        if (current != nullptr)
            current->reset();
        if (outgoing != nullptr)
            outgoing->reset();
    }

    bool isActive() const noexcept { return current != nullptr || outgoing != nullptr; }

    /** In place. mixStart/mixEnd ramp the dry/wet mix across the block. Scratch
        buffers must hold n samples each. */
    void process(float* left, float* right, int n, float mixStart, float mixEnd,
                 float* dryL, float* dryR, float* altL, float* altR) noexcept
    {
        if (! isActive())
            return;

        std::copy_n(left, n, dryL);
        std::copy_n(right, n, dryR);

        if (current != nullptr)
            current->process(left, right, n);
        else
        {
            std::copy_n(dryL, n, left); // fading toward empty: the "new" signal is dry
            std::copy_n(dryR, n, right);
        }

        if (outgoing != nullptr)
        {
            std::copy_n(dryL, n, altL);
            std::copy_n(dryR, n, altR);
            outgoing->process(altL, altR, n);
            for (int i = 0; i < n; ++i)
            {
                fade = std::min(1.0f, fade + fadeStep);
                left[i] = altL[i] + (left[i] - altL[i]) * fade;
                right[i] = altR[i] + (right[i] - altR[i]) * fade;
            }
            if (fade >= 1.0f)
            {
                retire(outgoing);
                outgoing = nullptr;
            }
        }

        const float step = (mixEnd - mixStart) / static_cast<float>(n);
        for (int i = 0; i < n; ++i)
        {
            const float mix = mixStart + step * static_cast<float>(i + 1);
            left[i] = dryL[i] + (left[i] - dryL[i]) * mix;
            right[i] = dryR[i] + (right[i] - dryR[i]) * mix;
        }
    }

    const dsp::Processor* getProcessor() const noexcept { return current; }

private:
    void retire(dsp::Processor* p) noexcept
    {
        if (p != nullptr)
            retired.push({ p }); // bounded by FxManager's in-flight limit
    }

    SpscQueue<Handoff> incoming;
    SpscQueue<Handoff> retired;
    dsp::Processor* current = nullptr;
    dsp::Processor* outgoing = nullptr;
    float fade = 1.0f;
    float fadeStep = 0.001f;
};

} // namespace tf::engine
