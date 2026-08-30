#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/ModelSetup.h"
#include "nec/NecDocument.h"
#include "ui/geometry/GeometrySettings.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "analysis/AnalysisResult.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

class QAction;
class QCloseEvent;
class QDockWidget;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;
class QTimer;

namespace necwb::ui {

class NecEditor;
class AnalysisSetupEditor;
class AnalysisRequestEditor;
class Geometry3DView;
class GeometryView;
class ImpedanceResultsView;
class SweepPlotsView;
class CurrentDistributionView;
class RadiationPatternView;
class Radiation3DView;
class SetupEditor;
class LoadNetworkEditor;
class WireCardEditor;
class StructuredCardEditor;
class DashboardPage;
class WelcomePage;
class OptimizationWorkspace;

class MainWindow final : public QMainWindow {
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createWorkspace();
    void createDocks();
    void createMenusAndToolbar();
    void showModule(int index);
    void showModelTab(int index);
    void setDisplayLengthUnit(model::LengthUnit unit);
    void setSnapSpacing(double meters);
    void applyGeometrySettings(const GeometrySettings& settings);
    void showGeometrySettings();
    void showAutoSegmentation();
    [[nodiscard]] auto formatLength(double meters) const -> QString;
    void newModel();
    void openFile();
    void openFileAtPath(const QString& path);
    void openExample();
    [[nodiscard]] auto recentFiles() const -> QStringList;
    void rememberRecentFile(const QString& path);
    void clearRecentFiles();
    auto saveFile() -> bool;
    auto saveFileAs() -> bool;
    auto writeFile(const QString& path) -> bool;
    auto maybeSaveChanges() -> bool;
    void checkModel();
    void clearCheckResults();
    void goToDiagnostic(QTreeWidgetItem* item);
    void activateProjectItem(QTreeWidgetItem* item);
    void selectWireInProject(int tag);
    void selectExcitation(std::size_t sourceLine);
    void selectLoad(std::size_t sourceLine);
    void selectTransmissionLine(std::size_t sourceLine);
    void synchronizeGeometrySelection(int tag);
    void fitAllGeometryViews();
    void previewEndpointMove(int tag, model::WireEndpoint endpoint, const model::Point3D& position);
    void commitEndpointMove(int tag, model::WireEndpoint endpoint,
        const model::Point3D& original, const model::Point3D& updated);
    void applyEndpointMove(int tag, model::WireEndpoint endpoint, const model::Point3D& position);
    void previewWireMove(int tag, const model::Point3D& start, const model::Point3D& end);
    void commitWireMove(int tag, const model::Point3D& originalStart, const model::Point3D& originalEnd,
        const model::Point3D& updatedStart, const model::Point3D& updatedEnd);
    void applyWireMove(int tag, const model::Point3D& start, const model::Point3D& end);
    void addWire(const model::Point3D& start, const model::Point3D& end);
    void splitWire(int tag, const model::Point3D& position);
    void deleteWire(int tag);
    void duplicateWire(int tag);
    void showWireProperties(int tag);
    void editWire(const model::Wire& original, const model::Wire& updated);
    void editStructuredCard(std::size_t sourceLine, const QString& cardText);
    void addStructuredCard(const QString& cardText);
    void deleteStructuredCard(std::size_t sourceLine);
    void changeFrequency(const model::FrequencyDefinition& frequency);
    void deleteFrequency(std::size_t sourceLine);
    void changeGround(const model::GroundDefinition& ground);
    void changeExcitation(const model::Excitation& excitation);
    void addExcitationAt(int wireTag, int segment);
    void showExcitationEditor(std::size_t sourceLine);
    void showExcitationInSetup(std::size_t sourceLine);
    void deleteExcitation(std::size_t sourceLine);
    void addLoadAt(int wireTag, int segment);
    void chooseTransmissionLineEndpoint(int wireTag, int segment);
    void setPendingTransmissionLineEndpoint(std::optional<std::pair<int, int>> endpoint);
    void showLoadInEditor(std::size_t sourceLine);
    void showTransmissionLineInEditor(std::size_t sourceLine);
    void upsertSetupCard(const QString& description, std::size_t sourceLine,
        const QString& cardText, bool frequencyCard);
    void deleteSetupCard(const QString& description, std::size_t sourceLine);
    void changeAnalysisRequests(bool executionEnabled, const model::ExecutionRequest& execution,
        bool patternEnabled, const model::RadiationPatternRequest& pattern);
    void changeLoad(const model::LoadDefinition& load);
    void changeTransmissionLine(const model::TransmissionLineDefinition& line);
    void updateAnalysisReadiness();
    void startAnalysis();
    void cancelAnalysis();
    void appendSolverOutput(const QString& text);
    void finishAnalysis(int exitCode, QProcess::ExitStatus exitStatus);
    void failAnalysis(const QString& message);
    void setCurrentRunStatus(const QString& status);
    void loadRunHistory();
    void addRunRecord(const AnalysisRunRecord& record, bool prepend);
    void loadSelectedRun();
    auto loadRunModel(const QString& directory, const QString& modelName,
        const QString& runContext) -> bool;
    void deleteSelectedRun();
    void updateRunSelectionActions();
    void displayRunArtifacts(const QString& directory, const QString& context);
    void setDisplayedResults(const analysis::AnalysisResult& result);
    void applyResultFrequency(double frequencyMHz);
    void clearDisplayedResults();
    void pushGeometrySourceEdit(const QString& description, QString updatedSource);
    void applyGeometrySource(const QString& source, int targetTabIndex);
    [[nodiscard]] auto nextWireTag() const -> int;
    void replaceWireSourceLine(const model::Wire& wire);
    void refreshGeometryViews();
    void performUndo();
    void performRedo();
    void updateUndoActions();
    void updateProjectTree(const model::AntennaModel& model, const nec::NecDocument& document);
    void showProjectItemProperties(QTreeWidgetItem* item);
    void showCardProperties(std::size_t sourceLine);
    void populateWireProperties(const model::Wire& wire);
    void populateLoadProperties(const model::LoadDefinition& load);
    void populateTransmissionLineProperties(const model::TransmissionLineDefinition& line);
    void setCurrentFile(QString path);
    void restoreWorkspaceLayout();
    void saveWorkspaceLayout();

