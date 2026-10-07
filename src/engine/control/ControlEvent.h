#pragma once

#include <cstdint>

namespace tf::engine {

using ParamIndex = std::uint16_t;

/** Where a change came from. Recorded so gesture recording and soft takeover can
    tell sources apart later. */
enum class ControlSource : std::uint8_t { UI, Midi, Score, Terrain, Osc };

enum class Command : std::uint8_t
{
    None,
    FadeIn,          // master fade up over master.fadeSeconds
    FadeOut,         // master fade down over master.fadeSeconds
    Panic,           // fast fade to silence, then reset all feedback state
    ResumeFromPanic, // clear panic and fade in
    ResetFeedback,   // clear filters/delays/reverbs without changing gain
    ReleaseLiveLayer, // hand every overridden parameter back to the terrain
    Catch,            // capture the last catch.seconds of catch.source into a cloud
    LoopRecord,       // looper pedal: record / close the loop / overdub on-off
    LoopClear,        // fade the loop out and empty it
    GestureRecord,    // start recording the performer's moves (stops playback)
    GesturePlay,      // play the published take from its start
    GestureStop,      // stop recording or playing
};

/** The single path for every change into the engine. Trivially copyable so it can
    travel through SpscQueue. */
struct ControlEvent
{
    /** SetParam on a terrain-bound parameter while scenes exist puts that parameter in
        the live layer (it overrides the terrain until released). ReleaseParam hands one
        parameter back. */
    /** SnapParam sets a value with no smoothing (session recall while faded out). */
    enum class Type : std::uint8_t { SetParam, ReleaseParam, Command, Note, SnapParam };

    Type type = Type::SetParam;
    ControlSource source = ControlSource::UI;
    Command command = Command::None;
    ParamIndex param = 0;
    float value = 0.0f;

    static ControlEvent setParam(ParamIndex index, float plainValue, ControlSource src = ControlSource::UI) noexcept
    {
        ControlEvent e;
        e.type = Type::SetParam;
        e.source = src;
        e.param = index;
        e.value = plainValue;
        return e;
    }

    static ControlEvent snapParam(ParamIndex index, float plainValue, ControlSource src = ControlSource::UI) noexcept
    {
        ControlEvent e = setParam(index, plainValue, src);
        e.type = Type::SnapParam;
        return e;
    }

    static ControlEvent releaseParam(ParamIndex index, ControlSource src = ControlSource::UI) noexcept
    {
        ControlEvent e;
        e.type = Type::ReleaseParam;
        e.source = src;
        e.param = index;
        return e;
    }

    /** Note on (velocity > 0) or off (velocity 0). `param` carries the MIDI note. */
    static ControlEvent note(int noteNumber, float velocity, ControlSource src = ControlSource::UI) noexcept
    {
        ControlEvent e;
        e.type = Type::Note;
        e.source = src;
        e.param = static_cast<ParamIndex>(noteNumber & 127);
        e.value = velocity;
        return e;
    }

    static ControlEvent makeCommand(Command c, ControlSource src = ControlSource::UI) noexcept
    {
        ControlEvent e;
        e.type = Type::Command;
        e.source = src;
        e.command = c;
        return e;
    }
};

} // namespace tf::engine
