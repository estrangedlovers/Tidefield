#include "ParamRegistry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tf::engine {

float ParamSpec::clamp(float v) const noexcept
{
    // NaN or infinity (a damaged file, a bad controller value) never reaches the DSP.
    return std::isfinite(v) ? std::clamp(v, minValue, maxValue) : defaultValue;
}

float ParamSpec::toNormalised(float plain) const noexcept
{
    const float v = clamp(plain);
    if (taper == Taper::Log && minValue > 0.0f)
        return std::log(v / minValue) / std::log(maxValue / minValue);
    return (v - minValue) / (maxValue - minValue);
}

float ParamSpec::fromNormalised(float n) const noexcept
{
    n = std::clamp(n, 0.0f, 1.0f);
    if (taper == Taper::Log && minValue > 0.0f)
        return minValue * std::pow(maxValue / minValue, n);
    return minValue + n * (maxValue - minValue);
}

ParamRegistry::ParamRegistry()
{
    using namespace ParamFlag;
    specs.reserve(kNumParams);

#define TF_PARAM_SPEC(name, id, display, lo, hi, def, taper, smooth, secs, unit, flags) \
    specs.push_back(ParamSpec { id, display, lo, hi, def, Taper::taper, Smoothing::smooth, secs, unit, flags });
    TF_PARAM_LIST(TF_PARAM_SPEC)
#undef TF_PARAM_SPEC

    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const auto [it, inserted] = byId.emplace(specs[i].id, static_cast<ParamIndex>(i));
        if (! inserted)
            throw std::logic_error("Duplicate parameter id: " + specs[i].id);
    }
}

std::optional<ParamIndex> ParamRegistry::find(std::string_view id) const
{
    if (const auto it = byId.find(std::string(id)); it != byId.end())
        return it->second;
    return std::nullopt;
}

} // namespace tf::engine
