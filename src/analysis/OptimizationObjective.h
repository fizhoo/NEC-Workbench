#pragma once

#include "analysis/AnalysisResult.h"

#include <optional>
#include <span>

namespace necwb::analysis {

enum class OptimizationObjectiveKind {
    WorstPointAcrossFrequencies,
    SwrAtFrequency
};

struct OptimizationObjectiveSpec {
    OptimizationObjectiveKind kind{OptimizationObjectiveKind::WorstPointAcrossFrequencies};
    double referenceImpedance{50.0};
    double targetFrequencyMHz{};
    double swrWeight{1.0};
    double resistanceWeight{};
    double resistanceTargetOhms{50.0};
    double reactanceWeight{};
    double reactanceTargetOhms{};
};

struct OptimizationObjectiveResult {
    double score{};
    double swr{};
    double swrComponent{};
    double resistanceComponent{};
    double reactanceComponent{};
    std::optional<FeedpointResult> feedpoint;
};

[[nodiscard]] auto evaluateOptimizationObjective(std::span<const FeedpointResult> feedpoints,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>;

}
