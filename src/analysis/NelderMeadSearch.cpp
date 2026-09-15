#include "analysis/NelderMeadSearch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace necwb::analysis {
namespace {

constexpr auto FailedScore = std::numeric_limits<double>::infinity();

auto finiteScore(std::optional<double> score) -> double
{
    return score && std::isfinite(*score) ? *score : FailedScore;
}

}

NelderMeadSearch::NelderMeadSearch(NelderMeadSettings settings)
    : settings_(std::move(settings))
{
}

auto NelderMeadSearch::valid() const -> bool
{
    return !settings_.variables.empty()
        && settings_.maximumEvaluations >= static_cast<int>(settings_.variables.size()) + 1
        && std::isfinite(settings_.scoreTolerance) && settings_.scoreTolerance >= 0.0
        && std::ranges::all_of(settings_.variables, [](const auto& variable) {
            return std::isfinite(variable.initial) && std::isfinite(variable.minimum)
                && std::isfinite(variable.maximum) && std::isfinite(variable.tolerance)
                && variable.minimum < variable.maximum && variable.tolerance > 0.0;
        });
}

auto NelderMeadSearch::bounded(std::vector<double> values) const -> std::vector<double>
{
    for (auto index = std::size_t{}; index < values.size(); ++index) {
        values[index] = std::clamp(values[index], settings_.variables[index].minimum,
            settings_.variables[index].maximum);
    }
    return values;
}

auto NelderMeadSearch::initialCandidates() -> std::vector<NelderMeadProposal>
{
    if (!valid() || phase_ != Phase::Initial || evaluationCount_ != 0) return {};
    std::vector<double> center;
    center.reserve(settings_.variables.size());
    for (const auto& variable : settings_.variables)
        center.push_back(std::clamp(variable.initial, variable.minimum, variable.maximum));

    std::vector<NelderMeadProposal> candidates;
    candidates.push_back({center, "initial", 0});
    for (auto index = std::size_t{}; index < settings_.variables.size(); ++index) {
        auto values = center;
        const auto& variable = settings_.variables[index];
        const auto step = (variable.maximum - variable.minimum) * 0.2;
        values[index] = center[index] + step <= variable.maximum
            ? center[index] + step : center[index] - step;
        candidates.push_back({bounded(std::move(values)), "initial-simplex", 0});
    }
    return candidates;
}

void NelderMeadSearch::record(std::vector<double> values, std::optional<double> score)
{
    if (phase_ == Phase::Finished || values.size() != settings_.variables.size()) return;
    ++evaluationCount_;
    Vertex vertex{bounded(std::move(values)), finiteScore(score)};
    switch (phase_) {
    case Phase::Initial:
        simplex_.push_back(std::move(vertex));
        if (simplex_.size() == settings_.variables.size() + 1) phase_ = Phase::Iterate;
        break;
    case Phase::Reflection:
        reflection_ = std::move(vertex);
        break;
    case Phase::Expansion:
    case Phase::Contraction:
        trial_ = std::move(vertex);
        break;
    case Phase::Shrink:
        shrinkVertices_.push_back(std::move(vertex));
        break;
    case Phase::Iterate:
    case Phase::Finished:
        break;
    }
}

void NelderMeadSearch::sortSimplex()
{
    std::ranges::sort(simplex_, {}, &Vertex::score);
}

void NelderMeadSearch::accept(Vertex vertex)
{
    simplex_.back() = std::move(vertex);
    ++iteration_;
    phase_ = Phase::Iterate;
    reflection_.reset();
    trial_.reset();
}

auto NelderMeadSearch::converged() -> bool
{
    sortSimplex();
    if (!std::isfinite(simplex_.front().score)) {
        stopReason_ = NelderMeadStopReason::NoSuccessfulCandidate;
        phase_ = Phase::Finished;
        return true;
    }

    auto parametersConverged = true;
    for (auto index = std::size_t{}; index < settings_.variables.size(); ++index) {
        for (const auto& vertex : simplex_) {
            if (std::abs(vertex.values[index] - simplex_.front().values[index])
                > settings_.variables[index].tolerance) {
                parametersConverged = false;
                break;
            }
        }
    }
    if (parametersConverged) {
        stopReason_ = NelderMeadStopReason::ParameterTolerance;
        phase_ = Phase::Finished;
        return true;
    }

    const auto scoreConverged = std::isfinite(simplex_.back().score)
        && simplex_.back().score - simplex_.front().score <= settings_.scoreTolerance;
    scoreConvergedIterations_ = scoreConverged ? scoreConvergedIterations_ + 1 : 0;
    auto simplexContracted = true;
    for (auto index = std::size_t{}; index < settings_.variables.size(); ++index) {
        const auto allowedSpread = std::max(settings_.variables[index].tolerance,
            (settings_.variables[index].maximum - settings_.variables[index].minimum) * 0.01);
        const auto [minimum, maximum] = std::ranges::minmax_element(simplex_, {},
            [index](const auto& vertex) { return vertex.values[index]; });
        if (maximum->values[index] - minimum->values[index] > allowedSpread) {
            simplexContracted = false;
            break;
        }
    }
    if (scoreConvergedIterations_ >= 2 && simplexContracted) {
        stopReason_ = NelderMeadStopReason::ScoreTolerance;
        phase_ = Phase::Finished;
        return true;
    }
    return false;
}

