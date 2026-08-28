#pragma once

#include <complex>
#include <limits>
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
};

struct RadiationMetrics {
    bool valid{};
    double peakGainDb{};
    double peakThetaDegrees{};
    double peakPhiDegrees{};
    double frontToBackDb{};
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
};

[[nodiscard]] auto standingWaveRatio(std::complex<double> impedance,
    double referenceImpedance = 50.0) -> double;
[[nodiscard]] auto radiationGainDb(const RadiationSample& sample,
    RadiationComponent component) -> double;
[[nodiscard]] auto radiationMetrics(std::span<const RadiationSample> samples,
    RadiationComponent component) -> RadiationMetrics;

}
