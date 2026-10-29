#pragma once

#include "analysis/AnalysisResult.h"

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
};

struct ResultSummary {
    std::optional<FeedpointSummary> selectedFeedpoint;
    std::optional<MinimumSwrSummary> minimumSwr;
    std::optional<PeakGainSummary> peakGain;
};

[[nodiscard]] auto summarizeResults(const AnalysisResult& result,
    double selectedFrequencyMHz,
    std::optional<FeedpointIdentity> selectedSource = std::nullopt) -> ResultSummary;

}
