#pragma once

#include "analysis/AnalysisResult.h"
#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QWidget>

class QLabel;
class QTableWidget;
class QTextDocument;
class QAction;
class QHBoxLayout;

namespace necwb::ui {

class NecEditor;
class Radiation3DView;

class DashboardPage final : public QWidget {
public:
    explicit DashboardPage(QTextDocument* document, QWidget* parent = nullptr);

    void setQuickActions(QAction* geometry, QAction* source, QAction* check,
        QAction* run, QAction* results);
    void setDocumentState(const QString& fileName, bool modified);

    void setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
        const QString& solver, bool checked, std::size_t errors, std::size_t warnings);
    void setResults(const analysis::AnalysisResult& result, const QString& context, bool stale);
    void markResultsStale();

private:
    void setValue(QTableWidget* table, int row, const QString& value);

    NecEditor* editor_{};
    Radiation3DView* view3D_{};
    QTableWidget* modelSummary_{};
    QTableWidget* quickResults_{};
    QLabel* modelState_{};
    QLabel* resultState_{};
    QLabel* fileState_{};
    QHBoxLayout* quickActions_{};
    QString resultContext_;
    bool hasResults_{};
    bool resultsStale_{};
};

}
