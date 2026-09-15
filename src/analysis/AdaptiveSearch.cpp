#include "analysis/AdaptiveSearch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace necwb::analysis {

AdaptiveSearch::AdaptiveSearch(AdaptiveSearchSettings settings)
    : settings_(settings), previousBestScore_(std::numeric_limits<double>::infinity())
{
}

auto AdaptiveSearch::initialCandidates() const -> std::vector<double>
{
    if (!std::isfinite(settings_.minimum) || !std::isfinite(settings_.maximum)
        || settings_.minimum >= settings_.maximum || settings_.maximumEvaluations < 2) return {};
    const auto count = std::min(5, settings_.maximumEvaluations);
    std::vector<double> candidates;
    candidates.reserve(static_cast<std::size_t>(count));
    for (auto index = 0; index < count; ++index) {
        const auto fraction = static_cast<double>(index) / static_cast<double>(count - 1);
        candidates.push_back(settings_.minimum
            + fraction * (settings_.maximum - settings_.minimum));
    }
    return candidates;
}

void AdaptiveSearch::record(double value, std::optional<double> score)
{
    if (!std::isfinite(value)) return;
    if (score && !std::isfinite(*score)) score.reset();
    observations_.push_back({value, score});
}

auto AdaptiveSearch::nextCandidates() -> std::vector<double>
{
    if (static_cast<int>(observations_.size()) >= settings_.maximumEvaluations) {
        stopReason_ = AdaptiveStopReason::MaximumEvaluations;
        return {};
    }
    std::vector<const Observation*> successful;
    for (const auto& observation : observations_)
        if (observation.score) successful.push_back(&observation);
    if (successful.empty()) {
        stopReason_ = AdaptiveStopReason::NoSuccessfulCandidate;
        return {};
    }
    std::ranges::sort(successful, {}, &Observation::value);
    const auto best = std::ranges::min_element(successful,
        [](const auto* first, const auto* second) { return *first->score < *second->score; });
    const auto bestScore = *(*best)->score;
    if (std::isfinite(previousBestScore_)) {
        const auto improvement = previousBestScore_ - bestScore;
        stagnantRounds_ = improvement <= settings_.scoreTolerance ? stagnantRounds_ + 1 : 0;
        if (stagnantRounds_ >= 2) {
            stopReason_ = AdaptiveStopReason::ScoreTolerance;
            return {};
        }
    }
    previousBestScore_ = bestScore;

    std::vector<double> candidates;
    const auto addMidpoint = [&](const Observation* neighbor) {
        if (neighbor == nullptr) return;
        const auto midpoint = ((*best)->value + neighbor->value) / 2.0;
        if (std::abs(midpoint - (*best)->value) < settings_.parameterTolerance) return;
        const auto duplicate = std::ranges::any_of(observations_, [midpoint](const auto& item) {
            return std::abs(item.value - midpoint)
                <= std::max(1.0, std::abs(midpoint)) * 1.0e-12;
        });
        if (!duplicate) candidates.push_back(midpoint);
    };
    if (best != successful.begin()) addMidpoint(*(best - 1));
    if (best + 1 != successful.end()) addMidpoint(*(best + 1));
    if (candidates.empty()) {
        stopReason_ = AdaptiveStopReason::ParameterTolerance;
        return {};
    }
    const auto remaining = settings_.maximumEvaluations
        - static_cast<int>(observations_.size());
    if (static_cast<int>(candidates.size()) > remaining) candidates.resize(remaining);
    if (!candidates.empty()) ++refinementRound_;
    return candidates;
}

auto AdaptiveSearch::stopReason() const noexcept -> AdaptiveStopReason
{
    return stopReason_;
}

auto AdaptiveSearch::refinementRound() const noexcept -> int
{
    return refinementRound_;
}

AdaptiveVectorSearch::AdaptiveVectorSearch(AdaptiveVectorSearchSettings settings)
    : settings_(std::move(settings)), previousBestScore_(std::numeric_limits<double>::infinity())
{
    steps_.reserve(settings_.variables.size());
    for (const auto& variable : settings_.variables)
        steps_.push_back((variable.maximum - variable.minimum) / 4.0);
}

