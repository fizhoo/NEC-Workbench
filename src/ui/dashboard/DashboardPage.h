#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/AverageGainTest.h"
#include "model/AntennaModel.h"
#include "model/ModelSetup.h"
#include "ui/analysis/AnalysisRunStore.h"

#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QTableWidget;
class QAction;
class QHBoxLayout;
class QToolButton;

namespace necwb::ui {

class DashboardPage final : public QWidget {
public:
    enum class Workspace {
        Model,
        Setup,
        Antenna,
        Impedance,
        Currents,
        Radiation,
        Runs,
        Optimize
    };

    explicit DashboardPage(QWidget* parent = nullptr);

    void setQuickActions(QAction* check, QAction* run);
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
    void setRecentRuns(const std::vector<AnalysisRunRecord>& runs);
    void setOpenRecentRunCallback(std::function<void(const QString&)> callback);
    void setOpenRunHistoryCallback(std::function<void()> callback);
    void setOpenWorkspaceCallback(std::function<void(Workspace)> callback);

private:
    void setValue(QTableWidget* table, int row, const QString& value);

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
    QTableWidget* recentRuns_{};
    std::function<void(const QString&)> openRecentRun_;
    std::function<void()> openRunHistory_;
    std::function<void(Workspace)> openWorkspace_;
    QString resultContext_;
    bool hasResults_{};
    bool resultsStale_{};
    bool hasAverageGainResult_{};
    bool hasConvergenceResult_{};
};

}
