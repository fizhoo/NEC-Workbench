#pragma once

#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/ResultData.h"

#include <optional>

namespace necwb::ui {

class ResultReferenceController final {
public:
    explicit ResultReferenceController(const AnalysisRunStore& store);

    void selectAutomatic(const AnalysisRunRecord& activeRun);
    void pin(const AnalysisRunRecord& run);
    void refreshMetadata(const AnalysisRunRecord& run);
    void clear();

    [[nodiscard]] auto hasReference() const -> bool;
    [[nodiscard]] auto isPinned() const -> bool;
    [[nodiscard]] auto record() const -> const AnalysisRunRecord*;
    // Parsed once and shared by every view that draws the overlay; null when unreadable.
    [[nodiscard]] auto data() const -> ResultData::Pointer;
    [[nodiscard]] auto label() const -> QString;

private:
    [[nodiscard]] static auto modelIdentity(const QString& sourceFile) -> QString;
    void select(AnalysisRunRecord run, bool pinned);

    const AnalysisRunStore& store_;
    std::optional<AnalysisRunRecord> reference_;
    mutable ResultData::Pointer parsedData_;
    mutable bool parseAttempted_{};
    bool pinned_{};
};

}
