#pragma once

#include "analysis/AnalysisResult.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace necwb::analysis {

enum class OptimizationObjectiveKind {
    PerCriterion,
    WorstPointAcrossFrequencies,
    AverageAcrossFrequencies,
    SwrAtFrequency
};

enum class OptimizationGoal {
    Minimize,
    Maximize,
    Target,
    GoodEnough
};

enum class OptimizationAggregation {
    Minimum,
    Average,
    Maximum
};

enum class GoodEnoughDirection {
    AtMost,
    AtLeast
};

struct OptimizationMetricSummary {
    double minimum{};
    double average{};
    double maximum{};
    double minimumFrequencyMHz{};
    double maximumFrequencyMHz{};
};

struct OptimizationFrequencyMetrics {
    double frequencyMHz{};
    std::optional<double> swr;
    std::optional<double> resistanceOhms;
    std::optional<double> reactanceOhms;
    std::optional<double> forwardGainDb;
    std::optional<double> frontToBackDb;
    std::optional<double> frontToRearDb;
};

struct OptimizationObjectiveSpec {
    OptimizationObjectiveKind kind{OptimizationObjectiveKind::PerCriterion};
    double referenceImpedance{50.0};
    double targetFrequencyMHz{};
    double swrWeight{1.0};
    double resistanceWeight{};
    double resistanceTargetOhms{50.0};
    double reactanceWeight{};
    double reactanceTargetOhms{};
    double forwardGainWeight{};
    double frontToBackWeight{};
    double frontToRearWeight{};
    OptimizationGoal swrGoal{OptimizationGoal::Minimize};
    OptimizationGoal resistanceGoal{OptimizationGoal::Target};
    OptimizationGoal reactanceGoal{OptimizationGoal::Target};
    OptimizationGoal forwardGainGoal{OptimizationGoal::Maximize};
    OptimizationGoal frontToBackGoal{OptimizationGoal::Maximize};
    OptimizationGoal frontToRearGoal{OptimizationGoal::Maximize};
    double swrTarget{2.0};
    double forwardGainTarget{};
    double frontToBackTarget{20.0};
    double frontToRearTarget{15.0};
    GoodEnoughDirection swrGoodEnoughDirection{GoodEnoughDirection::AtMost};
    GoodEnoughDirection resistanceGoodEnoughDirection{GoodEnoughDirection::AtMost};
    GoodEnoughDirection reactanceGoodEnoughDirection{GoodEnoughDirection::AtMost};
    GoodEnoughDirection forwardGainGoodEnoughDirection{GoodEnoughDirection::AtLeast};
    GoodEnoughDirection frontToBackGoodEnoughDirection{GoodEnoughDirection::AtLeast};
    GoodEnoughDirection frontToRearGoodEnoughDirection{GoodEnoughDirection::AtLeast};
    OptimizationAggregation swrAggregation{OptimizationAggregation::Maximum};
    OptimizationAggregation resistanceAggregation{OptimizationAggregation::Maximum};
    OptimizationAggregation reactanceAggregation{OptimizationAggregation::Maximum};
    OptimizationAggregation forwardGainAggregation{OptimizationAggregation::Minimum};
    OptimizationAggregation frontToBackAggregation{OptimizationAggregation::Minimum};
    OptimizationAggregation frontToRearAggregation{OptimizationAggregation::Minimum};
    double forwardThetaDegrees{90.0};
    double forwardPhiDegrees{};
    RadiationComponent radiationComponent{RadiationComponent::Total};
};

struct OptimizationObjectiveResult {
    double score{};
    double evaluationFrequencyMHz{};
    std::size_t evaluatedFrequencyCount{};
    double swr{};
    double swrComponent{};
    double resistanceComponent{};
    double reactanceComponent{};
    double forwardGainComponent{};
    double frontToBackComponent{};
    double frontToRearComponent{};
    std::optional<double> forwardGainDb;
    std::optional<double> forwardGainFrequencyMHz;
    std::optional<double> backGainDb;
    std::optional<double> frontToBackDb;
    std::optional<double> frontToBackFrequencyMHz;
    std::optional<double> rearGainDb;
    std::optional<double> frontToRearDb;
    std::optional<double> frontToRearFrequencyMHz;
    std::optional<OptimizationMetricSummary> swrSummary;
    std::optional<OptimizationMetricSummary> forwardGainSummary;
    std::optional<OptimizationMetricSummary> frontToBackSummary;
    std::optional<OptimizationMetricSummary> frontToRearSummary;
    std::vector<OptimizationFrequencyMetrics> frequencyMetrics;
    std::optional<FeedpointResult> feedpoint;
};

[[nodiscard]] auto evaluateOptimizationObjective(std::span<const FeedpointResult> feedpoints,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>;
[[nodiscard]] auto evaluateOptimizationObjective(const AnalysisResult& analysis,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>;

}
