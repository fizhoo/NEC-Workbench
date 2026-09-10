#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace necwb::nec {

[[nodiscard]] auto sourceLineCount(std::string_view source) -> std::size_t;
[[nodiscard]] auto replaceSourceLine(std::string_view source, std::size_t lineNumber,
    std::string_view replacement) -> std::optional<std::string>;
[[nodiscard]] auto removeSourceLine(std::string_view source, std::size_t lineNumber)
    -> std::optional<std::string>;
[[nodiscard]] auto insertSourceLine(std::string_view source, std::size_t zeroBasedIndex,
    std::string_view line) -> std::optional<std::string>;

}
