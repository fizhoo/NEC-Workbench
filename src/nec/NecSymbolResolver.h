#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace necwb::nec {

struct SymbolDefinition {
    std::string name;
    std::string expression;
    double value{};
    std::size_t lineNumber{};
};

struct SymbolDiagnostic {
    std::size_t lineNumber{};
    std::string message;
};

struct SymbolResolution {
    std::string resolvedSource;
    std::string generatedDeck;
    std::vector<SymbolDefinition> definitions;
    std::vector<SymbolDiagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool;
};

class NecSymbolResolver {
public:
    [[nodiscard]] auto resolve(std::string_view source,
        const std::unordered_map<std::string, double>& overrides = {}) const -> SymbolResolution;
};

// The names an expression uses that are not defined, each once, in the order they appear.
[[nodiscard]] auto undefinedSymbols(std::string_view expression,
    const std::vector<SymbolDefinition>& definitions) -> std::vector<std::string>;
// The value of a new symbol that keeps the expression at `target`, when the expression is
// linear in it: len/2 at 5.1 gives 10.2.
[[nodiscard]] auto symbolValueKeeping(std::string_view expression, std::string_view name,
    const std::vector<SymbolDefinition>& definitions, double target) -> std::optional<double>;

}
