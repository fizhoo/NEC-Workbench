#pragma once

#include "analysis/AnalysisResult.h"
#include "ui/analysis/AnalysisRunStore.h"

#include <optional>

namespace necwb::ui {

class ResultReferenceController final {
public:
    explicit ResultReferenceController(const AnalysisRunStore& store);

    void selectAutomatic(const AnalysisRunRecord& activeRun);
    void pin(const AnalysisRunRecord& run);
    void clear();

    [[nodiscard]] auto hasReference() const -> bool;
    [[nodiscard]] auto isPinned() const -> bool;
    [[nodiscard]] auto record() const -> const AnalysisRunRecord*;
    [[nodiscard]] auto result() const -> const analysis::AnalysisResult*;
    [[nodiscard]] auto label() const -> QString;

private:
    [[nodiscard]] static auto modelIdentity(const QString& sourceFile) -> QString;
    void select(AnalysisRunRecord run, bool pinned);

    const AnalysisRunStore& store_;
    std::optional<AnalysisRunRecord> reference_;
    mutable std::optional<analysis::AnalysisResult> parsedResult_;
    mutable bool parseAttempted_{};
    bool pinned_{};
};

}
