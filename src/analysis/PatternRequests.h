#pragma once

#include "analysis/GainDirection.h"
#include "model/ModelSetup.h"

#include <array>

namespace necwb::analysis {

// RP request presets. Over ground nothing radiates below the horizon, so requests stop
// there instead of spending half their samples underground.

// 5-degree grid in theta and phi.
[[nodiscard]] auto fullPatternRequest(bool overGround) -> model::RadiationPatternRequest;
// Every azimuth at one elevation.
[[nodiscard]] auto azimuthCutRequest(double elevationDegrees, double stepDegrees = 1.0)
    -> model::RadiationPatternRequest;
// The vertical plane through an azimuth, front and back.
[[nodiscard]] auto elevationCutRequest(double azimuthDegrees, bool overGround,
    double stepDegrees = 1.0) -> model::RadiationPatternRequest;
// The 1-degree azimuth and elevation cuts that cross at a pattern peak.
[[nodiscard]] auto cutsThroughPeak(PatternDirection peak, bool overGround)
    -> std::array<model::RadiationPatternRequest, 2>;
// A starting elevation for an azimuth cut when no pattern has been run: the horizon in
// free space, a typical low takeoff angle over ground.
[[nodiscard]] auto defaultCutElevation(bool overGround) -> double;
[[nodiscard]] auto hasGroundBelow(const model::ModelSetup& setup) -> bool;

}
