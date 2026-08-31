#pragma once

#include "model/AntennaModel.h"
#include "nec/NecDocument.h"

#include <cstddef>
#include <string>
#include <vector>

namespace necwb::nec {

enum class DiagnosticSeverity {
    Warning,
    Error
};

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
};

class NecModelChecker {
public:
    [[nodiscard]] auto check(const NecDocument& document) const -> ModelCheckResult;
};

}
