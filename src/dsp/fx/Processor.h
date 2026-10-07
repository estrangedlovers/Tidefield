#pragma once

#include "../core/ProcessSpec.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

namespace tf::dsp {

/** How a 0..1 control value is shown. Declarative so any front end (the JUCE panel,
    the React UI) formats identically:
      Linear  a + b * v
      Exp     a * b^v        (b is the max/min ratio)
      Power   a * v^b
      Choice  choices[floor(v * count)]
      Hidden  control unused by this processor */
struct DisplayMap
{
    enum class Curve : unsigned char { Linear, Exp, Power, Choice, Hidden };
    Curve curve = Curve::Linear;
    float a = 0.0f;
    float b = 1.0f;
    const char* unit = "";
    int decimals = 0;
    const char* const* choices = nullptr;
    int numChoices = 0;

    float value(float v01) const noexcept;
    /** Writes e.g. "2.4 s"; Exp values >= 1000 with unit "ms" print as seconds. */
    void format(float v01, char* out, int outSize) const noexcept;
};

/** What a processor's six generic controls mean. Slots expose stable parameter IDs
    (`fx.<slot>.p1` ... `p6`, each 0..1) so scenes, MIDI and sessions never depend on
    which processor is loaded; the processor maps them to its own ranges and the UI
    shows these names and formatted values. */
struct ProcessorControl
{
    const char* name = "";
    float defaultValue = 0.5f;
    DisplayMap display {};
};

struct ProcessorInfo
{
    const char* typeId = "";   // stable, persisted: "tf.reverb"
    const char* name = "";     // shown in menus
    std::array<ProcessorControl, 6> controls {};
    /** True for effects meant to return fully wet on a send bus (reverb, delay). */
    bool sendStyle = false;
};

class HarmonicGravity;

/** Context that changes per control tick: Tide for every modulation rate, and the
    current key for processors that tune themselves (may be null outside the engine). */
struct ModContext
{
    float timeScale = 1.0f;
    const HarmonicGravity* harmony = nullptr;
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

inline float DisplayMap::value(float v) const noexcept
{
    switch (curve)
    {
        case Curve::Linear: return a + b * v;
        case Curve::Exp: return a * std::pow(b, v);
        case Curve::Power: return a * std::pow(v, b);
        case Curve::Choice:
        case Curve::Hidden: return v;
    }
    return v;
}

inline void DisplayMap::format(float v, char* out, int outSize) const noexcept
{
    const auto n = static_cast<std::size_t>(outSize);
    if (curve == Curve::Hidden)
    {
        std::snprintf(out, n, "-");
        return;
    }
    if (curve == Curve::Choice && choices != nullptr && numChoices > 0)
    {
        int i = static_cast<int>(v * static_cast<float>(numChoices));
        i = i < 0 ? 0 : (i >= numChoices ? numChoices - 1 : i);
        std::snprintf(out, n, "%s", choices[i]);
        return;
    }
    const float x = value(v);
    if (unit[0] == 'm' && unit[1] == 's' && unit[2] == 0 && x >= 1000.0f)
    {
        std::snprintf(out, n, "%.2f s", static_cast<double>(x) * 0.001);
        return;
    }
    std::snprintf(out, n, "%.*f%s%s", decimals, static_cast<double>(x), unit[0] == 0 || unit[0] == '%' ? "" : " ", unit);
}

} // namespace tf::dsp
