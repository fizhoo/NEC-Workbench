#include "analysis/PatternRequests.h"

#include <cmath>

namespace necwb::analysis {

auto fullPatternRequest(bool overGround) -> model::RadiationPatternRequest
{
    model::RadiationPatternRequest pattern;
    pattern.thetaCount = overGround ? 19 : 37;
    pattern.phiCount = 72;
    pattern.thetaStep = 5.0;
    pattern.phiStep = 5.0;
    return pattern;
}

auto azimuthCutRequest(double elevationDegrees, double stepDegrees)
    -> model::RadiationPatternRequest
{
    model::RadiationPatternRequest pattern;
    pattern.thetaCount = 1;
    pattern.thetaStart = necThetaForElevation(elevationDegrees);
    pattern.thetaStep = 0.0;
    pattern.phiCount = static_cast<int>(std::lround(360.0 / stepDegrees));
    pattern.phiStep = stepDegrees;
    return pattern;
}

auto elevationCutRequest(double azimuthDegrees, bool overGround, double stepDegrees)
    -> model::RadiationPatternRequest
{
    auto azimuth = patternDirection(90.0, azimuthDegrees).azimuthDegrees;
    // phi and phi + 180 are the same plane; keep both angles inside [0, 360].
    if (azimuth > 180.0) azimuth -= 180.0;
    model::RadiationPatternRequest pattern;
    pattern.thetaCount = static_cast<int>(std::lround((overGround ? 90.0 : 180.0)
        / stepDegrees)) + 1;
    pattern.thetaStep = stepDegrees;
    pattern.phiCount = 2;
    pattern.phiStart = azimuth;
    pattern.phiStep = 180.0;
    return pattern;
}

auto cutsThroughPeak(PatternDirection peak, bool overGround)
    -> std::array<model::RadiationPatternRequest, 2>
{
    return {azimuthCutRequest(peak.elevationDegrees),
        elevationCutRequest(peak.azimuthDegrees, overGround)};
}

auto defaultCutElevation(bool overGround) -> double
{
    return overGround ? 10.0 : 0.0;
}

auto hasGroundBelow(const model::ModelSetup& setup) -> bool
{
    return setup.ground && setup.ground->type != model::GroundType::FreeSpace;
}

}
