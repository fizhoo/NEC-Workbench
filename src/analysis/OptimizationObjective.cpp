#include "analysis/OptimizationObjective.h"

#include <cmath>
#include <limits>

namespace necwb::analysis {

auto evaluateOptimizationObjective(std::span<const FeedpointResult> feedpoints,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>
{
    const FeedpointResult* selected{};
    auto selectedSwr = objective.kind == OptimizationObjectiveKind::MaximumSwr
        ? -std::numeric_limits<double>::infinity()
        : std::numeric_limits<double>::infinity();
    auto nearestDistance = std::numeric_limits<double>::infinity();

    for (const auto& feedpoint : feedpoints) {
        const auto swr = standingWaveRatio(feedpoint.impedance, objective.referenceImpedance);
        if (!std::isfinite(swr)) continue;
        if (objective.kind == OptimizationObjectiveKind::MaximumSwr) {
            if (swr > selectedSwr) {
                selected = &feedpoint;
                selectedSwr = swr;
            }
            continue;
        }
        const auto distance = std::abs(feedpoint.frequencyMHz - objective.targetFrequencyMHz);
        if (distance < nearestDistance) {
            selected = &feedpoint;
            selectedSwr = swr;
            nearestDistance = distance;
        }
    }

    if (selected == nullptr) return std::nullopt;
    return OptimizationObjectiveResult{selectedSwr, selectedSwr, *selected};
}

}
