#pragma once

#include "model/ModelSetup.h"
#include "nec/NecDocument.h"

namespace necwb::nec {

class NecSetupConverter {
public:
    [[nodiscard]] auto convert(const NecDocument& document) const -> model::ModelSetup;
};

}
