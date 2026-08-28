#pragma once

#include <string>
#include <optional>

namespace necwb::model {

[[nodiscard]] auto isValidAwg(int gauge) noexcept -> bool;
[[nodiscard]] auto awgDiameterMeters(int gauge) noexcept -> double;
[[nodiscard]] auto awgRadiusMeters(int gauge) noexcept -> double;
[[nodiscard]] auto awgLabel(int gauge) -> std::string;
[[nodiscard]] auto matchingAwg(double radiusMeters, double relativeTolerance = 1.0e-6) noexcept
    -> std::optional<int>;

}
