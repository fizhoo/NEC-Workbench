#pragma once

#include "analysis/OptimizationObjective.h"

#include <optional>
#include <span>
#include <string_view>

namespace necwb::analysis {

enum class OptimizationPresetId {
    MinimizeSwr,
    SwrLimit,
    FeedpointMatch,
    PeakGain,
    FrontToBack,
    FrontToRear,
    SwrAndGain,
    Custom
};

struct OptimizationPresetDefinition {
    OptimizationPresetId id{};
    std::string_view stableId;
    bool requiresDirectionalSetup{};
};

struct OptimizationPresetParameters {
    double referenceImpedanceOhms{50.0};
    double swrLimit{2.0};
};

[[nodiscard]] auto optimizationPresetDefinitions()
    -> std::span<const OptimizationPresetDefinition>;
[[nodiscard]] auto optimizationPresetStableId(OptimizationPresetId id) -> std::string_view;
[[nodiscard]] auto optimizationPresetObjective(OptimizationPresetId id,
    const OptimizationPresetParameters& parameters = {})
    -> std::optional<OptimizationObjectiveSpec>;

}
