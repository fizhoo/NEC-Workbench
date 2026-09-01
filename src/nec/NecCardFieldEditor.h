#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace necwb::nec {

struct NecFieldReplacement {
    std::size_t fieldIndex{};
    std::string value;
};

[[nodiscard]] auto replaceNecCardFields(std::string_view source,
    std::span<const NecFieldReplacement> replacements) -> std::optional<std::string>;
[[nodiscard]] auto necCardFieldIsNumeric(std::string_view source,
    std::size_t fieldIndex) -> bool;

}
