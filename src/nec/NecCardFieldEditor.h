#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace necwb::nec {

struct NecFieldReplacement {
    std::size_t fieldIndex{};
    std::string value;
};

using NecParameterFieldMap = std::unordered_map<std::size_t,
    std::unordered_map<std::size_t, std::string>>;

[[nodiscard]] auto replaceNecCardFields(std::string_view source,
    std::span<const NecFieldReplacement> replacements) -> std::optional<std::string>;
[[nodiscard]] auto necCardFieldIsNumeric(std::string_view source,
    std::size_t fieldIndex) -> bool;
// The field as written, such as "-drv_half"; empty when the card has no such field.
[[nodiscard]] auto necCardFieldText(std::string_view source, std::size_t fieldIndex)
    -> std::string;
[[nodiscard]] auto necCardFieldsReferencingSymbols(std::string_view source,
    std::span<const std::string> symbolNames) -> NecParameterFieldMap;

}
