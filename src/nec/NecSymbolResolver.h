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
    // An expression in a NEC integer field (a tag, segment, count or type code) that does not
    // come to a whole number in range.
    bool wholeNumberField{};
    // That field, as "GW field 2".
    std::string field{};
};

struct SymbolResolution {
    std::string resolvedSource;
    std::string generatedDeck;
    std::vector<SymbolDefinition> definitions;
    std::vector<SymbolDiagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool;
};

// Expands SY symbols. An expression in a NEC integer field must come to a whole number,
// allowing for rounding error in its arithmetic (1e-9 of the value); a fractional or
// out-of-range value is a diagnostic, never rounded. A voltage source marked to sit along its
// wire (SourcePositions.h) gets the segment at that point in the resolved geometry.
class NecSymbolResolver {
public:
    [[nodiscard]] auto resolve(std::string_view source,
        const std::unordered_map<std::string, double>& overrides = {}) const -> SymbolResolution;
};

// The first NEC integer field (SymbolDiagnostic::wholeNumberField) left fractional or out of
// range when the symbol `name` takes `value`, or none.
[[nodiscard]] auto wholeNumberFieldAt(std::string_view source, std::string_view name,
    double value) -> std::optional<SymbolDiagnostic>;
// Whether changing the symbol by a fraction makes a NEC integer field fractional, as when it
// is a segment count or a segment number, directly or through other symbols: the field's
// diagnostic with the symbol moved by two unrelated fractions of its size, or none. A symbol
// that only reaches decimal fields, or reaches an integer field in a way that stays whole,
// gives none.
[[nodiscard]] auto wholeNumberFieldReached(std::string_view source,
    const SymbolDefinition& definition) -> std::optional<SymbolDiagnostic>;

// The names an expression uses that are not defined, each once, in the order they appear.
[[nodiscard]] auto undefinedSymbols(std::string_view expression,
    const std::vector<SymbolDefinition>& definitions) -> std::vector<std::string>;
// The value of a new symbol that keeps the expression at `target`, when the expression is
// linear in it: len/2 at 5.1 gives 10.2.
[[nodiscard]] auto symbolValueKeeping(std::string_view expression, std::string_view name,
    const std::vector<SymbolDefinition>& definitions, double target) -> std::optional<double>;

}
