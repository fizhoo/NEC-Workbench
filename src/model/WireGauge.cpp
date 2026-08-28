#include "model/WireGauge.h"

#include <algorithm>
#include <cmath>

namespace necwb::model {

auto isValidAwg(int gauge) noexcept -> bool
{
    return gauge >= -3 && gauge <= 40;
}

auto awgDiameterMeters(int gauge) noexcept -> double
{
    if (!isValidAwg(gauge)) {
        return 0.0;
    }
    const auto diameterInches = 0.005 * std::pow(92.0, (36.0 - gauge) / 39.0);
    return diameterInches * 0.0254;
}

auto awgRadiusMeters(int gauge) noexcept -> double
{
    return awgDiameterMeters(gauge) / 2.0;
}

auto awgLabel(int gauge) -> std::string
{
    switch (gauge) {
    case -3:
        return "4/0 AWG";
    case -2:
        return "3/0 AWG";
    case -1:
        return "2/0 AWG";
    case 0:
        return "1/0 AWG";
    default:
        return isValidAwg(gauge) ? std::to_string(gauge) + " AWG" : "Invalid AWG";
    }
}

auto matchingAwg(double radiusMeters, double relativeTolerance) noexcept -> std::optional<int>
{
    if (!std::isfinite(radiusMeters) || radiusMeters <= 0.0) {
        return std::nullopt;
    }
    for (auto gauge = -3; gauge <= 40; ++gauge) {
        const auto standardRadius = awgRadiusMeters(gauge);
        const auto relativeError = std::abs(radiusMeters - standardRadius) / standardRadius;
        if (relativeError <= std::max(relativeTolerance, 0.0)) {
            return gauge;
        }
    }
    return std::nullopt;
}

}
