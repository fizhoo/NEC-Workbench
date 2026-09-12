#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/ModelSetup.h"
#include "nec/NecDocument.h"
#include "nec/DeckGeometryUnits.h"
#include "ui/geometry/GeometrySettings.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/SolverProcessRunner.h"
#include "analysis/AnalysisResult.h"
#include "analysis/AverageGainTest.h"
#include "analysis/SolverCommand.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

class QAction;
class QCloseEvent;
class QDockWidget;
class QDialog;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace necwb::ui {

class NecEditor;
class AnalysisSetupEditor;
class AnalysisRequestEditor;
class Geometry3DView;
class GeometryView;
class ImpedanceResultsView;
class ResultsSummaryView;
class AverageGainResultsView;
class ConvergenceWorkspace;
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
class ParameterEditor;

class MainWindow final : public QMainWindow {
public:
    enum class WorkspaceDensity {
        Compact,
        Standard,
        Spacious
    };

    MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class SolverRunPurpose {
        Analysis,
        AverageGainTest
    };

    enum class EditorDestination {
        Geometry,
        RawSource,
        Frequency,
        Parameters,
        Sources,
        Loads,
        Environment
    };

    void createActions();
    void createWorkspace();
    void createResultsWorkspace();
    void createOptimizationWorkspace();
    void createDocks();
    void createMenusAndToolbar();
    void resetWorkspaceLayout();
    void setWorkspaceDensity(WorkspaceDensity density);
    void applyWorkspaceDensity();
    void showGettingStarted();
    void openUserGuide();
    void showAboutDialog();
    auto showModule(int index) -> bool;
    [[nodiscard]] auto confirmCurrentEditorNavigation() -> bool;
    [[nodiscard]] auto confirmPendingEdits(QWidget* page) -> bool;
    void toggleResultsDetached();
    void detachResults();
    void attachResults();
    void showDetachedResults();
    void presentCompletedAnalysisResults();
    void showEditor(EditorDestination destination, int geometryTab = 0);
    void setDisplayLengthUnit(model::LengthUnit unit);
    void changeDeckLengthUnit(model::LengthUnit unit);
    void updateDeckUnitControls(const nec::DeckGeometryUnitInfo& info);
    [[nodiscard]] auto deckScaleForSourceLine(std::size_t sourceLine) const -> double;
    void setSnapSpacing(double meters);
    void applyGeometrySettings(const GeometrySettings& settings);
    void showGeometrySettings();
    void showAutoSegmentation();
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
    void makeFieldOptimizable(std::size_t sourceLine,
        std::size_t fieldIndex, const QString& fieldLabel);
    void addStructuredCard(const QString& cardText);
    void deleteStructuredCard(std::size_t sourceLine);
    void changeFrequency(const model::FrequencyDefinition& frequency);
    void deleteFrequency(std::size_t sourceLine);
    void changeParameter(std::size_t sourceLine, const QString& originalName,
        const QString& name, const QString& expression);
    void deleteParameter(std::size_t sourceLine, const QString& name);
    auto applyOptimizedParameter(const QString& name, double value) -> bool;
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
    void changeExecutionRequest(bool enabled, const model::ExecutionRequest& execution);
    void changeRadiationPattern(const model::RadiationPatternRequest& pattern);
    void changeLoad(const model::LoadDefinition& load);
    void changeTransmissionLine(const model::TransmissionLineDefinition& line);
    void updateAnalysisReadiness();
    void startAnalysis();
    void startAverageGainTest();
    void showConvergenceStudy();
    void synchronizeRunnerState();
    void startSolverProcess(const analysis::SolverCommand& command, const QString& activity);
    void cancelAnalysis();
    void appendSolverOutput(const QString& text);
    void updateSolverActivity();
    void completeSolverActivity(const QString& status);
    void finishAnalysis(SolverProcessResult result);
    void setCurrentRunStatus(const QString& status);
    void loadRunHistory();
    void addRunRecord(const AnalysisRunRecord& record, bool prepend);
    void loadSelectedRun();
    void inspectSelectedRunInput();
    void openSelectedRunSnapshot();
    auto loadRunModel(const QString& directory, const QString& modelName,
        const QString& runContext) -> bool;
    void deleteSelectedRun();
    void updateRunSelectionActions();
    void displayRunArtifacts(const QString& directory, const QString& context, bool historical = true);
    void displayAverageGainTestArtifacts(const QString& directory, const QString& context);
    void returnToCurrentWork();
    void captureHistoricalReturnContext();
    void leaveHistoricalSessionViews();
    void showHistoricalResultsContext(const QString& modelName, const QString& started,
        const QString& backend);
    void showActiveResultsContext(const QString& context);
    void updateActiveModelResultsLabel();
    void setDisplayedResults(const analysis::AnalysisResult& result);
    void applyResultFrequency(double frequencyMHz);
    void jumpRawOutputToSelectedFrequency();
    void findInRawOutput();
    void clearDisplayedResults();
    void pushGeometrySourceEdit(const QString& description, QString updatedSource);
    void applyGeometrySource(const QString& source, EditorDestination destination,
        int geometryTab, int analysisTab);
    [[nodiscard]] auto nextWireTag() const -> int;
    void replaceWireSourceLine(const model::Wire& wire);
    [[nodiscard]] auto wireHasSymbolicGeometry(std::size_t sourceLine) const -> bool;
    void showSymbolicGeometryEditBlocked();
    void refreshGeometryViews();
    void updateWireCardEditor();
    void performUndo();
    void performRedo();
    void updateUndoActions();
    void updateProjectTree(const model::AntennaModel& model, const nec::NecDocument& document);
    void synchronizeProjectItemSelection(QTreeWidgetItem* item);
    void setCurrentFile(QString path);
    void restoreWorkspaceLayout();
    void saveWorkspaceLayout();

