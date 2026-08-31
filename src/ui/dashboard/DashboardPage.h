#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/AverageGainTest.h"
#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QWidget>

class QLabel;
class QTableWidget;
class QTextDocument;
class QAction;
class QHBoxLayout;
class QToolButton;

namespace necwb::ui {

class NecEditor;
class Radiation3DView;

class DashboardPage final : public QWidget {
public:
    explicit DashboardPage(QTextDocument* document, QWidget* parent = nullptr);

    void setQuickActions(QAction* geometry, QAction* source, QAction* check,
        QAction* run, QAction* results);
    void setAverageGainAction(QAction* action);
    void setConvergenceAction(QAction* action);
    void setDocumentState(const QString& fileName, bool modified);

    void setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
        const QString& solver, bool checked, std::size_t errors, std::size_t warnings);
    void setResults(const analysis::AnalysisResult& result, const QString& context, bool stale);
    void markResultsStale();
    void setAverageGainRunning(double frequencyMHz);
    void setAverageGainResult(const analysis::AverageGainAssessment& assessment,
        double frequencyMHz);
    void setAverageGainFailure(const QString& message);
    void markAverageGainStale();
    void clearAverageGain();
    void setConvergenceState(const QString& summary);
    void markConvergenceStale();
    void clearConvergence();

private:
    void setValue(QTableWidget* table, int row, const QString& value);

    NecEditor* editor_{};
    Radiation3DView* view3D_{};
    QTableWidget* modelSummary_{};
    QTableWidget* quickResults_{};
    QLabel* modelState_{};
    QLabel* resultState_{};
    QLabel* fileState_{};
    QLabel* averageGainState_{};
    QToolButton* averageGainButton_{};
    QLabel* convergenceState_{};
    QToolButton* convergenceButton_{};
    QHBoxLayout* quickActions_{};
    QString resultContext_;
    bool hasResults_{};
    bool resultsStale_{};
    bool hasAverageGainResult_{};
    bool hasConvergenceResult_{};
};

}
