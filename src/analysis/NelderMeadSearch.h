#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace necwb::analysis {

struct NelderMeadVariable {
    double initial{};
    double minimum{};
    double maximum{};
    double tolerance{0.01};
};

struct NelderMeadSettings {
    std::vector<NelderMeadVariable> variables;
    int maximumEvaluations{41};
    double scoreTolerance{0.001};
};

enum class NelderMeadStopReason {
    None,
    MaximumEvaluations,
    ParameterTolerance,
    ScoreTolerance,
    NoSuccessfulCandidate,
};

struct NelderMeadProposal {
    std::vector<double> values;
    std::string_view role;
    int iteration{};
};

class NelderMeadSearch {
public:
    explicit NelderMeadSearch(NelderMeadSettings settings);

    [[nodiscard]] auto initialCandidates() -> std::vector<NelderMeadProposal>;
    void record(std::vector<double> values, std::optional<double> score);
    [[nodiscard]] auto nextCandidate() -> std::optional<NelderMeadProposal>;
    [[nodiscard]] auto stopReason() const noexcept -> NelderMeadStopReason;
    [[nodiscard]] auto iteration() const noexcept -> int;
    [[nodiscard]] auto evaluationCount() const noexcept -> int;

private:
    struct Vertex {
        std::vector<double> values;
        double score{};
    };

    enum class Phase {
        Initial,
        Iterate,
        Reflection,
        Expansion,
        Contraction,
        Shrink,
        Finished,
    };

    [[nodiscard]] auto valid() const -> bool;
    [[nodiscard]] auto bounded(std::vector<double> values) const -> std::vector<double>;
    [[nodiscard]] auto proposal(std::vector<double> values, std::string_view role)
        -> std::optional<NelderMeadProposal>;
    [[nodiscard]] auto converged() -> bool;
    void sortSimplex();
    void accept(Vertex vertex);
    void beginShrink();

    NelderMeadSettings settings_;
    std::vector<Vertex> simplex_;
    std::vector<double> centroid_;
    std::optional<Vertex> reflection_;
    std::optional<Vertex> trial_;
    std::vector<Vertex> shrinkVertices_;
    Phase phase_{Phase::Initial};
    NelderMeadStopReason stopReason_{NelderMeadStopReason::None};
    int iteration_{};
    int evaluationCount_{};
    int scoreConvergedIterations_{};
    std::size_t shrinkIndex_{1};
};

}
