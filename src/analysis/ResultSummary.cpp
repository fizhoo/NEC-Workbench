#include "analysis/ResultSummary.h"

#include "analysis/FrequencyComparison.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace necwb::analysis {
namespace {

auto matchesSource(const FeedpointResult& result, const FeedpointIdentity& source) -> bool
{
    return result.wireTag == source.wireTag && result.segment == source.segment;
}

auto validGain(double gainDb) -> bool
{
    return std::isfinite(gainDb) && gainDb > -900.0;
}

auto distinctValues(std::vector<double> values) -> std::size_t
{
    std::ranges::sort(values);
    auto count = std::size_t{};
    auto previous = 0.0;
    for (const auto value : values) {
        if (count == 0 || !nearlyEqual(value, previous)) {
            ++count;
            previous = value;
        }
    }
    return count;
}

auto datasetKindFor(const AnalysisResult& result, const RadiationSample& peak)
    -> RadiationDatasetKind
{
    std::vector<double> thetaValues;
    std::vector<double> phiValues;
    for (const auto& sample : result.radiation) {
        if (!nearlyEqual(sample.frequencyMHz, peak.frequencyMHz)
            || sample.patternIndex != peak.patternIndex) continue;
        thetaValues.push_back(sample.thetaDegrees);
        phiValues.push_back(sample.phiDegrees);
    }
    const auto thetaCount = distinctValues(std::move(thetaValues));
    const auto phiCount = distinctValues(std::move(phiValues));
    if (thetaCount > 1 && phiCount > 1) return RadiationDatasetKind::FullPattern;
    if (phiCount > 1) return RadiationDatasetKind::AzimuthCut;
    if (thetaCount > 1) return RadiationDatasetKind::ElevationCut;
    return RadiationDatasetKind::Point;
}

}

auto summarizeResults(const AnalysisResult& result, double selectedFrequencyMHz,
    std::optional<FeedpointIdentity> selectedSource) -> ResultSummary
{
    ResultSummary summary;
    const FeedpointResult* selected{};
    auto nearestDistance = std::numeric_limits<double>::infinity();
    for (const auto& feedpoint : result.feedpoints) {
        if (selectedSource && !matchesSource(feedpoint, *selectedSource)) continue;
        const auto distance = std::abs(feedpoint.frequencyMHz - selectedFrequencyMHz);
        if (selected == nullptr || distance < nearestDistance) {
            selected = &feedpoint;
            nearestDistance = distance;
        }
    }
    if (selected != nullptr) {
        const FeedpointIdentity source{selected->wireTag, selected->segment};
        summary.selectedFeedpoint = FeedpointSummary{
            selected->frequencyMHz,
            source,
            selected->impedance,
            standingWaveRatio(selected->impedance, result.referenceImpedanceOhms),
        };
        for (const auto& feedpoint : result.feedpoints) {
            if (!matchesSource(feedpoint, source)) continue;
            const auto swr = standingWaveRatio(
                feedpoint.impedance, result.referenceImpedanceOhms);
            if (!std::isfinite(swr)
                || (summary.minimumSwr && swr >= summary.minimumSwr->swr)) continue;
            summary.minimumSwr = MinimumSwrSummary{
                feedpoint.frequencyMHz, source, swr};
        }
    }

    const RadiationSample* peak{};
    for (const auto& sample : result.radiation) {
        if (!validGain(sample.totalGainDb)
            || (peak != nullptr && sample.totalGainDb <= peak->totalGainDb)) continue;
        peak = &sample;
    }
    if (peak != nullptr) {
        summary.peakGain = PeakGainSummary{
            peak->frequencyMHz,
            peak->totalGainDb,
            peak->thetaDegrees,
            peak->phiDegrees,
            peak->patternIndex,
            datasetKindFor(result, *peak),
        };
    }
    return summary;
}

}
