#include "analysis/OptimizationPreset.h"

#include <array>
#include <ranges>

namespace necwb::analysis {
namespace {

constexpr std::array Presets{
    OptimizationPresetDefinition{OptimizationPresetId::MinimizeSwr, "minimize-swr", false},
    OptimizationPresetDefinition{OptimizationPresetId::SwrLimit, "swr-limit", false},
    OptimizationPresetDefinition{OptimizationPresetId::FeedpointMatch, "feedpoint-match", false},
    OptimizationPresetDefinition{OptimizationPresetId::PeakGain, "peak-gain", true},
    OptimizationPresetDefinition{OptimizationPresetId::FrontToBack, "front-to-back", true},
    OptimizationPresetDefinition{OptimizationPresetId::FrontToRear, "front-to-rear", true},
    OptimizationPresetDefinition{OptimizationPresetId::SwrAndGain, "swr-and-gain", true},
    OptimizationPresetDefinition{OptimizationPresetId::Custom, "custom", false},
};

auto emptyObjective(const OptimizationPresetParameters& parameters)
    -> OptimizationObjectiveSpec
{
    OptimizationObjectiveSpec objective;
    objective.referenceImpedance = parameters.referenceImpedanceOhms;
    objective.swrWeight = 0.0;
    return objective;
}

void configureSwrLimit(OptimizationObjectiveSpec& objective,
    const OptimizationPresetParameters& parameters)
{
    objective.swrWeight = 1.0;
    objective.swrGoal = OptimizationGoal::GoodEnough;
    objective.swrTarget = parameters.swrLimit;
    objective.swrGoodEnoughDirection = GoodEnoughDirection::AtMost;
    objective.swrAggregation = OptimizationAggregation::Maximum;
}

void configureMinimizeSwr(OptimizationObjectiveSpec& objective)
{
    objective.swrWeight = 1.0;
    objective.swrGoal = OptimizationGoal::Minimize;
    objective.swrAggregation = OptimizationAggregation::Maximum;
}

void configureDirectionalObjective(OptimizationObjectiveSpec& objective)
{
    objective.directionalEvaluationMode = DirectionalEvaluationMode::StrongestInAzimuthCut;
}

}

auto optimizationPresetDefinitions() -> std::span<const OptimizationPresetDefinition>
{
    return Presets;
}

auto optimizationPresetStableId(OptimizationPresetId id) -> std::string_view
{
    const auto definition = std::ranges::find(Presets, id,
        &OptimizationPresetDefinition::id);
    return definition == Presets.end() ? std::string_view{} : definition->stableId;
}

auto optimizationPresetObjective(OptimizationPresetId id,
    const OptimizationPresetParameters& parameters)
    -> std::optional<OptimizationObjectiveSpec>
{
    if (id == OptimizationPresetId::Custom) return std::nullopt;

    auto objective = emptyObjective(parameters);
    switch (id) {
    case OptimizationPresetId::MinimizeSwr:
        configureMinimizeSwr(objective);
        break;
    case OptimizationPresetId::SwrLimit:
        configureSwrLimit(objective, parameters);
        break;
    case OptimizationPresetId::FeedpointMatch:
        objective.resistanceWeight = 1.0;
        objective.resistanceGoal = OptimizationGoal::Target;
        objective.resistanceTargetOhms = parameters.referenceImpedanceOhms;
        objective.resistanceAggregation = OptimizationAggregation::Maximum;
        objective.reactanceWeight = 1.0;
        objective.reactanceGoal = OptimizationGoal::Target;
        objective.reactanceTargetOhms = 0.0;
        objective.reactanceAggregation = OptimizationAggregation::Maximum;
        break;
    case OptimizationPresetId::PeakGain:
        objective.forwardGainWeight = 1.0;
        objective.forwardGainGoal = OptimizationGoal::Maximize;
        objective.forwardGainAggregation = OptimizationAggregation::Minimum;
        configureDirectionalObjective(objective);
        break;
    case OptimizationPresetId::FrontToBack:
        objective.frontToBackWeight = 1.0;
        objective.frontToBackGoal = OptimizationGoal::Maximize;
        objective.frontToBackAggregation = OptimizationAggregation::Minimum;
        configureDirectionalObjective(objective);
        break;
    case OptimizationPresetId::FrontToRear:
        objective.frontToRearWeight = 1.0;
        objective.frontToRearGoal = OptimizationGoal::Maximize;
        objective.frontToRearAggregation = OptimizationAggregation::Minimum;
        configureDirectionalObjective(objective);
        break;
    case OptimizationPresetId::SwrAndGain:
        configureSwrLimit(objective, parameters);
        objective.forwardGainWeight = 1.0;
        objective.forwardGainGoal = OptimizationGoal::Maximize;
        objective.forwardGainAggregation = OptimizationAggregation::Minimum;
        configureDirectionalObjective(objective);
        break;
    case OptimizationPresetId::Custom:
        break;
    }
    return objective;
}

}
