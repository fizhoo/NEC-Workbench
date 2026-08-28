#include "model/LengthUnit.h"

#include <algorithm>
#include <cmath>

namespace necwb::model {

auto metersPerUnit(LengthUnit unit) noexcept -> double
{
    switch (unit) {
    case LengthUnit::Meter:
        return 1.0;
    case LengthUnit::Centimeter:
        return 0.01;
    case LengthUnit::Millimeter:
        return 0.001;
    case LengthUnit::Inch:
        return 0.0254;
    case LengthUnit::Foot:
        return 0.3048;
    }
    return 1.0;
}

auto toMeters(double value, LengthUnit unit) noexcept -> double
{
    return value * metersPerUnit(unit);
}

auto fromMeters(double value, LengthUnit unit) noexcept -> double
{
    return value / metersPerUnit(unit);
}

auto lengthUnitSymbol(LengthUnit unit) noexcept -> std::string_view
{
    switch (unit) {
    case LengthUnit::Meter:
        return "m";
    case LengthUnit::Centimeter:
        return "cm";
    case LengthUnit::Millimeter:
        return "mm";
    case LengthUnit::Inch:
        return "in";
    case LengthUnit::Foot:
        return "ft";
    }
    return "m";
}

auto niceEngineeringStep(double minimumStep) noexcept -> double
{
    const auto positiveStep = std::max(minimumStep, 1.0e-15);
    const auto magnitude = std::pow(10.0, std::floor(std::log10(positiveStep)));
    const auto normalized = positiveStep / magnitude;
    const auto multiplier = normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0;
    return multiplier * magnitude;
}

auto nextEngineeringStep(double currentStep) noexcept -> double
{
    const auto positiveStep = std::max(currentStep, 1.0e-15);
    const auto magnitude = std::pow(10.0, std::floor(std::log10(positiveStep)));
    const auto normalized = positiveStep / magnitude;
    if (normalized < 2.0 - 1.0e-12) {
        return 2.0 * magnitude;
    }
    if (normalized < 5.0 - 1.0e-12) {
        return 5.0 * magnitude;
    }
    return 10.0 * magnitude;
}

auto previousEngineeringStep(double currentStep) noexcept -> double
{
    const auto positiveStep = std::max(currentStep, 1.0e-15);
    const auto magnitude = std::pow(10.0, std::floor(std::log10(positiveStep)));
    const auto normalized = positiveStep / magnitude;
    if (normalized > 5.0 + 1.0e-12) {
        return 5.0 * magnitude;
    }
    if (normalized > 2.0 + 1.0e-12) {
        return 2.0 * magnitude;
    }
    if (normalized > 1.0 + 1.0e-12) {
        return magnitude;
    }
    return 5.0 * magnitude / 10.0;
}

}
