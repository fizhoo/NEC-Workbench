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
[[nodiscard]] auto radiationCutMetrics(std::span<const RadiationCutPoint> samples,
    bool wraps) -> RadiationCutMetrics;

}
