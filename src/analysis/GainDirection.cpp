#include "analysis/GainDirection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace necwb::analysis {
namespace {

constexpr auto frequencyToleranceMHz = 1.0e-6;
constexpr auto angleToleranceDegrees = 1.0e-6;

auto printed(const char* format, double value) -> std::string
{
    std::array<char, 64> buffer{};
    const auto length = std::snprintf(buffer.data(), buffer.size(), format,
        DisplayDecimalPlaces, value);
    return length > 0 ? std::string(buffer.data()) : std::string{};
}

auto normalizedAzimuth(double degrees) -> double
{
    auto azimuth = std::fmod(degrees, 360.0);
    if (azimuth < 0.0) azimuth += 360.0;
    // fmod can round a value just below 360 up to 360.
    if (azimuth >= 360.0 - angleToleranceDegrees) azimuth = 0.0;
    return azimuth;
}

auto componentAdjective(RadiationComponent component) -> std::string
{
    switch (component) {
    case RadiationComponent::Total: return {};
    case RadiationComponent::Vertical: return "vertical";
    case RadiationComponent::Horizontal: return "horizontal";
    case RadiationComponent::RightHandCircular: return "RHCP";
    case RadiationComponent::LeftHandCircular: return "LHCP";
    }
    return {};
}

// Gap to the nearest distinct value on each side of `value`; `circular` wraps at 360.
void neighbourGaps(std::vector<double> values, double value, bool circular,
    std::optional<double>& widest)
{
    std::ranges::sort(values);
    const auto duplicates = std::ranges::unique(values, [](double left, double right) {
        return std::abs(left - right) <= angleToleranceDegrees;
    });
    values.erase(duplicates.begin(), duplicates.end());
    if (values.size() < 2) return;
    const auto position = std::ranges::find_if(values, [value](double candidate) {
        return std::abs(candidate - value) <= angleToleranceDegrees;
    });
    if (position == values.end()) return;
    const auto index = static_cast<std::size_t>(std::distance(values.begin(), position));
    // A cut covers the circle when the closing gap is no wider than the widest interior gap.
    auto interior = 0.0;
    for (auto next = std::size_t{1}; next < values.size(); ++next)
        interior = std::max(interior, values[next] - values[next - 1]);
    const auto closing = values.front() + 360.0 - values.back();
    const auto wraps = circular && closing <= interior + angleToleranceDegrees;
    const auto record = [&widest](double gap) {
        if (gap > angleToleranceDegrees) widest = std::max(widest.value_or(0.0), gap);
    };
    if (index > 0) record(values[index] - values[index - 1]);
    else if (wraps) record(closing);
    if (index + 1 < values.size()) record(values[index + 1] - values[index]);
    else if (wraps) record(closing);
}

}

auto patternDirection(double thetaDegrees, double phiDegrees) -> PatternDirection
{
    auto theta = std::fmod(thetaDegrees, 360.0);
    auto phi = phiDegrees;
    if (theta < 0.0) theta += 360.0;
    if (theta > 180.0) {
        theta = 360.0 - theta;
        phi += 180.0;
    }
    auto elevation = 90.0 - theta;
    if (std::abs(elevation) < angleToleranceDegrees) elevation = 0.0;
    return {elevation, normalizedAzimuth(phi)};
}

auto necThetaForElevation(double elevationDegrees) -> double
{
    return 90.0 - elevationDegrees;
}

auto formatDisplayDecimal(double value) -> std::string
{
    if (value == 0.0) value = 0.0;
    if (value != 0.0 && (std::abs(value) < 0.001 || std::abs(value) >= 1.0e9))
        return printed("%.*e", value);
    auto text = printed("%.*f", value);
    if (text.starts_with("-") && text.find_first_not_of("-0.") == std::string::npos)
        text.erase(0, 1);
    return text;
}

auto formatAngle(double degrees) -> std::string
{
    auto text = printed("%.*f", degrees);
    if (const auto point = text.find('.'); point != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.') text.pop_back();
    }
    if (text == "-0") text = "0";
    return text + "°";
}

auto formatDirection(double thetaDegrees, double phiDegrees) -> std::string
{
    const auto direction = patternDirection(thetaDegrees, phiDegrees);
    return "El " + formatAngle(direction.elevationDegrees)
        + ", Az " + formatAngle(direction.azimuthDegrees);
}

auto formatNecDirection(double thetaDegrees, double phiDegrees) -> std::string
{
    return "θ " + formatAngle(thetaDegrees) + ", φ " + formatAngle(phiDegrees);
}

