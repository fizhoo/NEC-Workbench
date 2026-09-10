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

private:
    struct Observation {
        double value{};
        std::optional<double> score;
    };

    AdaptiveSearchSettings settings_;
    std::vector<Observation> observations_;
    double previousBestScore_{};
    int stagnantRounds_{};
    AdaptiveStopReason stopReason_{AdaptiveStopReason::None};
};

}
