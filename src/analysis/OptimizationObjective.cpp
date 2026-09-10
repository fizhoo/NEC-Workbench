#include "analysis/OptimizationObjective.h"

#include <cmath>
#include <limits>

namespace necwb::analysis {
namespace {

struct ObjectiveScore {
    double total{};
    double swr{};
    double resistance{};
    double reactance{};
};

auto objectiveScore(const FeedpointResult& feedpoint,
    const OptimizationObjectiveSpec& objective) -> std::optional<ObjectiveScore>
{
    const auto totalWeight = objective.swrWeight + objective.resistanceWeight
        + objective.reactanceWeight;
    if (objective.swrWeight < 0.0 || objective.resistanceWeight < 0.0
        || objective.reactanceWeight < 0.0
        || !std::isfinite(totalWeight) || totalWeight <= 0.0
        || !std::isfinite(objective.referenceImpedance)
        || objective.referenceImpedance <= 0.0) {
        return std::nullopt;
    }

    ObjectiveScore score;
    if (objective.swrWeight > 0.0) {
        const auto swr = standingWaveRatio(feedpoint.impedance, objective.referenceImpedance);
        score.swr = objective.swrWeight * (std::isfinite(swr) ? swr : 1.0e12)
            / totalWeight;
    }
    if (objective.resistanceWeight > 0.0) {
        if (!std::isfinite(feedpoint.impedance.real())
            || !std::isfinite(objective.resistanceTargetOhms)) return std::nullopt;
        score.resistance = objective.resistanceWeight
            * std::abs(feedpoint.impedance.real() - objective.resistanceTargetOhms)
            / objective.referenceImpedance / totalWeight;
    }
    if (objective.reactanceWeight > 0.0) {
        if (!std::isfinite(feedpoint.impedance.imag())
            || !std::isfinite(objective.reactanceTargetOhms)) return std::nullopt;
        score.reactance = objective.reactanceWeight
            * std::abs(feedpoint.impedance.imag() - objective.reactanceTargetOhms)
            / objective.referenceImpedance / totalWeight;
    }
    score.total = score.swr + score.resistance + score.reactance;
    return score;
}

}

auto evaluateOptimizationObjective(std::span<const FeedpointResult> feedpoints,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>
{
    const FeedpointResult* selected{};
    auto selectedScore = objective.kind == OptimizationObjectiveKind::WorstPointAcrossFrequencies
        ? -std::numeric_limits<double>::infinity()
        : std::numeric_limits<double>::infinity();
    ObjectiveScore selectedComponents;
    auto nearestDistance = std::numeric_limits<double>::infinity();

    for (const auto& feedpoint : feedpoints) {
        const auto score = objectiveScore(feedpoint, objective);
        if (!score) continue;
        if (objective.kind == OptimizationObjectiveKind::WorstPointAcrossFrequencies) {
            if (score->total > selectedScore) {
                selected = &feedpoint;
                selectedScore = score->total;
                selectedComponents = *score;
            }
            continue;
        }
        const auto distance = std::abs(feedpoint.frequencyMHz - objective.targetFrequencyMHz);
        if (distance < nearestDistance) {
            selected = &feedpoint;
            selectedScore = score->total;
            selectedComponents = *score;
            nearestDistance = distance;
        }
    }

    if (selected == nullptr) return std::nullopt;
    return OptimizationObjectiveResult{selectedScore,
        standingWaveRatio(selected->impedance, objective.referenceImpedance),
        selectedComponents.swr, selectedComponents.resistance, selectedComponents.reactance,
        *selected};
}

}
