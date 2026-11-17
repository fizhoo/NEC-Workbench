#include "analysis/OptimizationObjective.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <variant>
#include <vector>

namespace necwb::analysis {
namespace {

constexpr auto frequencyToleranceMHz = 1.0e-6;

struct ReducedCriterion {
    double metric{};
    double component{};
    std::optional<std::size_t> index;
    std::optional<double> frequencyMHz;
    OptimizationMetricSummary summary;
};

auto feedpointAt(std::span<const FeedpointResult> feedpoints, double frequencyMHz)
    -> const FeedpointResult*
{
    const FeedpointResult* selected{};
    auto distance = std::numeric_limits<double>::infinity();
    for (const auto& feedpoint : feedpoints) {
        const auto candidateDistance = std::abs(feedpoint.frequencyMHz - frequencyMHz);
        if (candidateDistance < distance) {
            selected = &feedpoint;
            distance = candidateDistance;
        }
    }
    return selected != nullptr && distance <= frequencyToleranceMHz ? selected : nullptr;
}

auto metricSummary(std::span<const double> values, std::span<const double> frequenciesMHz)
    -> OptimizationMetricSummary
{
    const auto [minimum, maximum] = std::ranges::minmax_element(values);
    auto total = 0.0;
    for (const auto value : values) total += value;
    return {*minimum, total / values.size(), *maximum,
        frequenciesMHz[static_cast<std::size_t>(std::distance(values.begin(), minimum))],
        frequenciesMHz[static_cast<std::size_t>(std::distance(values.begin(), maximum))]};
}

auto effectiveAggregation(const OptimizationObjectiveSpec& objective,
    OptimizationGoal goal, OptimizationAggregation configured) -> OptimizationAggregation
{
    if (objective.kind == OptimizationObjectiveKind::AverageAcrossFrequencies)
        return OptimizationAggregation::Average;
    if (objective.kind != OptimizationObjectiveKind::WorstPointAcrossFrequencies)
        return configured;
    if (goal == OptimizationGoal::Maximize) return OptimizationAggregation::Minimum;
    return OptimizationAggregation::Maximum;
}

auto reducedValue(std::span<const double> values, OptimizationAggregation aggregation)
    -> std::pair<double, std::optional<std::size_t>>
{
    if (aggregation == OptimizationAggregation::Average) {
        auto total = 0.0;
        for (const auto value : values) total += value;
        return {total / values.size(), std::nullopt};
    }
    const auto iterator = aggregation == OptimizationAggregation::Minimum
        ? std::ranges::min_element(values) : std::ranges::max_element(values);
    return {*iterator, static_cast<std::size_t>(std::distance(values.begin(), iterator))};
}

auto reduceCriterion(std::span<const double> values, std::span<const double> frequenciesMHz,
    double weight, OptimizationGoal goal, double target,
    GoodEnoughDirection goodEnoughDirection, OptimizationAggregation aggregation,
    double scale, double totalWeight) -> ReducedCriterion
{
    const auto summary = metricSummary(values, frequenciesMHz);
    std::vector<double> costs;
    costs.reserve(values.size());
    for (const auto value : values) {
        switch (goal) {
        case OptimizationGoal::Minimize: costs.push_back(value / scale); break;
        case OptimizationGoal::Maximize: costs.push_back(-value / scale); break;
        case OptimizationGoal::Target:
            costs.push_back(std::abs(value - target) / scale);
            break;
        case OptimizationGoal::GoodEnough:
            costs.push_back((goodEnoughDirection == GoodEnoughDirection::AtMost
                    ? std::max(0.0, value - target)
                    : std::max(0.0, target - value)) / scale);
            break;
        }
    }

    const auto reduceCosts = goal == OptimizationGoal::Target
        || goal == OptimizationGoal::GoodEnough;
    const auto [reduced, index] = reducedValue(reduceCosts
            ? std::span<const double>{costs} : values,
        aggregation);
    double cost{};
    if (reduceCosts) {
        cost = reduced;
    } else if (goal == OptimizationGoal::Minimize) {
        cost = reduced / scale;
    } else {
        cost = -reduced / scale;
    }
    const auto metric = index ? values[*index] : summary.average;
    return {metric, weight * cost / totalWeight, index,
        index ? std::optional<double>{frequenciesMHz[*index]} : std::nullopt, summary};
}

auto directionalMetricsFor(const AnalysisResult& analysis,
    const OptimizationObjectiveSpec& objective) -> std::vector<RadiationFrequencyMetric>
{
    if (objective.directionalEvaluationMode
        == DirectionalEvaluationMode::SpecifiedDirection) {
        return radiationFrequencyMetrics(analysis.radiation,
            objective.radiationComponent, objective.forwardThetaDegrees,
            objective.forwardPhiDegrees, objective.frontToRearRegion);
    }
    const auto cutAngle = objective.directionalEvaluationMode
            == DirectionalEvaluationMode::StrongestInAzimuthCut
        ? objective.forwardThetaDegrees : objective.forwardPhiDegrees;
    return radiationTrackedCutFrequencyMetrics(analysis.radiation,
        objective.radiationComponent, objective.directionalEvaluationMode, cutAngle,
        objective.frontToRearRegion);
}

// What an objective reads from one analysis: its total weight, the frequencies it scores and
// the directional metrics at them.
struct ObjectivePlan {
    double totalWeight{};
    bool impedanceEnabled{};
    bool directionalEnabled{};
    std::vector<double> frequencies;
    std::vector<RadiationFrequencyMetric> directionalMetrics;
};

// The plan, or why the objective cannot be scored at any frequency.
auto planObjective(const AnalysisResult& analysis, const OptimizationObjectiveSpec& objective)
    -> std::variant<ObjectivePlan, OptimizationObjectiveFailureKind>
{
    const std::array weights{objective.swrWeight, objective.resistanceWeight,
        objective.reactanceWeight, objective.forwardGainWeight,
        objective.frontToBackWeight, objective.frontToRearWeight};
    ObjectivePlan plan;
    for (const auto weight : weights) {
        if (!std::isfinite(weight) || weight < 0.0)
            return OptimizationObjectiveFailureKind::InvalidConfiguration;
        plan.totalWeight += weight;
    }
    if (plan.totalWeight <= 0.0 || !std::isfinite(objective.referenceImpedance)
        || objective.referenceImpedance <= 0.0)
        return OptimizationObjectiveFailureKind::InvalidConfiguration;

    plan.impedanceEnabled = objective.swrWeight > 0.0
        || objective.resistanceWeight > 0.0 || objective.reactanceWeight > 0.0;
    plan.directionalEnabled = objective.forwardGainWeight > 0.0
        || objective.frontToBackWeight > 0.0 || objective.frontToRearWeight > 0.0;
    auto& frequencies = plan.frequencies;
    if (plan.impedanceEnabled) {
        for (const auto& feedpoint : analysis.feedpoints)
            frequencies.push_back(feedpoint.frequencyMHz);
    } else {
        for (const auto& sample : analysis.radiation)
            frequencies.push_back(sample.frequencyMHz);
    }
    std::ranges::sort(frequencies);
    const auto duplicates = std::ranges::unique(frequencies, [](double left, double right) {
        return std::abs(left - right) <= frequencyToleranceMHz;
    });
    frequencies.erase(duplicates.begin(), duplicates.end());
    if (frequencies.empty()) return OptimizationObjectiveFailureKind::NoFrequencyResults;
    if (objective.kind == OptimizationObjectiveKind::SwrAtFrequency) {
        const auto nearest = *std::ranges::min_element(frequencies, {},
            [target = objective.targetFrequencyMHz](double frequency) {
                return std::abs(frequency - target);
            });
        frequencies.assign(1, nearest);
    }
    if (plan.directionalEnabled) plan.directionalMetrics = directionalMetricsFor(analysis, objective);
    return plan;
}

// The measurements an objective scores at one frequency; each one it weights is present.
struct FrequencyInputs {
    const FeedpointResult* feedpoint{};
    double swr{};
    const RadiationFrequencyMetric* directional{};
};

// A metric is missing when its direction was not sampled, and null when it was sampled but
// gave no usable gain.
auto unavailable(RadiationMetricAvailability availability,
    OptimizationObjectiveFailureKind missing, OptimizationObjectiveFailureKind null)
    -> OptimizationObjectiveFailureKind
{
    return availability == RadiationMetricAvailability::Missing ? missing : null;
}

auto frequencyInputs(const AnalysisResult& analysis, const OptimizationObjectiveSpec& objective,
    const ObjectivePlan& plan, double frequencyMHz)
    -> std::variant<FrequencyInputs, OptimizationObjectiveFailureKind>
{
    using Failure = OptimizationObjectiveFailureKind;
    FrequencyInputs inputs;
    if (plan.impedanceEnabled) {
        inputs.feedpoint = feedpointAt(analysis.feedpoints, frequencyMHz);
        if (inputs.feedpoint == nullptr) return Failure::MissingImpedance;
        inputs.swr = standingWaveRatio(inputs.feedpoint->impedance, objective.referenceImpedance);
        if (!std::isfinite(inputs.swr)) return Failure::InvalidStandingWaveRatio;
    }
    if (!plan.directionalEnabled) return inputs;
    const auto directional = std::ranges::find_if(plan.directionalMetrics,
        [frequencyMHz](const auto& value) {
            return std::abs(value.frequencyMHz - frequencyMHz) <= frequencyToleranceMHz;
        });
    if (directional == plan.directionalMetrics.end()) return Failure::MissingForwardDirection;
    if (!directional->forwardGainDb)
        return unavailable(directional->forwardAvailability,
            Failure::MissingForwardDirection, Failure::NullForwardGain);
    if (objective.frontToBackWeight > 0.0 && !directional->frontToBackDb)
        return unavailable(directional->backAvailability,
            Failure::MissingBackDirection, Failure::NullBackGain);
    if (objective.frontToRearWeight > 0.0 && !directional->frontToRearDb)
        return unavailable(directional->rearAvailability,
            Failure::MissingRearCoverage, Failure::NullRearGain);
    inputs.directional = &*directional;
    return inputs;
}

auto evaluateObjective(const AnalysisResult& analysis,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>
{
    const auto planned = planObjective(analysis, objective);
    if (!std::holds_alternative<ObjectivePlan>(planned)) return std::nullopt;
    const auto& plan = std::get<ObjectivePlan>(planned);
    const auto& frequencies = plan.frequencies;
    const auto totalWeight = plan.totalWeight;
    const auto impedanceEnabled = plan.impedanceEnabled;

    std::vector<double> swrValues;
    std::vector<double> resistanceValues;
    std::vector<double> reactanceValues;
    std::vector<double> gainValues;
    std::vector<double> frontToBackValues;
    std::vector<double> frontToRearValues;
    OptimizationObjectiveResult result;

    for (const auto frequencyMHz : frequencies) {
        OptimizationFrequencyMetrics metrics;
        metrics.frequencyMHz = frequencyMHz;
        const auto measured = frequencyInputs(analysis, objective, plan, frequencyMHz);
        if (!std::holds_alternative<FrequencyInputs>(measured)) return std::nullopt;
        const auto& inputs = std::get<FrequencyInputs>(measured);
        if (const auto* feedpoint = inputs.feedpoint) {
            swrValues.push_back(inputs.swr);
            resistanceValues.push_back(feedpoint->impedance.real());
            reactanceValues.push_back(feedpoint->impedance.imag());
            metrics.swr = inputs.swr;
            metrics.resistanceOhms = feedpoint->impedance.real();
            metrics.reactanceOhms = feedpoint->impedance.imag();
        }
        if (const auto* directional = inputs.directional) {
            const auto frontGain = *directional->forwardGainDb;
            gainValues.push_back(frontGain);
            metrics.forwardGainDb = frontGain;
            metrics.forwardThetaDegrees = directional->forwardThetaDegrees;
            metrics.forwardPhiDegrees = directional->forwardPhiDegrees;
            if (objective.frontToBackWeight > 0.0) {
                frontToBackValues.push_back(*directional->frontToBackDb);
                metrics.frontToBackDb = directional->frontToBackDb;
            }
            if (objective.frontToRearWeight > 0.0) {
                frontToRearValues.push_back(*directional->frontToRearDb);
                metrics.frontToRearDb = directional->frontToRearDb;
            }
        }
        result.frequencyMetrics.push_back(std::move(metrics));
    }

    result.evaluatedFrequencyCount = frequencies.size();
    result.evaluationFrequencyMHz = frequencies.size() == 1 ? frequencies.front() : 0.0;
    const auto reduce = [&](std::span<const double> values, double weight,
                            OptimizationGoal goal, double target,
                            GoodEnoughDirection direction,
                            OptimizationAggregation aggregation, double scale) {
        return reduceCriterion(values, frequencies, weight, goal, target, direction,
            effectiveAggregation(objective, goal, aggregation), scale, totalWeight);
    };

    std::optional<ReducedCriterion> swr;
    std::optional<ReducedCriterion> resistance;
    std::optional<ReducedCriterion> reactance;
    std::optional<ReducedCriterion> gain;
    std::optional<ReducedCriterion> frontToBack;
    std::optional<ReducedCriterion> frontToRear;
    if (objective.swrWeight > 0.0)
        swr = reduce(swrValues, objective.swrWeight, objective.swrGoal,
            objective.swrTarget, objective.swrGoodEnoughDirection,
            objective.swrAggregation, 1.0);
    if (objective.resistanceWeight > 0.0)
        resistance = reduce(resistanceValues, objective.resistanceWeight,
            objective.resistanceGoal, objective.resistanceTargetOhms,
            objective.resistanceGoodEnoughDirection, objective.resistanceAggregation,
            objective.referenceImpedance);
    if (objective.reactanceWeight > 0.0)
        reactance = reduce(reactanceValues, objective.reactanceWeight,
            objective.reactanceGoal, objective.reactanceTargetOhms,
            objective.reactanceGoodEnoughDirection, objective.reactanceAggregation,
            objective.referenceImpedance);
    if (objective.forwardGainWeight > 0.0)
        gain = reduce(gainValues, objective.forwardGainWeight, objective.forwardGainGoal,
            objective.forwardGainTarget, objective.forwardGainGoodEnoughDirection,
            objective.forwardGainAggregation, 10.0);
    if (objective.frontToBackWeight > 0.0)
        frontToBack = reduce(frontToBackValues, objective.frontToBackWeight,
            objective.frontToBackGoal, objective.frontToBackTarget,
            objective.frontToBackGoodEnoughDirection, objective.frontToBackAggregation, 10.0);
    if (objective.frontToRearWeight > 0.0)
        frontToRear = reduce(frontToRearValues, objective.frontToRearWeight,
            objective.frontToRearGoal, objective.frontToRearTarget,
            objective.frontToRearGoodEnoughDirection, objective.frontToRearAggregation, 10.0);

    const auto add = [&result](const std::optional<ReducedCriterion>& criterion) {
        if (criterion) result.score += criterion->component;
    };
    add(swr); add(resistance); add(reactance); add(gain); add(frontToBack); add(frontToRear);
    if (objective.kind != OptimizationObjectiveKind::PerCriterion
        && objective.kind != OptimizationObjectiveKind::AverageAcrossFrequencies) {
        const auto representative = swr ? swr->frequencyMHz
            : resistance ? resistance->frequencyMHz
            : reactance ? reactance->frequencyMHz
            : gain ? gain->frequencyMHz
            : frontToBack ? frontToBack->frequencyMHz
            : frontToRear ? frontToRear->frequencyMHz : std::nullopt;
        if (representative) result.evaluationFrequencyMHz = *representative;
    }
    if (swr) {
        result.swr = swr->metric;
        result.swrComponent = swr->component;
        result.swrSummary = swr->summary;
    }
    if (resistance) result.resistanceComponent = resistance->component;
    if (reactance) result.reactanceComponent = reactance->component;
    if (gain) {
        result.forwardGainDb = gain->metric;
        result.forwardGainFrequencyMHz = gain->frequencyMHz;
        result.forwardGainComponent = gain->component;
        result.forwardGainSummary = gain->summary;
        if (gain->index) {
            result.forwardThetaDegrees = result.frequencyMetrics[*gain->index].forwardThetaDegrees;
            result.forwardPhiDegrees = result.frequencyMetrics[*gain->index].forwardPhiDegrees;
        }
    }
    if (frontToBack) {
        result.frontToBackDb = frontToBack->metric;
        result.frontToBackFrequencyMHz = frontToBack->frequencyMHz;
        if (frontToBack->index)
            result.backGainDb = gainValues[*frontToBack->index] - frontToBack->metric;
        result.frontToBackComponent = frontToBack->component;
        result.frontToBackSummary = frontToBack->summary;
    }
    if (frontToRear) {
        result.frontToRearDb = frontToRear->metric;
        result.frontToRearFrequencyMHz = frontToRear->frequencyMHz;
        if (frontToRear->index)
            result.rearGainDb = gainValues[*frontToRear->index] - frontToRear->metric;
        result.frontToRearComponent = frontToRear->component;
        result.frontToRearSummary = frontToRear->summary;
    }
    if (impedanceEnabled) {
        const auto resistanceMetric = resistance ? resistance->metric
            : metricSummary(resistanceValues, frequencies).average;
        const auto reactanceMetric = reactance ? reactance->metric
            : metricSummary(reactanceValues, frequencies).average;
        if (result.evaluationFrequencyMHz > 0.0) {
            if (const auto* representative = feedpointAt(
                    analysis.feedpoints, result.evaluationFrequencyMHz))
                result.feedpoint = *representative;
        }
        if (!result.feedpoint) {
            result.feedpoint = FeedpointResult{.frequencyMHz = result.evaluationFrequencyMHz,
                .wireTag = 0, .segment = 0, .voltage = {}, .current = {},
                .impedance = {resistanceMetric, reactanceMetric}, .inputPowerWatts = 0.0};
        }
        if (!swr) result.swr = metricSummary(swrValues, frequencies).average;
    }
    return result;
}

}

auto evaluateOptimizationObjective(std::span<const FeedpointResult> feedpoints,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>
{
    AnalysisResult analysis;
    analysis.feedpoints.assign(feedpoints.begin(), feedpoints.end());
    return evaluateObjective(analysis, objective);
}

auto evaluateOptimizationObjective(const AnalysisResult& analysis,
    const OptimizationObjectiveSpec& objective) -> std::optional<OptimizationObjectiveResult>
{
    return evaluateObjective(analysis, objective);
}

auto diagnoseOptimizationObjectiveFailure(const AnalysisResult& analysis,
    const OptimizationObjectiveSpec& objective) -> OptimizationObjectiveFailure
{
    const auto planned = planObjective(analysis, objective);
    if (const auto* failure = std::get_if<OptimizationObjectiveFailureKind>(&planned))
        return {*failure, std::nullopt};
    const auto& plan = std::get<ObjectivePlan>(planned);
    for (const auto frequencyMHz : plan.frequencies) {
        const auto measured = frequencyInputs(analysis, objective, plan, frequencyMHz);
        if (const auto* failure = std::get_if<OptimizationObjectiveFailureKind>(&measured))
            return {*failure, frequencyMHz};
    }
    return {OptimizationObjectiveFailureKind::Unknown, std::nullopt};
}

}
