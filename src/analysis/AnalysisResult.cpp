#include "analysis/AnalysisResult.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace necwb::analysis {
namespace {

constexpr auto gainTieToleranceDb = 1.0e-6;
constexpr auto directionTolerance = 1.0e-10;

auto unitDirection(double thetaDegrees, double phiDegrees) -> std::array<double, 3>
{
    constexpr auto radiansPerDegree = std::numbers::pi / 180.0;
    const auto theta = thetaDegrees * radiansPerDegree;
    const auto phi = phiDegrees * radiansPerDegree;
    return {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
}

auto directionDot(const std::array<double, 3>& first, const std::array<double, 3>& second) -> double
{
    return first[0]*second[0] + first[1]*second[1] + first[2]*second[2];
}

auto interpolatedCrossing(double outsideAngle, double outsideGain,
    double insideAngle, double insideGain, double threshold) -> double
{
    const auto gainDifference = insideGain - outsideGain;
    if (std::abs(gainDifference) <= std::numeric_limits<double>::epsilon()) return outsideAngle;
    return outsideAngle + (threshold - outsideGain)
        * (insideAngle - outsideAngle) / gainDifference;
}

}

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
    for (const auto& sample : samples) {
        const auto gain = radiationGainDb(sample, component);
        if (!std::isfinite(gain) || gain <= -900.0
            || std::abs(gain - peakGain) > gainTieToleranceDb) continue;
        const auto direction = unitDirection(sample.thetaDegrees, sample.phiDegrees);
        if (std::ranges::none_of(result.tiedPeaks, [direction](const auto& tiedPeak) {
                return directionDot(direction,
                    unitDirection(tiedPeak.thetaDegrees, tiedPeak.phiDegrees))
                    >= 1.0 - directionTolerance;
            })) {
            result.tiedPeaks.push_back(sample);
        }
    }
    const auto frontDirection = unitDirection(peak->thetaDegrees, peak->phiDegrees);
    const std::array<double, 3> backDirection{
        -frontDirection[0], -frontDirection[1], -frontDirection[2]};
    const RadiationSample* back{};
    auto bestDirectionDot = -1.0;
    for (const auto& sample : samples) {
        const auto gain = radiationGainDb(sample, component);
        if (!std::isfinite(gain) || gain <= -900.0) continue;
        const auto dot = directionDot(unitDirection(sample.thetaDegrees, sample.phiDegrees), backDirection);
        if (dot > bestDirectionDot) {
            bestDirectionDot = dot;
            back = &sample;
        }
    }
    if (back != nullptr && bestDirectionDot >= 1.0 - directionTolerance)
        result.frontToBackDb = peakGain - radiationGainDb(*back, component);
    return result;
}

auto radiationCutMetrics(std::span<const RadiationCutPoint> samples, bool wraps)
    -> RadiationCutMetrics
{
    RadiationCutMetrics result;
    std::vector<RadiationCutPoint> values;
    for (const auto& sample : samples) {
        if (std::isfinite(sample.gainDb) && sample.gainDb > -900.0) values.push_back(sample);
    }
    if (values.empty()) return result;
    std::ranges::sort(values, {}, &RadiationCutPoint::angleDegrees);
    const auto peak = std::ranges::max_element(values, {}, &RadiationCutPoint::gainDb);
    const auto peakIndex = static_cast<std::size_t>(std::distance(values.begin(), peak));
    result.valid = true;
    result.peakAngleDegrees = peak->angleDegrees;
    result.peakGainDb = peak->gainDb;
    std::vector<std::array<double, 3>> tiedDirections;
    for (const auto& sample : values) {
        if (std::abs(sample.gainDb - peak->gainDb) <= gainTieToleranceDb) {
            const auto direction = unitDirection(sample.thetaDegrees, sample.phiDegrees);
            if (std::ranges::any_of(tiedDirections, [direction](const auto& existing) {
                    return directionDot(direction, existing) >= 1.0 - directionTolerance;
                })) continue;
            result.tiedPeakAnglesDegrees.push_back(sample.angleDegrees);
            tiedDirections.push_back(direction);
        }
    }

    const auto frontDirection = unitDirection(peak->thetaDegrees, peak->phiDegrees);
    const std::array<double, 3> backDirection{
        -frontDirection[0], -frontDirection[1], -frontDirection[2]};
    const RadiationCutPoint* back{};
    auto bestDirectionDot = -1.0;
    for (const auto& sample : values) {
        const auto dot = directionDot(unitDirection(sample.thetaDegrees, sample.phiDegrees), backDirection);
        if (dot > bestDirectionDot) {
            bestDirectionDot = dot;
            back = &sample;
        }
    }
    if (back != nullptr && bestDirectionDot >= 1.0 - directionTolerance)
        result.frontToBackDb = peak->gainDb - back->gainDb;

    const auto threshold = peak->gainDb - 3.0;
    auto leftInsideAngle = peak->angleDegrees;
    auto rightInsideAngle = peak->angleDegrees;
    std::optional<double> leftCrossing;
    std::optional<double> rightCrossing;
    for (auto step = std::size_t{1}; step < values.size(); ++step) {
        if (!wraps && step > peakIndex) break;
        const auto index = (peakIndex + values.size() - step) % values.size();
        auto angle = values[index].angleDegrees;
        if (index >= peakIndex) angle -= 360.0;
        if (values[index].gainDb <= threshold) {
            leftCrossing = interpolatedCrossing(angle, values[index].gainDb,
                leftInsideAngle, values[(index + 1) % values.size()].gainDb, threshold);
            break;
        }
        leftInsideAngle = angle;
    }
    for (auto step = std::size_t{1}; step < values.size(); ++step) {
        if (!wraps && peakIndex + step >= values.size()) break;
        const auto index = (peakIndex + step) % values.size();
        auto angle = values[index].angleDegrees;
        if (index <= peakIndex) angle += 360.0;
        if (values[index].gainDb <= threshold) {
            const auto insideIndex = (index + values.size() - 1) % values.size();
            rightCrossing = interpolatedCrossing(angle, values[index].gainDb,
                rightInsideAngle, values[insideIndex].gainDb, threshold);
            break;
        }
        rightInsideAngle = angle;
    }
    if (leftCrossing && rightCrossing) {
        result.sampledBeamwidthDegrees = rightInsideAngle - leftInsideAngle;
        result.interpolatedBeamwidthDegrees = *rightCrossing - *leftCrossing;
    } else if (wraps && values.size() > 1
        && std::ranges::all_of(values, [threshold](const auto& sample) {
            return sample.gainDb > threshold;
        })) {
        result.sampledBeamwidthDegrees = 360.0;
        result.interpolatedBeamwidthDegrees = 360.0;
    }
    return result;
}

}
