#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/ModelSetup.h"
#include "nec/NecDocument.h"
#include "nec/DeckGeometryUnits.h"
#include "nec/NecSourceCache.h"
#include "ui/geometry/GeometrySettings.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/ResultReferenceController.h"
#include "ui/analysis/SolverProcessRunner.h"
#include "ui/modeltree/ModelTree.h"
#include "analysis/AnalysisResult.h"
#include "analysis/AverageGainTest.h"
#include "analysis/SolverCommand.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <string>
#include <utility>

class QAction;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QMenu;
class QTabWidget;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace necwb::ui {

class NecEditor;
class AnalysisSetupEditor;
class AnalysisRequestEditor;
class Geometry3DView;
class GeometryView;
class RunReviewWindow;
class AverageGainResultsView;
class ConvergenceWorkspace;
class SetupEditor;
class LoadNetworkEditor;
class WireCardEditor;
class StructuredCardEditor;
class WelcomePage;
class DeckView;
class ModelColumn;
class WirePropertiesEditor;
class WorkbenchToolBar;
class OptimizationWorkspace;
class ParameterEditor;
class DetachablePanel;
class ResultsController;
class WorkbenchStatusBar;
class BottomPanel;
class WorkbenchLayout;

class MainWindow final : public QMainWindow {
public:
    enum class WorkspaceDensity {
        Compact,
        Standard,
        Spacious
    };

    MainWindow();
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    enum class SolverRunPurpose {
        Analysis,
        AverageGainTest,
        OptimizationPeakSearch,
        OptimizationBaseline
    };

    enum class EditorDestination {
        Geometry,
        RawSource,
        Frequency,
        Parameters,
        Sources,
        Loads,
        Environment,
        Requests,
        Solver
    };

    struct SourceHistoryEntry {
        QString description;
        EditorDestination destination{EditorDestination::RawSource};
        int geometryTab{};
        int sourceTab{};
    };

