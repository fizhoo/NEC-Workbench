#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace necwb::nec {

[[nodiscard]] auto insertSymbolDefinition(std::string_view source,
    std::string_view name, std::string_view expression) -> std::string;

[[nodiscard]] auto replaceSymbolDefinition(std::string_view source,
    std::size_t lineNumber, std::string_view originalName,
    std::string_view name, std::string_view expression) -> std::optional<std::string>;

[[nodiscard]] auto removeSymbolDefinition(std::string_view source,
    std::size_t lineNumber, std::string_view name) -> std::optional<std::string>;

[[nodiscard]] auto parameterizeNecCardField(std::string_view source,
    std::size_t lineNumber, std::size_t fieldIndex,
    std::string_view name) -> std::optional<std::string>;

[[nodiscard]] auto replaceNecCardFieldExpression(std::string_view source,
    std::size_t lineNumber, std::size_t fieldIndex,
    std::string_view expression) -> std::optional<std::string>;

[[nodiscard]] auto symbolReferencePreservingValue(std::string_view name,
    double symbolValue, double fieldValue) -> std::string;

}
