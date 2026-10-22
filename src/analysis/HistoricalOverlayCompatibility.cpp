#include "analysis/HistoricalOverlayCompatibility.h"

#include "analysis/FrequencyComparison.h"
#include "analysis/OptimizationBaseline.h"

#include <algorithm>

namespace necwb::analysis {
namespace {

auto impedanceCompatibility(const AnalysisResult& current,
    const AnalysisResult& historical) -> HistoricalOverlayStatus
{
    if (current.feedpoints.empty() || historical.feedpoints.empty())
        return HistoricalOverlayStatus::NoMatchingData;

    auto matched = std::size_t{};
    for (const auto& sample : current.feedpoints) {
        const auto found = std::ranges::any_of(historical.feedpoints,
            [&sample](const auto& candidate) {
                return candidate.wireTag == sample.wireTag
                    && candidate.segment == sample.segment
                    && nearlyEqual(candidate.frequencyMHz, sample.frequencyMHz);
            });
        if (found) ++matched;
    }
    if (matched == 0) return HistoricalOverlayStatus::NoMatchingData;
    if (matched != current.feedpoints.size()
        || !nearlyEqual(current.referenceImpedanceOhms,
            historical.referenceImpedanceOhms)) return HistoricalOverlayStatus::Partial;
    return HistoricalOverlayStatus::Compatible;
}

auto radiationCompatibility(const AnalysisResult& current,
    const AnalysisResult& historical) -> HistoricalOverlayStatus
{
    const auto currentCoverage = optimizationResultCoverage(current).radiation;
    const auto historicalCoverage = optimizationResultCoverage(historical).radiation;
    if (currentCoverage.empty() || historicalCoverage.empty())
        return HistoricalOverlayStatus::NoMatchingData;

    auto matched = std::size_t{};
    for (const auto& required : currentCoverage) {
        const auto found = std::ranges::any_of(historicalCoverage,
            [&required](const auto& available) {
                return available.patternIndex == required.patternIndex
                    && nearlyEqual(available.frequencyMHz, required.frequencyMHz)
                    && available.sampleCount == required.sampleCount
                    && available.directionFingerprint == required.directionFingerprint;
            });
        if (found) ++matched;
    }
    if (matched == 0) return HistoricalOverlayStatus::NoMatchingData;
    return matched == currentCoverage.size()
        ? HistoricalOverlayStatus::Compatible : HistoricalOverlayStatus::Partial;
}

}

auto evaluateHistoricalOverlayCompatibility(const AnalysisResult& current,
    const AnalysisResult& historical) -> HistoricalOverlayCompatibility
{
    return {
        impedanceCompatibility(current, historical),
        radiationCompatibility(current, historical),
    };
}

}
