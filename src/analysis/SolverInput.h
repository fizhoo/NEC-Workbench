#pragma once

#include "model/ModelSetup.h"

#include <string>
#include <string_view>

namespace necwb::analysis {

enum class RadiationSweepMode {
    CenterFrequencyOnly,
    RepresentativeFrequencies,
    EveryFrequency
};

[[nodiscard]] auto prepareSolverInput(std::string_view source,
    const model::ModelSetup& setup,
    RadiationSweepMode mode = RadiationSweepMode::CenterFrequencyOnly) -> std::string;

}