auto AdaptiveVectorSearch::initialCandidates() const
    -> std::vector<AdaptiveVectorProposal>
{
    if (settings_.variables.empty() || settings_.maximumEvaluations < 1) return {};
    std::vector<double> center;
    center.reserve(settings_.variables.size());
    for (const auto& variable : settings_.variables) {
        if (!std::isfinite(variable.minimum) || !std::isfinite(variable.maximum)
            || variable.minimum >= variable.maximum || !std::isfinite(variable.tolerance)
            || variable.tolerance <= 0.0) return {};
        center.push_back((variable.minimum + variable.maximum) / 2.0);
    }
    std::vector<AdaptiveVectorProposal> candidates{{center, -1, 0}};
    for (auto index = std::size_t{}; index < settings_.variables.size(); ++index) {
        for (const auto direction : {-1, 1}) {
            if (static_cast<int>(candidates.size()) >= settings_.maximumEvaluations)
                return candidates;
            auto values = center;
            values[index] = direction < 0 ? settings_.variables[index].minimum
                                          : settings_.variables[index].maximum;
            candidates.push_back({std::move(values), static_cast<int>(index), direction});
        }
    }
    return candidates;
}

void AdaptiveVectorSearch::record(std::vector<double> values, std::optional<double> score)
{
    if (values.size() != settings_.variables.size()
        || std::ranges::any_of(values, [](double value) { return !std::isfinite(value); })) return;
    if (score && !std::isfinite(*score)) score.reset();
    observations_.push_back({std::move(values), score});
}

auto AdaptiveVectorSearch::isDuplicate(const std::vector<double>& values) const -> bool
{
    return std::ranges::any_of(observations_, [&values](const auto& observation) {
        if (observation.values.size() != values.size()) return false;
        for (auto index = std::size_t{}; index < values.size(); ++index) {
            if (std::abs(observation.values[index] - values[index])
                > std::max(1.0, std::abs(values[index])) * 1.0e-12) return false;
        }
        return true;
    });
}

auto AdaptiveVectorSearch::nextCandidates() -> std::vector<AdaptiveVectorProposal>
{
    if (static_cast<int>(observations_.size()) >= settings_.maximumEvaluations) {
        stopReason_ = AdaptiveStopReason::MaximumEvaluations;
        return {};
    }
    std::vector<const Observation*> successful;
    for (const auto& observation : observations_)
        if (observation.score) successful.push_back(&observation);
    if (successful.empty()) {
        stopReason_ = AdaptiveStopReason::NoSuccessfulCandidate;
        return {};
    }
    const auto best = *std::ranges::min_element(successful,
        [](const auto* first, const auto* second) { return *first->score < *second->score; });
    const auto bestScore = *best->score;
    if (std::isfinite(previousBestScore_)) {
        const auto improvement = previousBestScore_ - bestScore;
        stagnantRounds_ = improvement <= settings_.scoreTolerance ? stagnantRounds_ + 1 : 0;
        if (stagnantRounds_ >= 2) {
            stopReason_ = AdaptiveStopReason::ScoreTolerance;
            return {};
        }
    }
    previousBestScore_ = bestScore;

    std::vector<AdaptiveVectorProposal> candidates;
    for (auto index = std::size_t{}; index < settings_.variables.size(); ++index) {
        if (steps_[index] < settings_.variables[index].tolerance) continue;
        for (const auto direction : {-1, 1}) {
            auto values = best->values;
            values[index] = std::clamp(values[index] + direction * steps_[index],
                settings_.variables[index].minimum, settings_.variables[index].maximum);
            if (std::abs(values[index] - best->values[index])
                    < settings_.variables[index].tolerance
                || isDuplicate(values)
                || std::ranges::any_of(candidates, [&values](const auto& candidate) {
                    return candidate.values == values;
                })) continue;
            candidates.push_back({std::move(values), static_cast<int>(index), direction});
            if (static_cast<int>(observations_.size() + candidates.size())
                >= settings_.maximumEvaluations) break;
        }
        if (static_cast<int>(observations_.size() + candidates.size())
            >= settings_.maximumEvaluations) break;
    }
    for (auto& step : steps_) step /= 2.0;
    if (candidates.empty()) {
        stopReason_ = AdaptiveStopReason::ParameterTolerance;
        return {};
    }
    ++refinementRound_;
    return candidates;
}

auto AdaptiveVectorSearch::stopReason() const noexcept -> AdaptiveStopReason
{
    return stopReason_;
}

auto AdaptiveVectorSearch::refinementRound() const noexcept -> int
{
    return refinementRound_;
}

}
