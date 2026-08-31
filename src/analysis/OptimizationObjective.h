#pragma once

#include "analysis/AnalysisResult.h"

#include <optional>
#include <span>

namespace necwb::analysis {

enum class OptimizationObjectiveKind {
    MaximumSwr,
    SwrAtFrequency
};

struct OptimizationObjectiveSpec {
    OptimizationObjectiveKind kind{OptimizationObjectiveKind::MaximumSwr};
    double referenceImpedance{50.0};
    double targetFrequencyMHz{};
};

struct OptimizationObjectiveResult {
    double score{};
    double swr{};
    std::optional<FeedpointResult> feedpoint;
};

[[nodiscard]] auto evaluateOptimizationObjective(std::span<const FeedpointResult> feedpoints,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>;

}