    void createActions();
    void createModelColumn();
    void createWorkspace();
    void createResultsColumn();
    void createValidationWindow();
    void createOptimizationWorkspace();
    void createMenusAndToolbar();
    void resetWorkspaceLayout();
    void setWorkspaceDensity(WorkspaceDensity density);
    void applyWorkspaceDensity();
    void applyWorkspaceDensity(QWidget* root);
    void showGettingStarted();
    void openUserGuide();
    void showAboutDialog();
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
    void refreshRecentFileViews(const QStringList& files);
    void rememberRecentFile(const QString& path);
    void clearRecentFiles();
    auto saveFile() -> bool;
    auto saveFileAs() -> bool;
    auto writeFile(const QString& path) -> bool;
    auto maybeSaveChanges() -> bool;
    void checkModel(bool presentReport = false);
    void showGeneratedSolverDeck();
    void clearCheckResults();
    void goToDiagnostic(std::size_t sourceLine);
    void selectWireInProject(int tag);
    void selectExcitation(std::size_t sourceLine);
    void selectLoad(std::size_t sourceLine);
    void selectTransmissionLine(std::size_t sourceLine);
    void selectNetwork(std::size_t sourceLine);
    // Selects a row of the model tree, applying the open page's edits first.
    void selectInModel(const ModelSelection& selection);
    void showModelSelection(const ModelSelection& selection);
    [[nodiscard]] auto paneContent(ModelNode node) const -> std::pair<QWidget*, QString>;
    // Where an edit made in the properties pane returns to on undo.
    [[nodiscard]] auto paneDestination() const -> EditorDestination;
    void showSetupPage(EditorDestination destination);
    void showProperties(const ModelSelection& selection);
    void refreshWireEditor();
    // Shows the engine in the toolbar and on the model tree's Solver row.
    void refreshEngine();
    void discardPendingEdits(QWidget* page);
    void synchronizeGeometrySelection(int tag);
    void fitAllGeometryViews();
    void previewEndpointMove(int tag, model::WireEndpoint endpoint, const model::Point3D& position);
    void commitEndpointMove(int tag, model::WireEndpoint endpoint,
        const model::Point3D& original, const model::Point3D& updated);
    void previewWireMove(int tag, const model::Point3D& start, const model::Point3D& end);
    void commitWireMove(int tag, const model::Point3D& originalStart, const model::Point3D& originalEnd,
        const model::Point3D& updatedStart, const model::Point3D& updatedEnd);
    void addWire(const model::Point3D& start, const model::Point3D& end);
    void splitWire(int tag, const model::Point3D& position);
    void deleteWire(int tag);
    void duplicateWire(int tag);
    void editWire(const model::Wire& original, const model::Wire& updated);
    void editStructuredCard(std::size_t sourceLine, const QString& cardText);
    void makeFieldOptimizable(std::size_t sourceLine,
        std::size_t fieldIndex, const QString& fieldLabel);
    void detachFieldParameter(std::size_t sourceLine,
        std::size_t fieldIndex, const QString& fieldLabel);
    void addStructuredCard(const QString& cardText);
    void deleteStructuredCard(std::size_t sourceLine);
    void changeFrequency(const model::FrequencyDefinition& frequency);
    void deleteFrequency(std::size_t sourceLine);
    void changeParameter(std::size_t sourceLine, const QString& originalName,
        const QString& name, const QString& expression);
    void deleteParameter(std::size_t sourceLine, const QString& name);
    auto applyOptimizedParameters(
        const std::vector<std::pair<QString, double>>& values) -> bool;
    void changeGround(const model::GroundDefinition& ground);
    void changeReferenceImpedance(const model::ReferenceImpedanceDefinition& reference);
    void deleteReferenceImpedance(std::size_t sourceLine);
    void changeExcitation(const model::Excitation& excitation);
    void addExcitationAt(int wireTag, int segment);
    void deleteExcitation(std::size_t sourceLine);
    void addLoadAt(int wireTag, int segment);
    void chooseTransmissionLineEndpoint(int wireTag, int segment);
    void setPendingTransmissionLineEndpoint(std::optional<std::pair<int, int>> endpoint);
    // Adds new NT and TL cards to the existing NT/TL group; NEC-2 would discard the earlier
    // networks if other cards came between them.
    void upsertNetworkCard(const QString& description, std::size_t sourceLine,
        const QString& cardText);
    void upsertSetupCard(const QString& description, std::size_t sourceLine,
        const QString& cardText, bool frequencyCard);
    void deleteSetupCard(const QString& description, std::size_t sourceLine);
    void changeExecutionRequest(bool enabled, const model::ExecutionRequest& execution);
    void changeRadiationPattern(const model::RadiationPatternRequest& pattern);
    void changeLoad(const model::LoadDefinition& load);
    void changeTransmissionLine(const model::TransmissionLineDefinition& line);
    void changeNetwork(const model::NetworkDefinition& network);
    void updateAnalysisReadiness();
    void startAnalysis();
    void startQuickFrequencySweep();
    void startAnalysisRun(const std::string& authoredSource, const std::string& solverInput,
        const QString& runType, const QString& summary, const QString& activity,
        SolverRunPurpose purpose = SolverRunPurpose::Analysis);
    [[nodiscard]] auto startOptimizationPeakSearch(double frequencyMHz) -> bool;
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
    void showRunHistory();
    void refreshLatestRun();
    void loadSelectedRun();
    void inspectSelectedRunInput();
    void openSelectedRunSnapshot();
    auto loadRunModel(const QString& directory, const QString& modelName,
        const QString& runContext) -> bool;
    void deleteSelectedRun();
    void pinSelectedRunReference();
    void chooseResultReference(QWidget* anchor = nullptr);
    void editSelectedRunNote();
    void clearResultReference();
    void setResultReferenceVisible(bool visible);
    void updateResultReference();
    [[nodiscard]] auto selectedRunRecord() const -> std::optional<AnalysisRunRecord>;
    void updateRunSelectionActions();
    void displayRunArtifacts(const QString& directory, const QString& context);
    void setDisplayedResults(const analysis::AnalysisResult& result);
    void clearDisplayedResults();
    void applyReferenceImpedance(double ohms);
    void pushGeometrySourceEdit(const QString& description, QString updatedSource);
    void applyGeometrySource(const QString& description, const QString& source,
        EditorDestination destination, int geometryTab, int sourceTab);
    [[nodiscard]] auto nextWireTag() const -> int;
    [[nodiscard]] auto wireHasSymbolicGeometry(std::size_t sourceLine) const -> bool;
    void showSymbolicGeometryEditBlocked();
    void refreshGeometryViews();
    void updateWireCardEditor();
    void performUndo();
    void performRedo();
    void recordSourceUndoCommand();
    void resetSourceUndoHistory();
    void navigateToSourceHistoryEntry(const SourceHistoryEntry& entry);
    void updateUndoActions();
    [[nodiscard]] auto activeSource() const -> const nec::NecSourceCache&;
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
    QAction* quickSweepAction_{};
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
    QAction* necDeckAction_{};
    QAction* optimizeModuleAction_{};
    QAction* resetLayoutAction_{};
    QAction* problemsAction_{};
    QAction* runLogAction_{};
    QAction* compactDensityAction_{};
    QAction* standardDensityAction_{};
    QAction* spaciousDensityAction_{};
    QAction* gettingStartedAction_{};
    QAction* userGuideAction_{};
    QAction* aboutAction_{};
    QMenu* recentModelsMenu_{};
    QComboBox* lengthUnitControl_{};
    QComboBox* deckUnitControl_{};
    QLabel* deckScaleLabel_{};
    QDoubleSpinBox* snapSpacingControl_{};
    QTabWidget* workspace_{};
    QTabWidget* sourceWorkspace_{};
    QWidget* windowHost_{};
    WorkbenchLayout* workbenchLayout_{};
    DetachablePanel* validationPanel_{};
    DeckView* deckView_{};
    DetachablePanel* optimizationPanel_{};
    ModelColumn* modelColumn_{};
    WirePropertiesEditor* wireEditor_{};
    WorkbenchToolBar* toolBar_{};
    WelcomePage* welcomePage_{};
    WireCardEditor* wireCardEditor_{};
    StructuredCardEditor* structuredCardEditor_{};
    QLabel* structuredParameterizationHint_{};
    NecEditor* editor_{};
    GeometryView* xyView_{};
    GeometryView* xzView_{};
    GeometryView* yzView_{};
    Geometry3DView* geometry3DView_{};
    SetupEditor* setupEditor_{};
    LoadNetworkEditor* loadNetworkEditor_{};
    AnalysisSetupEditor* analysisSetupEditor_{};
    AnalysisRequestEditor* analysisRequestEditor_{};
    RunReviewWindow* runReviewWindow_{};
    AverageGainResultsView* averageGainResultsView_{};
    ConvergenceWorkspace* convergenceWorkspace_{};
    OptimizationWorkspace* optimizationWorkspace_{};
    ParameterEditor* parameterEditor_{};
    QTabWidget* validationWorkspace_{};
    QLabel* validationOverviewSummary_{};
    QLabel* validationOverviewScope_{};
    WorkbenchStatusBar* statusBar_{};
    BottomPanel* bottomPanel_{};
    mutable nec::NecSourceCache activeSourceCache_;
    model::AntennaModel currentModel_;
    model::ModelSetup currentSetup_;
    GeometrySettings geometrySettings_;
    WorkspaceDensity workspaceDensity_{WorkspaceDensity::Compact};
    double deckScaleToMeters_{1.0};
    bool updatingDeckUnitControl_{};
    QString currentFile_;
    QString solverBackendId_{QStringLiteral("nec2")};
    QString solverExecutablePath_;
    QHash<QString, QString> solverExecutablePaths_;
    int solverTimeoutSeconds_{120};
    SolverProcessRunner* solverRunner_{};
    QTimer* solverActivityTimer_{};
    QElapsedTimer solverElapsed_;
    QString solverActivityName_;
    QString solverActivityPhase_;
    QString currentSolverCommand_;
    QString currentRunDirectory_;
    QString latestStoredRunDirectory_;
    QString currentRunOutputPath_;
    QString displayedRunDirectory_;
    // Solver deck identities (Z0-free), so a Z0-only edit can refresh SWR instead of staling results.
    std::string pendingRunDeck_;
    std::string liveResultsDeck_;
    QString activeModelResultsDirectory_;
    QString activeModelResultsContext_;
    AnalysisRunStore runStore_;
    ResultReferenceController resultReferenceController_{runStore_};
    std::unique_ptr<ResultsController> resultsController_;
    std::optional<AnalysisRunRecord> currentRunRecord_;
    SolverRunPurpose currentRunPurpose_{SolverRunPurpose::Analysis};
    analysis::AverageGainEnvironment currentAverageGainEnvironment_{
        analysis::AverageGainEnvironment::FreeSpace};
    double currentAverageGainFrequencyMHz_{};
    double currentPeakSearchFrequencyMHz_{};
    // SWR reference requested by the optimization study whose baseline is running.
    double currentBaselineReferenceImpedanceOhms_{50.0};
    int geometryTabIndex_{};
    int geometry3DTabIndex_{};
    int structuredSourceTabIndex_{1};
    int validationOverviewTabIndex_{};
    int averageGainValidationTabIndex_{};
    int convergenceValidationTabIndex_{};
    std::size_t modelErrorCount_{};
    std::size_t modelWarningCount_{};
    bool modelChecked_{};
    bool hasNecModel_{};
    bool resultsAvailable_{};
    bool liveResultsStale_{};
    bool suppressSourceHistoryRecording_{};
    std::optional<SourceHistoryEntry> pendingSourceHistoryEntry_;
    std::vector<SourceHistoryEntry> sourceUndoHistory_;
    std::vector<SourceHistoryEntry> sourceRedoHistory_;
    std::optional<std::pair<int, int>> pendingTransmissionLineEndpoint_;
};

}
