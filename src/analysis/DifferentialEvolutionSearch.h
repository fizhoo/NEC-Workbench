#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

namespace necwb::analysis {

struct DifferentialEvolutionVariable {
    double initial{};
    double minimum{};
    double maximum{};
    double tolerance{0.01};
};

struct DifferentialEvolutionSettings {
    std::vector<DifferentialEvolutionVariable> variables;
    int populationSize{12};
    int maximumGenerations{20};
    double mutationFactor{0.8};
    double crossoverRate{0.9};
    double scoreTolerance{0.001};
    int stagnantGenerations{3};
    std::uint32_t seed{5489u};
};

enum class DifferentialEvolutionStopReason {
    None,
    MaximumGenerations,
    ParameterTolerance,
    ScoreTolerance,
    NoSuccessfulCandidate,
};

struct DifferentialEvolutionProposal {
    std::vector<double> values;
    std::string_view role;
    int generation{};
    int targetIndex{-1};
};

class DifferentialEvolutionSearch {
public:
    explicit DifferentialEvolutionSearch(DifferentialEvolutionSettings settings);

    [[nodiscard]] auto initialCandidates() -> std::vector<DifferentialEvolutionProposal>;
    void record(std::vector<double> values, std::optional<double> score);
    [[nodiscard]] auto nextCandidate() -> std::optional<DifferentialEvolutionProposal>;
    [[nodiscard]] auto stopReason() const noexcept -> DifferentialEvolutionStopReason;
    [[nodiscard]] auto generation() const noexcept -> int;
    [[nodiscard]] auto evaluationCount() const noexcept -> int;

private:
    struct Member {
        std::vector<double> values;
        double score{};
    };

    struct PendingTrial {
        std::vector<double> values;
        int targetIndex{};
    };

    enum class Phase {
        Initial,
        Evolve,
        Finished,
    };

    [[nodiscard]] auto valid() const -> bool;
    [[nodiscard]] auto bounded(std::vector<double> values) const -> std::vector<double>;
    [[nodiscard]] auto bestScore() const -> double;
    [[nodiscard]] auto populationConverged() const -> bool;
    [[nodiscard]] auto completeGeneration() -> bool;

    DifferentialEvolutionSettings settings_;
    std::mt19937 random_;
    std::vector<Member> population_;
    std::optional<PendingTrial> pendingTrial_;
    Phase phase_{Phase::Initial};
    DifferentialEvolutionStopReason stopReason_{DifferentialEvolutionStopReason::None};
    int generation_{};
    int targetIndex_{};
    int evaluationCount_{};
    int stagnantGenerations_{};
    double previousBestScore_{};
};

}
