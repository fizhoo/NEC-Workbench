#include "analysis/AnalysisResult.h"

#include <cmath>

namespace necwb::analysis {

auto standingWaveRatio(std::complex<double> impedance, double referenceImpedance) -> double
{
    if (referenceImpedance <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const auto denominator = impedance + referenceImpedance;
    if (std::abs(denominator) == 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const auto reflectionMagnitude = std::abs((impedance - referenceImpedance) / denominator);
    if (reflectionMagnitude >= 1.0) {
        return std::numeric_limits<double>::infinity();
    }
    return (1.0 + reflectionMagnitude) / (1.0 - reflectionMagnitude);
}

}
