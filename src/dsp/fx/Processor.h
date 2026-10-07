#pragma once

#include "../core/ProcessSpec.h"

#include <array>
#include <memory>

namespace tf::dsp {

/** What a processor's six generic controls mean. Slots expose stable parameter IDs
    (`fx.<slot>.p1` ... `p6`, each 0..1) so scenes, MIDI and sessions never depend on
    which processor is loaded; the processor maps them to its own ranges and the UI
    shows these names and formatted values. */
struct ProcessorControl
{
    const char* name = "";
    float defaultValue = 0.5f;
    /** Formats the processor's interpretation of a 0..1 value, e.g. "2.4 s". */
    void (*format)(float value01, char* out, int outSize) = nullptr;
};

struct ProcessorInfo
{
    const char* typeId = "";   // stable, persisted: "tf.reverb"
    const char* name = "";     // shown in menus
    std::array<ProcessorControl, 6> controls {};
    /** True for effects meant to return fully wet on a send bus (reverb, delay). */
    bool sendStyle = false;
};

/** Context that changes per block: Tide for every modulation rate. */
struct ModContext
{
    float timeScale = 1.0f;
};

/** Base for everything that can sit in an FX slot: inserts, send buses, master.
    prepare() may allocate and runs off the audio thread; everything else is
    realtime-safe. Implementations process in place and output the wet signal; the
    slot applies dry/wet mix. */
class Processor
{
public:
    virtual ~Processor() = default;

    virtual const ProcessorInfo& info() const noexcept = 0;
    virtual void prepare(const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    /** Controls are 0..1, already smoothed by the engine. */
    virtual void setControls(const std::array<float, 6>& controls, const ModContext& ctx) noexcept = 0;
    virtual void process(float* left, float* right, int numSamples) noexcept = 0;
    virtual int getLatencySamples() const noexcept { return 0; }
    /** How long the processor keeps sounding after input stops (for offline renders
        and the plugin's getTailLengthSeconds). */
    virtual float getTailSeconds() const noexcept { return 0.0f; }
};

using ProcessorPtr = std::unique_ptr<Processor>;

} // namespace tf::dsp
