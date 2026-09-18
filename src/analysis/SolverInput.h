#pragma once

#include "analysis/FrequencyPlan.h"
#include "model/ModelSetup.h"

#include <span>
#include <string>
#include <string_view>

namespace necwb::analysis {

enum class RadiationSweepMode {
    CenterFrequencyOnly,
    RepresentativeFrequencies,
    EveryFrequency
};

[[nodiscard]] auto normalizeSolverDeck(std::string_view source) -> std::string;

[[nodiscard]] auto prepareSolverInput(std::string_view source,
    const model::ModelSetup& setup,
    RadiationSweepMode mode = RadiationSweepMode::CenterFrequencyOnly) -> std::string;

[[nodiscard]] auto prepareSolverInput(std::string_view source,
    const model::ModelSetup& setup,
    const FrequencyPlan& radiationFrequencies) -> std::string;

[[nodiscard]] auto prepareImpedanceInput(std::string_view source) -> std::string;

[[nodiscard]] auto prepareExplicitFrequencyInput(std::string_view source,
    std::span<const double> frequenciesMHz) -> std::string;

[[nodiscard]] auto prepareFrequencySweepInput(std::string_view source,
    const model::FrequencyDefinition& sweep, bool includeRadiationPatterns) -> std::string;

}
