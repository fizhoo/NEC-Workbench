#include "analysis/DifferentialEvolutionSearch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace necwb::analysis {
namespace {

constexpr auto FailedScore = std::numeric_limits<double>::infinity();

auto finiteScore(std::optional<double> score) -> double
{
    return score && std::isfinite(*score) ? *score : FailedScore;
}

}

DifferentialEvolutionSearch::DifferentialEvolutionSearch(
    DifferentialEvolutionSettings settings)
    : settings_(std::move(settings))
    , random_(settings_.seed)
    , previousBestScore_(FailedScore)
{
}

auto DifferentialEvolutionSearch::valid() const -> bool
{
    return !settings_.variables.empty() && settings_.populationSize >= 4
        && settings_.maximumGenerations >= 1
        && std::isfinite(settings_.mutationFactor) && settings_.mutationFactor > 0.0
        && settings_.mutationFactor <= 2.0
        && std::isfinite(settings_.crossoverRate) && settings_.crossoverRate >= 0.0
        && settings_.crossoverRate <= 1.0
        && std::isfinite(settings_.scoreTolerance) && settings_.scoreTolerance >= 0.0
        && settings_.stagnantGenerations >= 1
        && std::ranges::all_of(settings_.variables, [](const auto& variable) {
            return std::isfinite(variable.initial) && std::isfinite(variable.minimum)
                && std::isfinite(variable.maximum) && std::isfinite(variable.tolerance)
                && variable.minimum < variable.maximum && variable.tolerance > 0.0;
        });
}

auto DifferentialEvolutionSearch::bounded(std::vector<double> values) const
    -> std::vector<double>
{
    for (auto index = std::size_t{}; index < values.size(); ++index) {
        values[index] = std::clamp(values[index], settings_.variables[index].minimum,
            settings_.variables[index].maximum);
    }
    return values;
}

auto DifferentialEvolutionSearch::initialCandidates()
    -> std::vector<DifferentialEvolutionProposal>
{
    if (!valid() || phase_ != Phase::Initial || evaluationCount_ != 0) return {};
    std::vector<DifferentialEvolutionProposal> candidates;
    candidates.reserve(static_cast<std::size_t>(settings_.populationSize));
    std::vector<double> initial;
    initial.reserve(settings_.variables.size());
    for (const auto& variable : settings_.variables)
        initial.push_back(std::clamp(variable.initial, variable.minimum, variable.maximum));
    candidates.push_back({std::move(initial), "initial", 0, 0});
    for (auto member = 1; member < settings_.populationSize; ++member) {
        std::vector<double> values;
        values.reserve(settings_.variables.size());
        for (const auto& variable : settings_.variables) {
            std::uniform_real_distribution<double> distribution(
                variable.minimum, variable.maximum);
            values.push_back(distribution(random_));
        }
        candidates.push_back({std::move(values), "initial-population", 0, member});
    }
    return candidates;
}

void DifferentialEvolutionSearch::record(
    std::vector<double> values, std::optional<double> score)
{
    if (phase_ == Phase::Finished || values.size() != settings_.variables.size()) return;
    ++evaluationCount_;
    const auto candidateScore = finiteScore(score);
    if (phase_ == Phase::Initial) {
        population_.push_back({bounded(std::move(values)), candidateScore});
        if (static_cast<int>(population_.size()) == settings_.populationSize) {
            if (!std::isfinite(bestScore())) {
                stopReason_ = DifferentialEvolutionStopReason::NoSuccessfulCandidate;
                phase_ = Phase::Finished;
            } else {
                previousBestScore_ = bestScore();
                phase_ = Phase::Evolve;
            }
        }
        return;
    }
    if (!pendingTrial_) return;
    const auto target = pendingTrial_->targetIndex;
    if (candidateScore <= population_[static_cast<std::size_t>(target)].score) {
        population_[static_cast<std::size_t>(target)] = {
            bounded(std::move(values)), candidateScore};
    }
    pendingTrial_.reset();
    ++targetIndex_;
}

auto DifferentialEvolutionSearch::bestScore() const -> double
{
    if (population_.empty()) return FailedScore;
    return std::ranges::min(population_, {}, &Member::score).score;
}

auto DifferentialEvolutionSearch::populationConverged() const -> bool
{
    for (auto variable = std::size_t{}; variable < settings_.variables.size(); ++variable) {
        const auto [minimum, maximum] = std::ranges::minmax_element(population_, {},
            [variable](const auto& member) { return member.values[variable]; });
        if (maximum->values[variable] - minimum->values[variable]
            > settings_.variables[variable].tolerance) return false;
    }
    return true;
}

auto DifferentialEvolutionSearch::completeGeneration() -> bool
{
    ++generation_;
    if (populationConverged()) {
        stopReason_ = DifferentialEvolutionStopReason::ParameterTolerance;
        phase_ = Phase::Finished;
        return true;
    }
    const auto currentBest = bestScore();
    const auto improvement = previousBestScore_ - currentBest;
    stagnantGenerations_ = improvement <= settings_.scoreTolerance
        ? stagnantGenerations_ + 1 : 0;
    previousBestScore_ = currentBest;
    if (stagnantGenerations_ >= settings_.stagnantGenerations) {
        stopReason_ = DifferentialEvolutionStopReason::ScoreTolerance;
        phase_ = Phase::Finished;
        return true;
    }
    if (generation_ >= settings_.maximumGenerations) {
        stopReason_ = DifferentialEvolutionStopReason::MaximumGenerations;
        phase_ = Phase::Finished;
        return true;
    }
    targetIndex_ = 0;
    return false;
}

auto DifferentialEvolutionSearch::nextCandidate()
    -> std::optional<DifferentialEvolutionProposal>
{
    if (phase_ != Phase::Evolve || pendingTrial_) return std::nullopt;
    if (targetIndex_ >= settings_.populationSize && completeGeneration())
        return std::nullopt;

    std::vector<int> donors(static_cast<std::size_t>(settings_.populationSize));
    std::iota(donors.begin(), donors.end(), 0);
    donors.erase(donors.begin() + targetIndex_);
    std::ranges::shuffle(donors, random_);
    const auto first = static_cast<std::size_t>(donors[0]);
    const auto second = static_cast<std::size_t>(donors[1]);
    const auto third = static_cast<std::size_t>(donors[2]);
    const auto target = static_cast<std::size_t>(targetIndex_);
    auto trial = population_[target].values;
    std::uniform_int_distribution<std::size_t> forcedVariable(
        0, settings_.variables.size() - 1);
    std::uniform_real_distribution<double> crossover(0.0, 1.0);
    const auto forced = forcedVariable(random_);
    for (auto variable = std::size_t{}; variable < trial.size(); ++variable) {
        if (variable != forced && crossover(random_) > settings_.crossoverRate) continue;
        trial[variable] = population_[first].values[variable]
            + settings_.mutationFactor
                * (population_[second].values[variable]
                    - population_[third].values[variable]);
    }
    trial = bounded(std::move(trial));
    pendingTrial_ = PendingTrial{trial, targetIndex_};
    return DifferentialEvolutionProposal{
        std::move(trial), "trial", generation_ + 1, targetIndex_};
}

auto DifferentialEvolutionSearch::stopReason() const noexcept
    -> DifferentialEvolutionStopReason
{
    return stopReason_;
}

auto DifferentialEvolutionSearch::generation() const noexcept -> int
{
    return generation_;
}

auto DifferentialEvolutionSearch::evaluationCount() const noexcept -> int
{
    return evaluationCount_;
}

}
