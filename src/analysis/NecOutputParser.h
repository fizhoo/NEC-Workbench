#pragma once

#include "analysis/AnalysisResult.h"

#include <string_view>

namespace necwb::analysis {

class NecOutputParser {
public:
    [[nodiscard]] auto parse(std::string_view output) const -> AnalysisResult;
};

}
