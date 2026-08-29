#pragma once

#include "model/ModelSetup.h"

#include <string>
#include <string_view>

namespace necwb::analysis {

[[nodiscard]] auto prepareSolverInput(std::string_view source,
    const model::ModelSetup& setup) -> std::string;

}
