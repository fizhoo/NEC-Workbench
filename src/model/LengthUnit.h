#pragma once

#include <string_view>

namespace necwb::model {

enum class LengthUnit {
    Meter,
    Centimeter,
    Millimeter,
    Inch,
    Foot
};

[[nodiscard]] auto metersPerUnit(LengthUnit unit) noexcept -> double;
[[nodiscard]] auto toMeters(double value, LengthUnit unit) noexcept -> double;
[[nodiscard]] auto fromMeters(double value, LengthUnit unit) noexcept -> double;
[[nodiscard]] auto lengthUnitSymbol(LengthUnit unit) noexcept -> std::string_view;
[[nodiscard]] auto niceEngineeringStep(double minimumStep) noexcept -> double;
[[nodiscard]] auto nextEngineeringStep(double currentStep) noexcept -> double;
[[nodiscard]] auto previousEngineeringStep(double currentStep) noexcept -> double;

}
