#pragma once

#include <complex>
#include <limits>
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

struct RadiationSample {
    double frequencyMHz{};
    double thetaDegrees{};
    double phiDegrees{};
    double verticalGainDb{};
    double horizontalGainDb{};
    double totalGainDb{};
};

struct AnalysisResult {
    std::vector<FeedpointResult> feedpoints;
    std::vector<SegmentCurrentResult> currents;
    std::vector<RadiationSample> radiation;
};

[[nodiscard]] auto standingWaveRatio(std::complex<double> impedance,
    double referenceImpedance = 50.0) -> double;

}