auto radiationComponentName(RadiationComponent component) -> std::string
{
    switch (component) {
    case RadiationComponent::Total: return "Total";
    case RadiationComponent::Vertical: return "Vertical";
    case RadiationComponent::Horizontal: return "Horizontal";
    case RadiationComponent::RightHandCircular: return "RHCP";
    case RadiationComponent::LeftHandCircular: return "LHCP";
    }
    return {};
}

auto isCoarseSampleStep(std::optional<double> stepDegrees) -> bool
{
    return stepDegrees && *stepDegrees > CoarsePatternStepDegrees + angleToleranceDegrees;
}

auto coarseSampleNote(double stepDegrees) -> std::string
{
    return "sampled, RP step " + formatAngle(stepDegrees);
}

auto formatGain(const GainReading& reading) -> GainText
{
    GainText result;
    result.text = formatDisplayDecimal(reading.gainDb) + " dBi";
    if (const auto adjective = componentAdjective(reading.component); !adjective.empty())
        result.text += " " + adjective;
    const auto known = reading.thetaDegrees && reading.phiDegrees;
    result.text += known
        ? " @ " + formatDirection(*reading.thetaDegrees, *reading.phiDegrees)
        : std::string{", direction unavailable"};
    const auto coarse = isCoarseSampleStep(reading.sampleStepDegrees);
    if (coarse) result.text += " (" + coarseSampleNote(*reading.sampleStepDegrees) + ")";

    result.toolTip = known
        ? "NEC " + formatNecDirection(*reading.thetaDegrees, *reading.phiDegrees)
        : std::string{"The direction of this value is not known"};
    if (reading.frequencyMHz)
        result.toolTip += " · " + formatDisplayDecimal(*reading.frequencyMHz) + " MHz";
    result.toolTip += " · " + radiationComponentName(reading.component) + " gain";
    if (coarse) {
        result.toolTip += "\nSampled peak: the RP step around this sample is "
            + formatAngle(*reading.sampleStepDegrees)
            + ", so the true peak may sit between samples. Request a finer RP step "
              "(5° or less) to locate it.";
    }
    return result;
}

auto azimuthCutName(double thetaDegrees) -> std::string
{
    return "Azimuth cut at El " + formatAngle(patternDirection(thetaDegrees, 0.0).elevationDegrees);
}

auto elevationCutName(double phiDegrees) -> std::string
{
    return "Elevation cut at Az " + formatAngle(normalizedAzimuth(phiDegrees));
}

auto referenceLevelText(double referenceDbi) -> std::string
{
    return "0 dB = " + formatDisplayDecimal(referenceDbi) + " dBi";
}

auto frontToBackDefinition() -> std::string
{
    return "F/B: gain in the forward direction minus gain in the opposite direction "
           "(same elevation, azimuth + 180°).";
}

auto frontToRearDefinition(DirectionalEvaluationMode mode) -> std::string
{
    switch (mode) {
    case DirectionalEvaluationMode::SpecifiedDirection:
        return "F/R: forward gain minus the strongest sample in the rear of the forward "
               "azimuth cut (same elevation, azimuth more than 90° from forward).";
    case DirectionalEvaluationMode::StrongestInAzimuthCut:
        return "F/R: tracked lobe gain minus the strongest sample in this azimuth cut that is "
               "more than 90° from the lobe in 3D. For an elevated cut the rear starts a few "
               "degrees past 90° in azimuth.";
    case DirectionalEvaluationMode::StrongestInElevationCut:
        return "F/R: tracked lobe gain minus the strongest sample in this elevation cut that is "
               "more than 90° from the lobe, including high angles over the back.";
    }
    return {};
}

auto patternStepAround(std::span<const RadiationSample> samples,
    const RadiationSample& peak) -> std::optional<double>
{
    std::vector<double> thetaValues;
    std::vector<double> phiValues;
    for (const auto& sample : samples) {
        if (std::abs(sample.frequencyMHz - peak.frequencyMHz) > frequencyToleranceMHz
            || sample.patternIndex != peak.patternIndex) continue;
        if (std::abs(sample.phiDegrees - peak.phiDegrees) <= angleToleranceDegrees)
            thetaValues.push_back(sample.thetaDegrees);
        if (std::abs(sample.thetaDegrees - peak.thetaDegrees) <= angleToleranceDegrees)
            phiValues.push_back(normalizedAzimuth(sample.phiDegrees));
    }
    std::optional<double> widest;
    neighbourGaps(std::move(thetaValues), peak.thetaDegrees, false, widest);
    // Every phi at the zenith or nadir is the same direction.
    if (std::abs(std::sin(peak.thetaDegrees * std::numbers::pi / 180.0)) > 1.0e-9)
        neighbourGaps(std::move(phiValues), normalizedAzimuth(peak.phiDegrees), true, widest);
    return widest;
}

}