    QAction* newAction_{};
    QAction* openAction_{};
    QAction* saveAction_{};
    QAction* saveAsAction_{};
    QAction* checkAction_{};
    QAction* runAction_{};
    QAction* fitGeometryAction_{};
    QAction* snapGridAction_{};
    QAction* snapEndpointsAction_{};
    QAction* geometrySettingsAction_{};
    QAction* autoSegmentationAction_{};
    QAction* undoAction_{};
    QAction* redoAction_{};
    QAction* homeModuleAction_{};
    QAction* modelModuleAction_{};
    QAction* sourceModuleAction_{};
    QAction* analysisModuleAction_{};
    QAction* visualizeModuleAction_{};
    QAction* optimizeModuleAction_{};
    QUndoStack* undoStack_{};
    QComboBox* lengthUnitControl_{};
    QDoubleSpinBox* snapSpacingControl_{};
    QStackedWidget* moduleStack_{};
    QStackedWidget* dashboardStack_{};
    QTabWidget* workspace_{};
    QTabWidget* sourceWorkspace_{};
    QTabWidget* resultsWorkspace_{};
    QComboBox* resultsFrequencyControl_{};
    DashboardPage* dashboardPage_{};
    WelcomePage* welcomePage_{};
    WireCardEditor* wireCardEditor_{};
    StructuredCardEditor* structuredCardEditor_{};
    NecEditor* editor_{};
    GeometryView* xyView_{};
    GeometryView* xzView_{};
    GeometryView* yzView_{};
    Geometry3DView* geometry3DView_{};
    SetupEditor* setupEditor_{};
    LoadNetworkEditor* loadNetworkEditor_{};
    AnalysisSetupEditor* analysisSetupEditor_{};
    AnalysisRequestEditor* analysisRequestEditor_{};
    ImpedanceResultsView* analysisResultsView_{};
    ImpedanceResultsView* visualizeResultsView_{};
    SweepPlotsView* visualizePlotsView_{};
    CurrentDistributionView* currentResultsView_{};
    RadiationPatternView* radiationPatternView_{};
    Radiation3DView* radiation3DView_{};
    OptimizationWorkspace* optimizationWorkspace_{};
    QTabWidget* analysisWorkspace_{};
    QTableWidget* analysisRuns_{};
    QPlainTextEdit* analysisOutput_{};
    QPushButton* cancelRunButton_{};
    QPushButton* openRunFolderButton_{};
    QPushButton* openRunResultsButton_{};
    QPushButton* deleteRunButton_{};
    QTreeWidget* projectTree_{};
    QTableWidget* properties_{};
    QTreeWidget* diagnostics_{};
    QPlainTextEdit* solverOutput_{};
    QDockWidget* projectDock_{};
    QDockWidget* propertiesDock_{};
    QDockWidget* diagnosticsDock_{};
    QDockWidget* solverOutputDock_{};
    QDockWidget* messagesDock_{};
    QLabel* checkStatus_{};
    QLabel* modelFileStatus_{};
    QLabel* resultsStatusLabel_{};
    QLabel* resultsAvailabilityLabel_{};
    analysis::AnalysisResult displayedResults_;
    model::AntennaModel currentModel_;
    model::ModelSetup currentSetup_;
    GeometrySettings geometrySettings_;
    QString currentFile_;
    QString solverBackendId_{QStringLiteral("nec2")};
    QString solverExecutablePath_;
    int solverTimeoutSeconds_{120};
    QProcess* solverProcess_{};
    QTimer* solverTimeout_{};
    QElapsedTimer solverElapsed_;
    QString currentRunDirectory_;
    QString currentRunOutputPath_;
    QString displayedRunDirectory_;
    AnalysisRunStore runStore_;
    std::optional<AnalysisRunRecord> currentRunRecord_;
    int currentRunRow_{-1};
    bool currentRunCanceled_{};
    bool currentRunTimedOut_{};
    int sourceTabIndex_{};
    int geometryTabIndex_{};
    int structuredSourceTabIndex_{1};
    int setupTabIndex_{};
    int loadNetworkTabIndex_{};
    int homeModuleIndex_{};
    int modelModuleIndex_{};
    int sourceModuleIndex_{};
    int analysisModuleIndex_{};
    int analysisRunsTabIndex_{};
    int analysisResultsTabIndex_{};
    int analysisOutputTabIndex_{};
    int visualizeModuleIndex_{};
    int optimizeModuleIndex_{};
    bool updatingSourceFromGeometry_{};
    std::size_t modelErrorCount_{};
    std::size_t modelWarningCount_{};
    bool modelChecked_{};
    bool hasNecModel_{};
    bool resultsAvailable_{};
    std::optional<std::pair<int, int>> pendingTransmissionLineEndpoint_;
};

}
