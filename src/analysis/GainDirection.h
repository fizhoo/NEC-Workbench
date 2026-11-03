#pragma once

#include "analysis/AnalysisResult.h"

#include <optional>
#include <span>
#include <string>

namespace necwb::analysis {

// Decimal places for gains and other displayed numbers, shared with the UI helpers.
inline constexpr auto DisplayDecimalPlaces = 3;

// RP steps wider than this around a peak can hide the true maximum between samples.
inline constexpr auto CoarsePatternStepDegrees = 5.0;
// Peaks on steps wider than this, up to CoarsePatternStepDegrees, get a soft grid note:
// fine for locating a lobe, a little rough for its exact angle and gain.
inline constexpr auto FinePatternStepDegrees = 2.0;

enum class SampleStepPrecision {
    Fine,   // step of FinePatternStepDegrees or less, or not a sampled peak
    Grid,   // up to CoarsePatternStepDegrees: "(5° grid)"
    Coarse  // wider: "(sampled, RP step 10°)"
};

// A physical direction as antenna users read it: elevation above the horizon and azimuth.
struct PatternDirection {
    double elevationDegrees{};
    double azimuthDegrees{};
};

// NEC theta is measured from the zenith; negative theta at phi is the same direction as
// positive theta at phi + 180. The result has azimuth in [0, 360).
[[nodiscard]] auto patternDirection(double thetaDegrees, double phiDegrees) -> PatternDirection;
[[nodiscard]] auto necThetaForElevation(double elevationDegrees) -> double;

// The RP step next to a sampled peak, as entered on the RP card, and the true angular
// spacing it gives there. Azimuth steps shrink toward the zenith, so precision is judged
// from the spacing while notes quote the RP step.
struct SampleStep {
    double rpStepDegrees{};
    double spacingDegrees{};
};

struct GainReading {
    double gainDb{};
    std::optional<double> thetaDegrees;
    std::optional<double> phiDegrees;
    RadiationComponent component{RadiationComponent::Total};
    std::optional<double> frequencyMHz;
    // Set when the reading is a sampled peak; see SampleStepPrecision.
    std::optional<SampleStep> sampleStep;
};

struct GainText {
    std::string text;
    std::string toolTip;
};

[[nodiscard]] auto formatDisplayDecimal(double value) -> std::string;
[[nodiscard]] auto formatAngle(double degrees) -> std::string;
// "El 10°, Az 45°"
[[nodiscard]] auto formatDirection(double thetaDegrees, double phiDegrees) -> std::string;
// "θ 80°, φ 45°"
[[nodiscard]] auto formatNecDirection(double thetaDegrees, double phiDegrees) -> std::string;
// "Total", "Vertical", ...
[[nodiscard]] auto radiationComponentName(RadiationComponent component) -> std::string;
// "6.670 dBi @ El 0°, Az 0°"; non-total components are named after the unit.
[[nodiscard]] auto formatGain(const GainReading& reading) -> GainText;
// Both take the true angular spacing (SampleStep::spacingDegrees).
[[nodiscard]] auto isCoarseSampleStep(std::optional<double> spacingDegrees) -> bool;
[[nodiscard]] auto sampleStepPrecision(std::optional<double> spacingDegrees) -> SampleStepPrecision;
[[nodiscard]] auto coarseSampleNote(double stepDegrees) -> std::string;

// "Azimuth cut at El 10°" for a fixed-theta cut.
[[nodiscard]] auto azimuthCutName(double thetaDegrees) -> std::string;
// "Elevation cut at Az 45°" for the vertical plane through phi.
[[nodiscard]] auto elevationCutName(double phiDegrees) -> std::string;
// "0 dB = 6.670 dBi"
[[nodiscard]] auto referenceLevelText(double referenceDbi) -> std::string;

[[nodiscard]] auto frontToBackDefinition() -> std::string;
[[nodiscard]] auto frontToRearDefinition(DirectionalEvaluationMode mode,
    FrontToRearRegion region = FrontToRearRegion::ForwardAzimuthCut) -> std::string;
// "F/R" or "F/R (3D rear hemisphere)"
[[nodiscard]] auto frontToRearName(FrontToRearRegion region) -> std::string;

// The widest spacing to the given sample's neighbours along theta and phi, among samples
// at the same frequency and pattern dataset. Phi steps are scaled by sin(theta) to their
// true spacing on the sphere. Empty when there are no neighbours.
[[nodiscard]] auto patternStepAround(std::span<const RadiationSample> samples,
    const RadiationSample& peak) -> std::optional<SampleStep>;

}
