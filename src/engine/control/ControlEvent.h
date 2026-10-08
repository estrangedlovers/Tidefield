#pragma once

#include <cstdint>

namespace tf::engine {
using ParamIndex = std::uint16_t;

enum class ControlSource : std::uint8_t { UI, Midi, Score, Terrain, Osc };

enum class Command : std::uint8_t
{
    None,
    FadeIn,
    FadeOut,
    Panic,
    ResumeFromPanic,
    ResetFeedback,
    ReleaseLiveLayer,
    Catch,
    LoopRecord,
    LoopClear,
    GestureRecord,
    GesturePlay,
    GestureStop,
};

struct ControlEvent
{
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
}
