#pragma once

#include <QString>

#include <cmath>

namespace necwb::ui {

inline constexpr auto DisplayDecimalPlaces = 3;

inline auto formatDecimal(double value) -> QString
{
    if (value != 0.0 && (std::abs(value) < 0.001 || std::abs(value) >= 1.0e9)) {
        return QString::number(value, 'e', DisplayDecimalPlaces);
    }
    return QString::number(value, 'f', DisplayDecimalPlaces);
}

}
