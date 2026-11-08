#pragma once

#include "nec/NecModelChecker.h"

#include <QWidget>

#include <cstddef>
#include <span>

class QLabel;
class QTreeWidget;

namespace necwb::ui {

// Model check findings: one compact row per diagnostic under a one-line verdict.
class ProblemsView final : public QWidget {
    Q_OBJECT

public:
    explicit ProblemsView(QWidget* parent = nullptr);

    void setFindings(std::span<const nec::ModelDiagnostic> diagnostics,
        const model::AntennaModel& model, const QString& summary);
    void clearFindings(const QString& summary);

signals:
    void findingActivated(std::size_t sourceLine);
    void recheckRequested();
    void generatedDeckRequested();
    void changeSolverRequested();

private:
    QLabel* summary_{};
    QTreeWidget* findings_{};
};

}
