#pragma once

#include "model/AntennaModel.h"
#include "nec/NecDocument.h"

#include <cstddef>
#include <string>
#include <vector>

namespace necwb::nec {

struct ConversionIssue {
    std::size_t lineNumber{};
    std::string message;
};

struct ModelConversionResult {
    model::AntennaModel model;
    std::vector<ConversionIssue> issues;
};

class NecModelConverter {
public:
    [[nodiscard]] auto convert(const NecDocument& document) const -> ModelConversionResult;
};

}
