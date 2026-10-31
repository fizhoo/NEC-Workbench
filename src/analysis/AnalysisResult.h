#pragma once

#include <complex>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace necwb::analysis {

struct FeedpointResult {
    double frequencyMHz{};
    int wireTag{};
    int segment{};
    std::complex<double> voltage;
    std::complex<double> current;
    std::complex<double> impedance;
    double inputPowerWatts{};
};

struct SegmentCurrentResult {
    double frequencyMHz{};
    int segment{};
    int wireTag{};
    double xWavelengths{};
    double yWavelengths{};
    double zWavelengths{};
    double lengthWavelengths{};
    std::complex<double> current;
    double magnitude{};
    double phaseDegrees{};
    // NEC's absolute segment number. `segment` counts within the tag, which cannot tell
    // apart wires that share a tag; this identifies the physical wire. 0 when unknown.
    int necSegment{};
};

enum class PolarizationSense {
    Unknown,
    Linear,
    RightHand,
    LeftHand
};

enum class RadiationComponent {
    Total,
    Vertical,
    Horizontal,
    RightHandCircular,
    LeftHandCircular
};

enum class RadiationScale {
    Absolute,
    Normalized
};

enum class DirectionalEvaluationMode {
    SpecifiedDirection,
    StrongestInAzimuthCut,
    StrongestInElevationCut
};

struct RadiationSample {
    double frequencyMHz{};
    double thetaDegrees{};
    double phiDegrees{};
    double verticalGainDb{};
    double horizontalGainDb{};
    double totalGainDb{};
    double axialRatio{};
    double tiltDegrees{};
    PolarizationSense polarizationSense{PolarizationSense::Unknown};
    int patternIndex{};
};

struct RadiationMetrics {
    bool valid{};
    double peakGainDb{};
    double peakThetaDegrees{};
    double peakPhiDegrees{};
    std::vector<RadiationSample> tiedPeaks;
    std::optional<double> frontToBackDb;
};

enum class RadiationMetricAvailability {
    Missing,
    PatternNull,
    Available
};

struct RadiationFrequencyMetric {
    double frequencyMHz{};
    RadiationMetricAvailability forwardAvailability{RadiationMetricAvailability::Missing};
    RadiationMetricAvailability backAvailability{RadiationMetricAvailability::Missing};
    RadiationMetricAvailability rearAvailability{RadiationMetricAvailability::Missing};
    std::optional<double> forwardGainDb;
    std::optional<double> frontToBackDb;
    std::optional<double> frontToRearDb;
    std::optional<double> forwardThetaDegrees;
    std::optional<double> forwardPhiDegrees;
};

struct RadiationPeak {
    double frequencyMHz{};
    RadiationMetrics metrics;
    std::size_t sampleCount{};
};

struct RadiationCutPoint {
    double angleDegrees{};
    double thetaDegrees{};
    double phiDegrees{};
    double gainDb{};
};

struct RadiationCutMetrics {
    bool valid{};
    double peakAngleDegrees{};
    double peakGainDb{};
    std::vector<double> tiedPeakAnglesDegrees;
    std::optional<double> sampledBeamwidthDegrees;
    std::optional<double> interpolatedBeamwidthDegrees;
    std::optional<double> frontToBackDb;
};

struct RadiationDisplaySettings {
    double frequencyMHz{};
    RadiationComponent component{RadiationComponent::Total};
    RadiationScale scale{RadiationScale::Normalized};
    double floorDb{-40.0};

    auto operator==(const RadiationDisplaySettings&) const -> bool = default;
};

struct AnalysisResult {
    std::vector<FeedpointResult> feedpoints;
    std::vector<SegmentCurrentResult> currents;
    std::vector<RadiationSample> radiation;
    std::optional<double> averagePowerGain;
    std::optional<double> averagingSolidAnglePi;
    double referenceImpedanceOhms{50.0};
};

[[nodiscard]] auto standingWaveRatio(std::complex<double> impedance,
    double referenceImpedance = 50.0) -> double;
[[nodiscard]] auto radiationGainDb(const RadiationSample& sample,
    RadiationComponent component) -> double;
[[nodiscard]] auto radiationMetrics(std::span<const RadiationSample> samples,
    RadiationComponent component) -> RadiationMetrics;
[[nodiscard]] auto radiationPeakAtNearestFrequency(
    std::span<const RadiationSample> samples, RadiationComponent component,
    double targetFrequencyMHz) -> std::optional<RadiationPeak>;
[[nodiscard]] auto radiationFrequencyMetrics(std::span<const RadiationSample> samples,
    RadiationComponent component, double forwardThetaDegrees,
    double forwardPhiDegrees) -> std::vector<RadiationFrequencyMetric>;
[[nodiscard]] auto radiationTrackedCutFrequencyMetrics(
    std::span<const RadiationSample> samples, RadiationComponent component,
    DirectionalEvaluationMode mode, double cutAngleDegrees)
    -> std::vector<RadiationFrequencyMetric>;
[[nodiscard]] auto radiationCutMetrics(std::span<const RadiationCutPoint> samples,
    bool wraps) -> RadiationCutMetrics;

}
