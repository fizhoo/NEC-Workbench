#include "analysis/AdaptiveSearch.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

}
