#pragma once

#include <algorithm>
#include <cmath>

namespace necwb {

inline auto nearlyEqual(double first, double second) -> bool
{
    return std::abs(first - second)
        <= 1.0e-9 * std::max({1.0, std::abs(first), std::abs(second)});
}

}
