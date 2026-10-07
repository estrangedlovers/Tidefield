#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace tf::dsp {

/** A set of pitch classes plus a root. Notes are MIDI note numbers (fractional ok). */
struct Scale
{
    std::uint16_t mask = 0x0FFF; // bit n = pitch class (root + n) is in the scale
    int root = 2;                // 0 = C ... 11 = B

    bool contains(int pitchClassFromC) const noexcept
    {
        const int degree = ((pitchClassFromC - root) % 12 + 12) % 12;
        return (mask >> degree) & 1u;
    }

    /** Nearest scale tone to `note`. Ties resolve downward so results are stable. */
    float nearest(float note) const noexcept
    {
        const int centre = static_cast<int>(std::lround(note));
        for (int offset = 0; offset <= 6; ++offset)
        {
            const int below = centre - offset;
            const int above = centre + offset;
            const bool hasBelow = contains(below);
            const bool hasAbove = contains(above);
            if (hasBelow && hasAbove)
                return std::fabs(note - static_cast<float>(below)) <= std::fabs(note - static_cast<float>(above))
                           ? static_cast<float>(below) : static_cast<float>(above);
            if (hasBelow)
                return static_cast<float>(below);
            if (hasAbove)
                return static_cast<float>(above);
        }
        return note;
    }

    /** Scale degree `degree` (0 = root) counting from the root at octave `baseNote`'s
        root. Negative degrees go down. */
    float degreeToNote(int baseRootNote, int degree) const noexcept
    {
        int count = 0;
        for (int i = 0; i < 12; ++i)
            count += (mask >> i) & 1u;
        if (count == 0)
            return static_cast<float>(baseRootNote);
        const int octave = degree >= 0 ? degree / count : -((-degree + count - 1) / count);
        int remainder = degree - octave * count;
        int semitone = 0;
        for (int i = 0; i < 12; ++i)
        {
            if ((mask >> i) & 1u)
            {
                if (remainder == 0)
                {
                    semitone = i;
                    break;
                }
                --remainder;
            }
        }
        return static_cast<float>(baseRootNote + octave * 12 + semitone);
    }
};

/** Built-in scales. Order is persisted by index in sessions: append only. */
struct ScaleType
{
    const char* name;
    std::uint16_t mask;
};

inline constexpr std::array<ScaleType, 12> kScaleTypes { {
    { "Major",            0b101010110101 },
    { "Minor",            0b010110101101 },
    { "Dorian",           0b011010101101 },
    { "Lydian",           0b101011010101 },
    { "Mixolydian",       0b011010110101 },
    { "Major pentatonic", 0b001010010101 },
    { "Minor pentatonic", 0b010010101001 },
    { "Whole tone",       0b010101010101 },
    { "Hirajoshi",        0b000110001101 },
    { "In sen",           0b010010100011 },
    { "Fifths",           0b000010000001 },
    { "Chromatic",        0b111111111111 },
} };

inline constexpr std::array<const char*, 12> kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

} // namespace tf::dsp
