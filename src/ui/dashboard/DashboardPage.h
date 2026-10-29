#pragma once

#include "analysis/AnalysisResult.h"
#include "model/AntennaModel.h"
#include "model/ModelSetup.h"
#include "ui/analysis/AnalysisRunStore.h"

#include <QWidget>

#include <functional>
#include <vector>

class QAction;
class QLabel;
class QTableWidget;
class QToolButton;

namespace necwb::nec { class NecDocument; }

namespace necwb::ui {

class DashboardPage final : public QWidget {
public:
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

    explicit DashboardPage(QWidget* parent = nullptr);

    void setFileActions(QAction* open, QAction* save);
    void setQuickActions(QAction* check, QAction* run, QAction* sweep, QAction* stop);
    void setDocumentState(const QString& fileName, bool modified);
    void setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
        const nec::NecDocument& document, const QString& solver, bool checked,
        std::size_t errors, std::size_t warnings);
    void setResults(const analysis::AnalysisResult& result, const QString& context, bool stale);
    void markResultsStale();
    void setRecentRuns(const std::vector<AnalysisRunRecord>& runs);
    void setResultWindowVisible(Workspace workspace, bool visible);
    void setOpenRunHistoryCallback(std::function<void()> callback);
    void setOpenWorkspaceCallback(std::function<void(Workspace)> callback);
    void setOpenEditorCallback(std::function<void(Editor)> callback);

private:
    void refreshHeader();
    void refreshFooter();
    void refreshResultButton(Workspace workspace);
    void refreshGeometryViewButton();
    void setModelRow(Editor editor, const QString& value);
    [[nodiscard]] auto resultButton(Workspace workspace) const -> QToolButton*;

    QLabel* modelName_{};
    QToolButton* backendState_{};
    QLabel* modelState_{};
    QLabel* resultState_{};
    QLabel* latestResult_{};
    QLabel* footer_{};
    QTableWidget* modelRows_{};
    QToolButton* impedanceButton_{};
    QToolButton* currentsButton_{};
    QToolButton* radiationButton_{};
    QToolButton* geometryViewButton_{};
    std::function<void()> openRunHistory_;
    std::function<void(Workspace)> openWorkspace_;
    std::function<void(Editor)> openEditor_;
    QString fileName_{tr("Untitled model.nec")};
    QString backend_;
    QString modelStatus_{tr("Model not checked")};
    QString resultStatus_{tr("Results none")};
    QString resultContext_;
    QString lastRunText_{tr("No completed run")};
    bool modified_{};
    bool resultsStale_{};
    bool impedanceAvailable_{};
    bool currentsAvailable_{};
    bool radiationAvailable_{};
    bool impedanceVisible_{};
    bool currentsVisible_{};
    bool radiationVisible_{};
    bool geometryViewVisible_{};
};

}
