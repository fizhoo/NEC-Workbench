#pragma once

#include "model/AntennaModel.h"
#include "nec/NecDocument.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace necwb::nec {

enum class DiagnosticSeverity {
    Information,
    Warning,
    Error
};

enum class CompatibilityProfile {
    OriginalNec2,
    FourNec2,
    OpenNec
};

struct ModelCheckOptions {
    CompatibilityProfile compatibility{CompatibilityProfile::OriginalNec2};
    std::size_t expandedSymbolCount{};
};

[[nodiscard]] auto compatibilityProfileForBackend(std::string_view backendId) noexcept
    -> CompatibilityProfile;
[[nodiscard]] auto compatibilityProfileName(CompatibilityProfile profile) noexcept
    -> std::string_view;

struct ModelDiagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::size_t lineNumber{};
    std::string message;
    std::string category{"Model"};
};

struct ModelCheckResult {
    model::AntennaModel model;
    std::vector<ModelDiagnostic> diagnostics;

    [[nodiscard]] auto errorCount() const noexcept -> std::size_t;
    [[nodiscard]] auto warningCount() const noexcept -> std::size_t;
    [[nodiscard]] auto informationCount() const noexcept -> std::size_t;
};

class NecModelChecker {
public:
    [[nodiscard]] auto check(const NecDocument& document,
        ModelCheckOptions options = {}) const -> ModelCheckResult;
};

}
