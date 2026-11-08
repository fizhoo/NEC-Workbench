#pragma once

#include "ui/analysis/AnalysisRunStore.h"

#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QTableWidget;
class QToolButton;

namespace necwb::ui {

auto formatByteSize(qint64 bytes) -> QString;
// One-line identity of a run: model, start time, purpose, and backend.
auto runContext(const AnalysisRunRecord& record) -> QString;

// Saved analysis and study runs. Rows are read-only; every action on a run is requested from
// the owner, which keeps the active model untouched unless the user opens a snapshot.
class RunHistoryView final : public QWidget {
    Q_OBJECT

public:
    struct Selection {
        // Identity fields only (id, directory, type, start, backend, status, model name).
        AnalysisRunRecord record;
        QString modelName;
        QString context;
    };

    explicit RunHistoryView(QWidget* parent = nullptr);

    // Runs are read from the store the first time the view is shown.
    void setRecordSource(std::function<std::vector<AnalysisRunRecord>()> source);
    void reload();
    void addActiveRun(const AnalysisRunRecord& record);
    void updateRun(const AnalysisRunRecord& record);
    void updateRunSummary(const QString& directory, const AnalysisRunRecord& summary);
    void removeRun(const QString& directory);
    void setBusy(bool solverBusy, const QString& activeDirectory);
    [[nodiscard]] auto selection() const -> std::optional<Selection>;

signals:
    void reviewRequested();
    void inspectInputRequested();
    void openSnapshotRequested();
    void overlayRequested();
    void editNoteRequested();
    void deleteRequested();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void load();
    void setRow(int row, const AnalysisRunRecord& record);
    [[nodiscard]] auto rowForDirectory(const QString& directory) const -> int;
    void fitColumns();
    void updateActions();

    std::function<std::vector<AnalysisRunRecord>()> source_;
    QTableWidget* table_{};
    QToolButton* review_{};
    QToolButton* inspectInput_{};
    QToolButton* openSnapshot_{};
    QToolButton* overlay_{};
    QToolButton* editNote_{};
    QToolButton* openFolder_{};
    QToolButton* delete_{};
    QString activeDirectory_;
    bool solverBusy_{};
    bool loaded_{};
};

}
