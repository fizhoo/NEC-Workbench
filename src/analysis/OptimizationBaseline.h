#pragma once

#include "analysis/AnalysisResult.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace necwb::analysis {

struct RadiationCoverage {
    double frequencyMHz{};
    int patternIndex{};
    std::size_t sampleCount{};
    std::string directionFingerprint;

    auto operator==(const RadiationCoverage&) const -> bool = default;
};

struct OptimizationResultCoverage {
    double referenceImpedanceOhms{50.0};
    std::vector<double> impedanceFrequenciesMHz;
    std::vector<RadiationCoverage> radiation;
};

struct OptimizationBaselineDescriptor {
    std::string modelFingerprint;
    std::string backend;
    OptimizationResultCoverage coverage;
};

struct OptimizationBaselineRequirements {
    std::string modelFingerprint;
    std::string backend;
    double referenceImpedanceOhms{50.0};
    std::vector<double> impedanceFrequenciesMHz;
    std::vector<double> radiationFrequenciesMHz;
    std::vector<RadiationCoverage> radiation;
};

enum class OptimizationBaselineStatus {
    Compatible,
    Missing,
    IncompleteMetadata,
    ModelChanged,
    BackendChanged,
    ReferenceImpedanceChanged,
    MissingImpedanceCoverage,
    MissingRadiationCoverage
};

struct OptimizationBaselineCompatibility {
    OptimizationBaselineStatus status{OptimizationBaselineStatus::Missing};
    std::vector<double> missingImpedanceFrequenciesMHz;
    std::vector<double> missingRadiationFrequenciesMHz;
    std::vector<RadiationCoverage> missingRadiation;

    [[nodiscard]] auto compatible() const noexcept -> bool;
};

[[nodiscard]] auto optimizationModelFingerprint(std::string_view resolvedDeck) -> std::string;
[[nodiscard]] auto optimizationResultCoverage(const AnalysisResult& result)
    -> OptimizationResultCoverage;
[[nodiscard]] auto optimizationRadiationRequirements(std::string_view solverDeck)
    -> std::vector<RadiationCoverage>;
[[nodiscard]] auto evaluateOptimizationBaseline(
    const std::optional<OptimizationBaselineDescriptor>& baseline,
    const OptimizationBaselineRequirements& requirements)
    -> OptimizationBaselineCompatibility;

}
