#pragma once

#include "ParamDefs.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tf::engine {

struct ParamSpec
{
    std::string id;
    std::string name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    Taper taper = Taper::Linear;
    Smoothing smoothing = Smoothing::Linear;
    float smoothingSeconds = 0.05f;
    std::string unit;
    unsigned flags = ParamFlag::kNone;

    float clamp(float v) const noexcept;
    /** Maps a plain value to 0..1 using the taper (for UI controls and MIDI). */
    float toNormalised(float plain) const noexcept;
    float fromNormalised(float normalised) const noexcept;
};

/** Static description of every parameter. Built once; read-only afterwards, so it is
    safe to read from any thread. Lookups by string are for the non-realtime side. */
class ParamRegistry
{
public:
    ParamRegistry();

    std::size_t size() const noexcept { return specs.size(); }
    const ParamSpec& spec(ParamIndex i) const noexcept { return specs[i]; }
    const ParamSpec& spec(P p) const noexcept { return specs[idx(p)]; }
    std::optional<ParamIndex> find(std::string_view id) const;

    const std::vector<ParamSpec>& all() const noexcept { return specs; }

private:
    std::vector<ParamSpec> specs;
    std::unordered_map<std::string, ParamIndex> byId;
};

} // namespace tf::engine
