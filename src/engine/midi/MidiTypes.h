#pragma once

#include "../control/ControlEvent.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tf::engine {

/** A short MIDI message as it arrives from a device. Trivially copyable for SPSC. */
struct RawMidi
{
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint8_t port = 0;

    int channel() const noexcept { return status & 0x0f; }       // 0-15
    int type() const noexcept { return status & 0xf0; }
    bool isCc() const noexcept { return type() == 0xb0; }
    bool isNoteOn() const noexcept { return type() == 0x90 && data2 > 0; }
    bool isNoteOff() const noexcept { return type() == 0x80 || (type() == 0x90 && data2 == 0); }
};

inline constexpr int kMaxMidiPorts = 4;
inline constexpr int kMaxMidiBindings = 128;

/** What a controller can drive besides parameters. Buttons fire on a rising edge
    (value crossing 64 upward). */
enum class MidiAction : std::uint8_t { None, Catch, FadeToggle, Panic, ReleaseLive, CaptureScene };

struct MidiBinding
{
    enum class Source : std::uint8_t { Cc = 0, Note = 1 };

    Source source = Source::Cc; // notes can only trigger actions (pads)
    int channel = -1;          // -1 = any channel
    int cc = 0;                // CC number, or note number for Source::Note
    // Target: a parameter, or an action when `action` != None.
    ParamIndex param = 0;
    MidiAction action = MidiAction::None;
    float low = 0.0f;          // normalised range the controller sweeps
    float high = 1.0f;
    float curve = 0.0f;        // -1..1: 0 linear, >0 slow start, <0 fast start
    bool pickup = true;        // soft takeover
};

/** Immutable, audio-thread form of the mapping, published through SnapshotChannel. */
struct MidiMap
{
    std::vector<MidiBinding> bindings;

    /** For each (source, channel, number): a run of binding indices in `targets`.
        Bindings for "any channel" appear under all 16 channels. A control may drive
        several targets. */
    using Table16x128 = std::array<std::array<std::uint16_t, 128>, 16>;
    using Count16x128 = std::array<std::array<std::uint8_t, 128>, 16>;
    std::array<Table16x128, 2> start {};
    std::array<Count16x128, 2> count {};
    std::vector<std::uint16_t> targets;

    int noteChannel = -1;      // -1 = omni
    bool notesToDrone = false; // the last note sets the drone root
    std::uint64_t version = 0;

    void rebuildLookup()
    {
        targets.clear();
        for (int src = 0; src < 2; ++src)
            for (int ch = 0; ch < 16; ++ch)
                for (int num = 0; num < 128; ++num)
                {
                    const auto first = targets.size();
                    for (std::size_t i = 0; i < bindings.size(); ++i)
                    {
                        const auto& b = bindings[i];
                        if (static_cast<int>(b.source) == src && b.cc == num && (b.channel < 0 || b.channel == ch)
                            && targets.size() - first < 255)
                            targets.push_back(static_cast<std::uint16_t>(i));
                    }
                    const auto s = static_cast<std::size_t>(src), c = static_cast<std::size_t>(ch), n = static_cast<std::size_t>(num);
                    start[s][c][n] = static_cast<std::uint16_t>(first);
                    count[s][c][n] = static_cast<std::uint8_t>(targets.size() - first);
                }
    }
};

} // namespace tf::engine