auto NelderMeadSearch::proposal(std::vector<double> values, std::string_view role)
    -> std::optional<NelderMeadProposal>
{
    if (evaluationCount_ >= settings_.maximumEvaluations) {
        stopReason_ = NelderMeadStopReason::MaximumEvaluations;
        phase_ = Phase::Finished;
        return std::nullopt;
    }
    return NelderMeadProposal{bounded(std::move(values)), role, iteration_ + 1};
}

void NelderMeadSearch::beginShrink()
{
    sortSimplex();
    shrinkVertices_.clear();
    shrinkVertices_.push_back(simplex_.front());
    shrinkIndex_ = 1;
    phase_ = Phase::Shrink;
    reflection_.reset();
    trial_.reset();
}

auto NelderMeadSearch::nextCandidate() -> std::optional<NelderMeadProposal>
{
    constexpr auto reflectionFactor = 1.0;
    constexpr auto expansionFactor = 2.0;
    constexpr auto contractionFactor = 0.5;
    constexpr auto shrinkFactor = 0.5;

    while (phase_ != Phase::Finished) {
        if (evaluationCount_ >= settings_.maximumEvaluations) {
            stopReason_ = NelderMeadStopReason::MaximumEvaluations;
            phase_ = Phase::Finished;
            return std::nullopt;
        }
        if (phase_ == Phase::Initial) return std::nullopt;
        if (phase_ == Phase::Iterate) {
            if (simplex_.size() != settings_.variables.size() + 1 || converged())
                return std::nullopt;
            centroid_.assign(settings_.variables.size(), 0.0);
            for (auto vertex = std::size_t{}; vertex + 1 < simplex_.size(); ++vertex) {
                for (auto index = std::size_t{}; index < centroid_.size(); ++index)
                    centroid_[index] += simplex_[vertex].values[index];
            }
            for (auto& value : centroid_) value /= static_cast<double>(settings_.variables.size());
            auto reflected = centroid_;
            for (auto index = std::size_t{}; index < reflected.size(); ++index) {
                reflected[index] += reflectionFactor
                    * (centroid_[index] - simplex_.back().values[index]);
            }
            phase_ = Phase::Reflection;
            return proposal(std::move(reflected), "reflection");
        }
        if (phase_ == Phase::Reflection && reflection_) {
            sortSimplex();
            if (reflection_->score < simplex_.front().score) {
                auto expanded = centroid_;
                for (auto index = std::size_t{}; index < expanded.size(); ++index) {
                    expanded[index] += expansionFactor
                        * (reflection_->values[index] - centroid_[index]);
                }
                phase_ = Phase::Expansion;
                return proposal(std::move(expanded), "expansion");
            }
            if (reflection_->score < simplex_[simplex_.size() - 2].score) {
                accept(std::move(*reflection_));
                continue;
            }
            const auto outside = reflection_->score < simplex_.back().score;
            auto contracted = centroid_;
            const auto& endpoint = outside ? reflection_->values : simplex_.back().values;
            for (auto index = std::size_t{}; index < contracted.size(); ++index) {
                contracted[index] += contractionFactor
                    * (endpoint[index] - centroid_[index]);
            }
            phase_ = Phase::Contraction;
            return proposal(std::move(contracted), outside
                ? "outside-contraction" : "inside-contraction");
        }
        if (phase_ == Phase::Expansion && trial_ && reflection_) {
            accept(trial_->score < reflection_->score ? std::move(*trial_)
                                                      : std::move(*reflection_));
            continue;
        }
        if (phase_ == Phase::Contraction && trial_ && reflection_) {
            sortSimplex();
            const auto outside = reflection_->score < simplex_.back().score;
            const auto accepted = outside ? trial_->score <= reflection_->score
                                          : trial_->score < simplex_.back().score;
            if (accepted) {
                accept(std::move(*trial_));
                continue;
            }
            beginShrink();
        }
        if (phase_ == Phase::Shrink) {
            if (shrinkIndex_ >= simplex_.size()) {
                simplex_ = std::move(shrinkVertices_);
                ++iteration_;
                phase_ = Phase::Iterate;
                continue;
            }
            auto values = simplex_.front().values;
            for (auto index = std::size_t{}; index < values.size(); ++index) {
                values[index] += shrinkFactor
                    * (simplex_[shrinkIndex_].values[index] - values[index]);
            }
            ++shrinkIndex_;
            return proposal(std::move(values), "shrink");
        }
        return std::nullopt;
    }
    return std::nullopt;
}

auto NelderMeadSearch::stopReason() const noexcept -> NelderMeadStopReason
{
    return stopReason_;
}

auto NelderMeadSearch::iteration() const noexcept -> int
{
    return iteration_;
}

auto NelderMeadSearch::evaluationCount() const noexcept -> int
{
    return evaluationCount_;
}

}
