#pragma once

#include <dsp/core/ProcessSpec.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace tf::engine {
struct GuestEvent
{
    std::uint32_t offset = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    int type() const noexcept { return status & 0xf0; }
    int channel() const noexcept { return status & 0x0f; }
    bool isNoteOn() const noexcept { return type() == 0x90 && data2 > 0; }
    bool isNoteOff() const noexcept { return type() == 0x80 || (type() == 0x90 && data2 == 0); }
};

struct InstrumentInfo
{
    std::string typeId;
    std::string name;
    std::array<std::string, 6> controls {};
    std::array<float, 6> defaults { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
};

class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare(const dsp::ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    virtual void setControls(const std::array<float, 6>& controls) noexcept = 0;
    virtual void process(const GuestEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept = 0;
    virtual int getLatencySamples() const noexcept { return 0; }
};

using InstrumentPtr = std::unique_ptr<Instrument>;

class ExternalInstruments
{
public:
    virtual ~ExternalInstruments() = default;
    virtual bool handlesInstrument(std::string_view typeId) const = 0;
    virtual const InstrumentInfo* findInstrument(std::string_view typeId) const = 0;
    virtual InstrumentPtr createInstrument(std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec) = 0;
    virtual std::string captureInstrumentState() const = 0;
};
}
