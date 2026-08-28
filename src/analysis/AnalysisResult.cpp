#include "analysis/AnalysisResult.h"

#include <algorithm>
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

auto radiationGainDb(const RadiationSample& sample, RadiationComponent component) -> double
{
    if (component == RadiationComponent::Total) return sample.totalGainDb;
    if (component == RadiationComponent::Vertical) return sample.verticalGainDb;
    if (component == RadiationComponent::Horizontal) return sample.horizontalGainDb;
    if (!std::isfinite(sample.totalGainDb) || sample.totalGainDb <= -900.0)
        return sample.totalGainDb;

    const auto ratio = std::clamp(sample.axialRatio, 0.0, 1.0);
    const auto denominator = 2.0 * (1.0 + ratio*ratio);
    const auto dominantFraction = (1.0 + ratio)*(1.0 + ratio) / denominator;
    const auto oppositeFraction = (1.0 - ratio)*(1.0 - ratio) / denominator;
    const auto wantsRight = component == RadiationComponent::RightHandCircular;
    auto fraction = 0.5;
    if (sample.polarizationSense == PolarizationSense::RightHand)
        fraction = wantsRight ? dominantFraction : oppositeFraction;
    else if (sample.polarizationSense == PolarizationSense::LeftHand)
        fraction = wantsRight ? oppositeFraction : dominantFraction;
    return fraction <= 0.0 ? -999.99 : sample.totalGainDb + 10.0*std::log10(fraction);
}

auto radiationMetrics(std::span<const RadiationSample> samples,
    RadiationComponent component) -> RadiationMetrics
{
    RadiationMetrics result;
    const RadiationSample* peak{};
    auto peakGain = -std::numeric_limits<double>::infinity();
    for (const auto& sample : samples) {
        const auto gain = radiationGainDb(sample, component);
        if (std::isfinite(gain) && gain > -900.0 && gain > peakGain) {
            peak = &sample;
            peakGain = gain;
        }
    }
    if (peak == nullptr) return result;

    result.valid = true;
    result.peakGainDb = peakGain;
    result.peakThetaDegrees = peak->thetaDegrees;
    result.peakPhiDegrees = peak->phiDegrees;
    const auto oppositeTheta = 180.0 - peak->thetaDegrees;
    const auto oppositePhi = std::fmod(peak->phiDegrees + 180.0, 360.0);
    const RadiationSample* back{};
    auto nearestDistance = std::numeric_limits<double>::infinity();
    for (const auto& sample : samples) {
        const auto thetaDelta = sample.thetaDegrees - oppositeTheta;
        auto phiDelta = std::fmod(std::abs(sample.phiDegrees - oppositePhi), 360.0);
        phiDelta = std::min(phiDelta, 360.0-phiDelta);
        const auto distance = thetaDelta*thetaDelta + phiDelta*phiDelta;
        if (distance < nearestDistance && radiationGainDb(sample, component) > -900.0) {
            nearestDistance = distance;
            back = &sample;
        }
    }
    if (back != nullptr)
        result.frontToBackDb = peakGain - radiationGainDb(*back, component);
    return result;
}

}
