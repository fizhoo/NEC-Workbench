#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace necwb::analysis {

struct AdaptiveSearchSettings {
    double minimum{};
    double maximum{};
    int maximumEvaluations{21};
    double parameterTolerance{0.01};
    double scoreTolerance{0.001};
};

struct AdaptiveVectorVariable {
    double minimum{};
    double maximum{};
    double tolerance{0.01};
};

struct AdaptiveVectorSearchSettings {
    std::vector<AdaptiveVectorVariable> variables;
    int maximumEvaluations{21};
    double scoreTolerance{0.001};
};

struct AdaptiveVectorProposal {
    std::vector<double> values;
    int variableIndex{-1};
    int direction{};
};

enum class AdaptiveStopReason {
    None,
    MaximumEvaluations,
    ParameterTolerance,
    ScoreTolerance,
    NoSuccessfulCandidate,
};

class AdaptiveSearch {
public:
    explicit AdaptiveSearch(AdaptiveSearchSettings settings);

    [[nodiscard]] auto initialCandidates() const -> std::vector<double>;
    void record(double value, std::optional<double> score);
    [[nodiscard]] auto nextCandidates() -> std::vector<double>;
    [[nodiscard]] auto stopReason() const noexcept -> AdaptiveStopReason;
    [[nodiscard]] auto refinementRound() const noexcept -> int;

private:
    struct Observation {
        double value{};
        std::optional<double> score;
    };

    AdaptiveSearchSettings settings_;
    std::vector<Observation> observations_;
    double previousBestScore_{};
    int stagnantRounds_{};
    int refinementRound_{};
    AdaptiveStopReason stopReason_{AdaptiveStopReason::None};
};

class AdaptiveVectorSearch {
public:
    explicit AdaptiveVectorSearch(AdaptiveVectorSearchSettings settings);

    [[nodiscard]] auto initialCandidates() const -> std::vector<AdaptiveVectorProposal>;
    void record(std::vector<double> values, std::optional<double> score);
    [[nodiscard]] auto nextCandidates() -> std::vector<AdaptiveVectorProposal>;
    [[nodiscard]] auto stopReason() const noexcept -> AdaptiveStopReason;
    [[nodiscard]] auto refinementRound() const noexcept -> int;

private:
    struct Observation {
        std::vector<double> values;
        std::optional<double> score;
    };

    [[nodiscard]] auto isDuplicate(const std::vector<double>& values) const -> bool;

    AdaptiveVectorSearchSettings settings_;
    std::vector<Observation> observations_;
    std::vector<double> steps_;
    double previousBestScore_{};
    int stagnantRounds_{};
    int refinementRound_{};
    AdaptiveStopReason stopReason_{AdaptiveStopReason::None};
};

}
