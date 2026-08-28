#pragma once

#include "nec/NecDocument.h"

#include <string_view>

namespace necwb::nec {

class NecParser {
public:
    [[nodiscard]] auto parse(std::string_view source) const -> NecDocument;
};

}
