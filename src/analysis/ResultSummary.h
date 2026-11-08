#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/GainDirection.h"

#include <complex>
#include <optional>

namespace necwb::analysis {

struct FeedpointIdentity {
    int wireTag{};
    int segment{};

    auto operator==(const FeedpointIdentity&) const -> bool = default;
};

struct FeedpointSummary {
    double frequencyMHz{};
    FeedpointIdentity source;
    std::complex<double> impedance;
    double swr{};
};

struct MinimumSwrSummary {
    double frequencyMHz{};
    FeedpointIdentity source;
    double swr{};
};

enum class RadiationDatasetKind {
    Point,
    AzimuthCut,
    ElevationCut,
    FullPattern
};

struct PeakGainSummary {
    double frequencyMHz{};
    double gainDb{};
    double thetaDegrees{};
    double phiDegrees{};
    int patternIndex{};
    RadiationDatasetKind datasetKind{RadiationDatasetKind::Point};
    // RP step next to the peak sample; see patternStepAround.
    std::optional<SampleStep> sampleStep;
};

struct ResultSummary {
    std::optional<FeedpointSummary> selectedFeedpoint;
    std::optional<MinimumSwrSummary> minimumSwr;
    std::optional<PeakGainSummary> selectedGain;
    std::optional<double> selectedFrontToBackDb;
    // Rear is the forward azimuth cut more than 90 degrees from the selected peak.
    std::optional<double> selectedFrontToRearDb;
    // Why F/R is missing when there is a selected peak: no rear samples, or only nulls.
    RadiationMetricAvailability selectedFrontToRearAvailability{
        RadiationMetricAvailability::Missing};
    std::optional<PeakGainSummary> peakGain;
};

// SWR uses referenceImpedanceOhms when given, for a Z0 changed after the run.
[[nodiscard]] auto summarizeResults(const AnalysisResult& result,
    double selectedFrequencyMHz,
    std::optional<FeedpointIdentity> selectedSource = std::nullopt,
    std::optional<double> referenceImpedanceOhms = std::nullopt) -> ResultSummary;

}
