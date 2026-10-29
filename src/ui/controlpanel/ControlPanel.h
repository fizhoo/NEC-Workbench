#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/AverageGainTest.h"
#include "model/AntennaModel.h"
#include "model/ModelSetup.h"
#include "ui/analysis/AnalysisRunStore.h"

#include <QWidget>

#include <functional>
#include <array>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QTableWidget;
class QToolButton;

namespace necwb::nec { class NecDocument; }

namespace necwb::ui {

class ControlPanel final : public QWidget {
public:
    enum class Study { AverageGain, Convergence };
    enum class StudyState { NotRun, Running, Complete, Failed, Stale };
    enum class Workspace {
        Setup,
        GeometryView,
        Impedance,
        Currents,
        Radiation,
        Runs,
        Optimize
    };

    enum class Editor {
        Frequency,
        Wires,
        Sources,
        Loads,
        TransmissionLines,
        Ground,
        Variables,
        Requests,
        Deck
    };

    explicit ControlPanel(QWidget* parent = nullptr);

    void setFileActions(QAction* open, QAction* save);
    void setQuickActions(QAction* check, QAction* run, QAction* sweep, QAction* stop);
    void setAverageGainAction(QAction* action);
    void setConvergenceAction(QAction* action);
    void setDocumentState(const QString& fileName, bool modified);
    void setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
        const nec::NecDocument& document, const QString& solver, bool checked,
        std::size_t errors, std::size_t warnings);
    void setResults(const analysis::AnalysisResult& result, const QString& context, bool stale);
    void markResultsStale();
    void setStudyStatus(Study study, StudyState state, const QString& text = {},
        const QString& toolTip = {});
    void clearStudy(Study study);
    void setRecentRuns(const std::vector<AnalysisRunRecord>& runs);
    void setResultWindowVisible(Workspace workspace, bool visible);
    void setOpenRunHistoryCallback(std::function<void()> callback);
    void setOpenWorkspaceCallback(std::function<void(Workspace)> callback);
    void setOpenEditorCallback(std::function<void(Editor)> callback);

private:
    struct ResultLauncher {
        Workspace workspace{};
        QToolButton* button{};
        QString label;
        bool available{};
        bool visible{};
    };
    void refreshHeader();
    void refreshFooter();
    void refreshQuickResults();
    void refreshResultLauncher(Workspace workspace);
    void setModelRow(Editor editor, const QString& value);
    [[nodiscard]] auto resultLauncher(Workspace workspace) -> ResultLauncher*;

    QLabel* modelName_{};
    QToolButton* backendState_{};
    QToolButton* openButton_{};
    QToolButton* saveButton_{};
    QToolButton* runButton_{};
    QToolButton* sweepButton_{};
    QToolButton* stopButton_{};
    QToolButton* checkButton_{};
    QLabel* modelState_{};
    QLabel* resultState_{};
    QComboBox* resultFrequency_{};
    QLabel* resultStale_{};
    QWidget* quickResults_{};
    QLabel* resultExtrema_{};
    QToolButton* resistanceResult_{};
    QToolButton* reactanceResult_{};
    QToolButton* swrResult_{};
    QLabel* gainResultLabel_{};
    QToolButton* gainResult_{};
    QLabel* frontToBackResultLabel_{};
    QToolButton* frontToBackResult_{};
    std::array<QLabel*, 2> studyStateLabels_{};
    std::array<QToolButton*, 2> studyButtons_{};
    std::array<StudyState, 2> studyStates_{StudyState::NotRun, StudyState::NotRun};
    QLabel* footer_{};
    QTableWidget* modelRows_{};
    std::array<ResultLauncher, 4> resultLaunchers_{};
    std::function<void()> openRunHistory_;
    std::function<void(Workspace)> openWorkspace_;
    std::function<void(Editor)> openEditor_;
    QString fileName_{tr("Untitled model.nec")};
    QString backend_;
    QString modelStatus_{tr("Model not checked")};
    QString resultStatus_{tr("Results none")};
    QString resultContext_;
    QString lastRunText_{tr("No completed run")};
    analysis::AnalysisResult currentResult_;
    bool modified_{};
    bool resultsStale_{};
};

}
