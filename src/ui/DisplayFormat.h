#pragma once

#include "analysis/GainDirection.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <string>

namespace necwb::ui {

inline constexpr auto DisplayDecimalPlaces = analysis::DisplayDecimalPlaces;

inline auto formatDecimal(double value) -> QString
{
    if (value != 0.0 && (std::abs(value) < 0.001 || std::abs(value) >= 1.0e9)) {
        return QString::number(value, 'e', DisplayDecimalPlaces);
    }
    return QString::number(value, 'f', DisplayDecimalPlaces);
}

inline auto fromCore(const std::string& text) -> QString
{
    return QString::fromStdString(text);
}

inline auto formatAxisTick(double value, double step) -> QString
{
    const auto magnitude = std::abs(value);
    const auto absoluteStep = std::abs(step);
    if (magnitude >= 10000.0
        || (magnitude > 0.0 && magnitude < 0.01)
        || (absoluteStep > 0.0 && absoluteStep < 1.0e-6)) {
        return QString::number(value, 'g', 6);
    }
    if (!(absoluteStep > 0.0) || !std::isfinite(absoluteStep)) {
        if (std::abs(value - std::round(value)) < 1.0e-9)
            return QString::number(static_cast<qlonglong>(std::llround(value)));
        return QString::number(value, 'f', magnitude < 2.0 ? 1 : 0);
    }
    const auto decimals = absoluteStep > 0.0 && std::isfinite(absoluteStep)
        ? std::clamp(static_cast<int>(std::ceil(
              -std::log10(absoluteStep) - 1.0e-9)), 0, 6)
        : 0;
    return QString::number(value, 'f', decimals);
}

}