    QAction* newAction_{};
    QAction* openAction_{};
    QAction* saveAction_{};
    QAction* saveAsAction_{};
    QAction* exitAction_{};
    QAction* cutAction_{};
    QAction* copyAction_{};
    QAction* pasteAction_{};
    QAction* checkAction_{};
    QAction* runAction_{};
    QAction* stopAction_{};
    QAction* averageGainAction_{};
    QAction* convergenceAction_{};
    QAction* fitGeometryAction_{};
    QAction* snapGridAction_{};
    QAction* snapEndpointsAction_{};
    QAction* geometrySettingsAction_{};
    QAction* autoSegmentationAction_{};
    QAction* undoAction_{};
    QAction* redoAction_{};
    QAction* homeModuleAction_{};
    QAction* modelModuleAction_{};
    QAction* analysisModuleAction_{};
    QAction* resultsModuleAction_{};
    QAction* optimizeModuleAction_{};
    QAction* detachResultsAction_{};
    QAction* resetLayoutAction_{};
    QAction* compactDensityAction_{};
    QAction* standardDensityAction_{};
    QAction* spaciousDensityAction_{};
    QAction* gettingStartedAction_{};
    QAction* userGuideAction_{};
    QAction* aboutAction_{};
    QUndoStack* undoStack_{};
    QComboBox* lengthUnitControl_{};
    QComboBox* deckUnitControl_{};
    QLabel* deckScaleLabel_{};
    QDoubleSpinBox* snapSpacingControl_{};
    QStackedWidget* moduleStack_{};
    QStackedWidget* dashboardStack_{};
    QTabWidget* modelWorkspace_{};
    QTabWidget* workspace_{};
    QTabWidget* sourceWorkspace_{};
    QTabWidget* resultsWorkspace_{};
    QWidget* resultsHost_{};
    QWidget* resultsContent_{};
    QWidget* detachedResultsPlaceholder_{};
    QVBoxLayout* resultsHostLayout_{};
    QDialog* detachedResultsWindow_{};
    QComboBox* resultsFrequencyControl_{};
    QLineEdit* rawOutputFindControl_{};
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
    ResultsSummaryView* resultsSummaryView_{};
    AverageGainResultsView* averageGainResultsView_{};
    ConvergenceWorkspace* convergenceWorkspace_{};
    ImpedanceResultsView* analysisResultsView_{};
    SweepPlotsView* visualizePlotsView_{};
    CurrentDistributionView* currentResultsView_{};
    RadiationPatternView* radiationPatternView_{};
    Radiation3DView* radiation3DView_{};
    OptimizationWorkspace* optimizationWorkspace_{};
    ParameterEditor* parameterEditor_{};
    QTabWidget* analysisWorkspace_{};
    int previousModelWorkspaceIndex_{-1};
    int previousAnalysisWorkspaceIndex_{-1};
    bool restoringWorkspaceTab_{};
    QTabWidget* validationWorkspace_{};
    QTableWidget* analysisRuns_{};
    QWidget* runsPage_{};
    QPlainTextEdit* analysisOutput_{};
    QPushButton* cancelRunButton_{};
    QPushButton* openRunFolderButton_{};
    QPushButton* openRunResultsButton_{};
    QPushButton* inspectRunInputButton_{};
    QPushButton* openRunSnapshotButton_{};
    QPushButton* deleteRunButton_{};
    QTreeWidget* projectTree_{};
    QTreeWidget* diagnostics_{};
    QPlainTextEdit* solverOutput_{};
    QDockWidget* projectDock_{};
    QDockWidget* diagnosticsDock_{};
    QDockWidget* solverOutputDock_{};
    QLabel* validationSummary_{};
    QLabel* validationScope_{};
    QLabel* checkStatus_{};
    QWidget* solverActivityWidget_{};
    QLabel* solverActivityLabel_{};
    QProgressBar* solverActivityProgress_{};
    QPushButton* solverActivityCancelButton_{};
    QLabel* modelFileStatus_{};
    QLabel* resultsContextTitleLabel_{};
    QLabel* resultsContextDetailLabel_{};
    QLabel* activeModelResultsLabel_{};
    QPushButton* returnToActiveResultsButton_{};
    QPushButton* detachResultsButton_{};
    QLabel* resultsStatusLabel_{};
    QLabel* resultsAvailabilityLabel_{};
    analysis::AnalysisResult displayedResults_;
    model::AntennaModel currentModel_;
    model::ModelSetup currentSetup_;
    GeometrySettings geometrySettings_;
    WorkspaceDensity workspaceDensity_{WorkspaceDensity::Compact};
    double deckScaleToMeters_{1.0};
    bool updatingDeckUnitControl_{};
    QString currentFile_;
    QString solverBackendId_{QStringLiteral("nec2")};
    QString solverExecutablePath_;
    int solverTimeoutSeconds_{120};
    SolverProcessRunner* solverRunner_{};
    QTimer* solverActivityTimer_{};
    QElapsedTimer solverElapsed_;
    QString solverActivityName_;
    QString solverActivityPhase_;
    QString currentRunDirectory_;
    QString currentRunOutputPath_;
    QString displayedRunDirectory_;
    QString activeModelResultsDirectory_;
    QString activeModelResultsContext_;
    AnalysisRunStore runStore_;
    std::optional<AnalysisRunRecord> currentRunRecord_;
    SolverRunPurpose currentRunPurpose_{SolverRunPurpose::Analysis};
    analysis::AverageGainEnvironment currentAverageGainEnvironment_{
        analysis::AverageGainEnvironment::FreeSpace};
    double currentAverageGainFrequencyMHz_{};
    int currentRunRow_{-1};
    int geometryTabIndex_{};
    int structuredSourceTabIndex_{1};
    int loadNetworkTabIndex_{};
    int homeModuleIndex_{};
    int modelModuleIndex_{};
    int modelGeometryWorkspaceIndex_{};
    int modelParametersWorkspaceIndex_{};
    int modelSourcesWorkspaceIndex_{};
    int modelEnvironmentWorkspaceIndex_{};
    int modelDeckWorkspaceIndex_{};
    int analysisModuleIndex_{};
    int analysisFrequencyTabIndex_{};
    int analysisRunsTabIndex_{};
    int resultsSummaryTabIndex_{};
    int averageGainResultsTabIndex_{};
    int averageGainValidationTabIndex_{};
    int convergenceValidationTabIndex_{};
    int analysisOutputTabIndex_{};
    int resultsModuleIndex_{};
    int optimizeModuleIndex_{};
    int lastNonResultsModuleIndex_{-1};
    int historicalReturnModuleIndex_{-1};
    int historicalReturnResultsTabIndex_{-1};
    bool updatingSourceFromGeometry_{};
    std::size_t modelErrorCount_{};
    std::size_t modelWarningCount_{};
    bool modelChecked_{};
    bool hasNecModel_{};
    bool resultsAvailable_{};
    bool displayingHistoricalResults_{};
    bool historicalReviewActive_{};
    bool historicalSessionViewActive_{};
    bool resultsDetached_{};
    std::optional<std::pair<int, int>> pendingTransmissionLineEndpoint_;
};

}
