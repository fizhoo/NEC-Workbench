#pragma once

#include <cstddef>
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
    bool adjustable{};
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

}
