#include "ui/MainWindow.h"

#include "ui/DisplayFormat.h"
#include "ui/DetachablePanel.h"
#include "ui/WorkspaceNavigator.h"
#include "ui/WorkbenchIcon.h"

#include "analysis/FrequencyComparison.h"
#include "analysis/HistoricalOverlayCompatibility.h"
#include "analysis/OptimizationBaseline.h"
#include "analysis/SolverCommand.h"
#include "analysis/NecOutputParser.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/NecCardCatalog.h"
#include "nec/DeckGeometryUnits.h"
#include "nec/NecModelConverter.h"
#include "nec/NecCardFieldEditor.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
#include "nec/NecSourceEditor.h"
#include "nec/NecSymbolEditor.h"
#include "nec/NecSymbolResolver.h"
#include "nec/NecWriter.h"
#include "ui/controlpanel/ControlPanel.h"
#include "ui/cards/WireCardEditor.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/analysis/AnalysisSetupEditor.h"
#include "ui/analysis/AnalysisRequestEditor.h"
#include "ui/analysis/AverageGainResultsView.h"
#include "ui/analysis/ConvergenceWorkspace.h"
#include "ui/analysis/ImpedanceResultsView.h"
#include "ui/analysis/QuickSweepDialog.h"
#include "ui/analysis/ResultWindowCoordinator.h"
#include "ui/analysis/RunReviewWindow.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/analysis/HistoricalOverlayControl.h"
#include "ui/editor/NecEditor.h"
#include "ui/editor/NecHighlighter.h"
#include "ui/geometry/EngineeringSpinBox.h"
#include "ui/geometry/AutoSegmentationDialog.h"
#include "ui/geometry/Geometry3DView.h"
#include "ui/geometry/GeometryView.h"
#include "ui/geometry/GeometrySettingsDialog.h"
#include "ui/geometry/WirePropertiesDialog.h"
#include "ui/optimization/OptimizationWorkspace.h"
#include "ui/model/ParameterEditor.h"
#include "ui/setup/SetupEditor.h"
#include "ui/setup/LoadNetworkEditor.h"
#include "ui/setup/ExcitationPropertiesDialog.h"
#include "ui/welcome/WelcomePage.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLinkButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QStringList>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMargins>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableView>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <iomanip>
#include <span>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace necwb::ui {
namespace {

constexpr QSize kResultWindowSize{520, 380};

constexpr auto ItemKindRole = Qt::UserRole;
constexpr auto WireTagRole = Qt::UserRole + 1;
constexpr auto SourceLineRole = Qt::UserRole + 2;

auto modelSegmentCount(const model::AntennaModel& model) -> int
{
    auto count = 0;
    for (const auto& wire : model.wires()) count += wire.segments;
    return count;
}

auto historicalOverlayStatusText(analysis::HistoricalOverlayStatus status) -> QString
{
    switch (status) {
    case analysis::HistoricalOverlayStatus::Compatible:
        return QObject::tr("Compatible");
    case analysis::HistoricalOverlayStatus::Partial:
        return QObject::tr("Partial");
    case analysis::HistoricalOverlayStatus::NoMatchingData:
        return QObject::tr("No matching data");
    }
    return {};
}

void invokeFocusedEditCommand(const char* command)
{
    for (auto* widget = QApplication::focusWidget(); widget != nullptr;
         widget = widget->parentWidget()) {
        if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            if (qstrcmp(command, "cut") == 0) lineEdit->cut();
            else if (qstrcmp(command, "copy") == 0) lineEdit->copy();
            else if (qstrcmp(command, "paste") == 0) lineEdit->paste();
            return;
        }
        if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
            if (qstrcmp(command, "cut") == 0) plainTextEdit->cut();
            else if (qstrcmp(command, "copy") == 0) plainTextEdit->copy();
            else if (qstrcmp(command, "paste") == 0) plainTextEdit->paste();
            return;
        }
        if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
            if (qstrcmp(command, "cut") == 0) textEdit->cut();
            else if (qstrcmp(command, "copy") == 0) textEdit->copy();
            else if (qstrcmp(command, "paste") == 0) textEdit->paste();
            return;
        }
    }
}

constexpr auto RunDirectoryRole = Qt::UserRole + 3;
constexpr auto RunContextRole = Qt::UserRole + 4;
constexpr auto CardMnemonicRole = Qt::UserRole + 5;
constexpr auto RunTypeRole = Qt::UserRole + 6;
constexpr auto RunIdRole = Qt::UserRole + 7;

auto workspaceDensityFromId(const QString& id) -> MainWindow::WorkspaceDensity
{
    if (id == QStringLiteral("spacious")) return MainWindow::WorkspaceDensity::Spacious;
    if (id == QStringLiteral("standard")) return MainWindow::WorkspaceDensity::Standard;
    return MainWindow::WorkspaceDensity::Compact;
}

auto workspaceDensityId(MainWindow::WorkspaceDensity density) -> QString
{
    switch (density) {
    case MainWindow::WorkspaceDensity::Compact: return QStringLiteral("compact");
    case MainWindow::WorkspaceDensity::Standard: return QStringLiteral("standard");
    case MainWindow::WorkspaceDensity::Spacious: return QStringLiteral("spacious");
    }
    return QStringLiteral("compact");
}

auto workspaceDensityScale(MainWindow::WorkspaceDensity density) -> qreal
{
    switch (density) {
    case MainWindow::WorkspaceDensity::Compact: return 1.0 / 3.0;
    case MainWindow::WorkspaceDensity::Standard: return 0.5;
    case MainWindow::WorkspaceDensity::Spacious: return 5.0 / 6.0;
    }
    return 1.0 / 3.0;
}

auto workspaceDensityFontSize(MainWindow::WorkspaceDensity density) -> int
{
    switch (density) {
    case MainWindow::WorkspaceDensity::Compact: return 9;
    case MainWindow::WorkspaceDensity::Standard: return 10;
    case MainWindow::WorkspaceDensity::Spacious: return 11;
    }
    return 9;
}

enum RunColumn {
    RunStartedColumn,
    RunModelColumn,
    RunNoteColumn,
    RunTypeColumn,
    RunResultsColumn,
    RunOutputSizeColumn,
    RunBackendColumn,
    RunStatusColumn,
    RunDurationColumn,
    RunFolderColumn,
    RunColumnCount
};

auto formatByteSize(qint64 bytes) -> QString
{
    if (bytes <= 0) return QObject::tr("—");
    constexpr auto kibibyte = 1024.0;
    constexpr auto mebibyte = kibibyte * 1024.0;
    if (bytes >= mebibyte) return QObject::tr("%1 MB").arg(formatDecimal(bytes / mebibyte));
    if (bytes >= kibibyte) return QObject::tr("%1 KB").arg(formatDecimal(bytes / kibibyte));
    return QObject::tr("%1 B").arg(bytes);
}

auto runResultsText(const AnalysisRunRecord& record) -> QString
{
    if (record.runType == QStringLiteral("optimization-session"))
        return record.summary.isEmpty()
            ? QObject::tr("%1 candidates").arg(record.candidateCount) : record.summary;
    if (record.runType == QStringLiteral("convergence-session"))
        return record.summary.isEmpty()
            ? QObject::tr("%1 levels").arg(record.candidateCount) : record.summary;
    if (record.runType == QStringLiteral("average-gain-test"))
        return record.summary.isEmpty() ? QObject::tr("AGT") : record.summary;
    QStringList types;
    if (record.hasImpedance) types.append(QObject::tr("Z"));
    if (record.hasCurrents) types.append(QObject::tr("I"));
    if (record.hasRadiation) types.append(QObject::tr("RP"));
    if (types.empty()) return record.outputBytes > 0 ? QObject::tr("Not indexed") : QObject::tr("—");
    return record.frequencyCount > 0
        ? QObject::tr("%1 freq · %2").arg(record.frequencyCount).arg(types.join(QStringLiteral(" · ")))
        : types.join(QStringLiteral(" · "));
}

void setRunResultMetadata(AnalysisRunRecord& record, const analysis::AnalysisResult& result,
    qint64 outputBytes)
{
    record.outputBytes = outputBytes;
    record.hasImpedance = !result.feedpoints.empty();
    record.hasCurrents = !result.currents.empty();
    record.hasRadiation = !result.radiation.empty();
    const auto coverage = analysis::optimizationResultCoverage(result);
    record.referenceImpedanceOhms = coverage.referenceImpedanceOhms;
    record.impedanceFrequenciesMHz = coverage.impedanceFrequenciesMHz;
    record.radiationCoverage = coverage.radiation;
    std::vector<double> frequencies;
    const auto addFrequency = [&frequencies](double frequencyMHz) {
        if (std::ranges::find_if(frequencies, [frequencyMHz](double existing) {
                return nearlyEqual(existing, frequencyMHz);
            }) == frequencies.end()) frequencies.push_back(frequencyMHz);
    };
    for (const auto& value : result.feedpoints) addFrequency(value.frequencyMHz);
    for (const auto& value : result.currents) addFrequency(value.frequencyMHz);
    for (const auto& value : result.radiation) addFrequency(value.frequencyMHz);
    record.frequencyCount = static_cast<int>(frequencies.size());
}

auto runContext(const AnalysisRunRecord& record) -> QString
{
    const auto modelName = record.sourceFile.isEmpty()
        ? QObject::tr("Archived model.nec")
        : QFileInfo(record.sourceFile).fileName();
    const auto started = record.started.isValid()
        ? record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : record.id;
    const auto backend = record.backend.isEmpty() ? QObject::tr("Unknown backend") : record.backend;
    const auto purpose = record.runType == QStringLiteral("quick-sweep")
        ? record.summary.isEmpty() ? QObject::tr(" · Quick Sweep")
                                   : QObject::tr(" · Quick Sweep: %1").arg(record.summary)
        : QString{};
    return QObject::tr("Model: %1 · Run: %2%3 · Backend: %4")
        .arg(modelName, started, purpose, backend);
}

auto averageGainEnvironmentId(analysis::AverageGainEnvironment environment) -> QString
{
    return environment == analysis::AverageGainEnvironment::PerfectGround
        ? QStringLiteral("perfect-ground") : QStringLiteral("free-space");
}

auto averageGainClassificationName(analysis::AverageGainClassification classification) -> QString
{
    switch (classification) {
    case analysis::AverageGainClassification::Pass: return QObject::tr("Pass");
    case analysis::AverageGainClassification::Usable: return QObject::tr("Usable");
    case analysis::AverageGainClassification::Caution: return QObject::tr("Caution");
    case analysis::AverageGainClassification::Questionable: return QObject::tr("Questionable");
    }
    return QObject::tr("Unknown");
}

auto writeAverageGainMetadata(const QString& directory, double frequencyMHz,
    analysis::AverageGainEnvironment environment,
    const std::optional<analysis::AverageGainAssessment>& assessment = std::nullopt,
    const std::optional<double>& solidAnglePi = std::nullopt) -> bool
{
    QJsonObject metadata{{QStringLiteral("version"), 1},
        {QStringLiteral("frequencyMHz"), frequencyMHz},
        {QStringLiteral("environment"), averageGainEnvironmentId(environment)},
        {QStringLiteral("expectedGain"),
            environment == analysis::AverageGainEnvironment::PerfectGround ? 2.0 : 1.0}};
    if (assessment) {
        metadata.insert(QStringLiteral("averagePowerGain"), assessment->averagePowerGain);
        metadata.insert(QStringLiteral("normalizedGain"), assessment->normalizedGain);
        metadata.insert(QStringLiteral("gainAdjustmentDb"), assessment->gainAdjustmentDb);
        metadata.insert(QStringLiteral("classification"),
            averageGainClassificationName(assessment->classification));
    }
    if (solidAnglePi) metadata.insert(QStringLiteral("solidAnglePi"), *solidAnglePi);
    QFile file(QDir(directory).filePath(QStringLiteral("agt.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const auto contents = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    return file.write(contents) == contents.size();
}

auto findProjectItem(QTreeWidget* tree, const QString& kind, int role,
    const QVariant& value) -> QTreeWidgetItem*
{
    QTreeWidgetItemIterator iterator(tree);
    while (*iterator != nullptr) {
        auto* item = *iterator;
        if (item->data(0, ItemKindRole).toString() == kind
            && item->data(0, role) == value) return item;
        ++iterator;
    }
    return nullptr;
}

auto createGeometryPane(const QString& title, GeometryView* view, QWidget* parent) -> QWidget*
{
    auto* pane = new QWidget(parent);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(pane);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(7, 3, 5, 3);
    auto* label = new QLabel(title, header);
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    auto* fitButton = new QToolButton(header);
    fitButton->setText(QObject::tr("Fit"));
    fitButton->setAutoRaise(true);
    QObject::connect(fitButton, &QToolButton::clicked, view, &GeometryView::fitToView);
    headerLayout->addWidget(label);
    headerLayout->addStretch();
    headerLayout->addWidget(fitButton);

    layout->addWidget(header);
    layout->addWidget(view, 1);
    return pane;
}

auto scrollableEditor(QWidget* editor, QWidget* parent) -> QScrollArea*
{
    auto* scroll = new QScrollArea(parent);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(editor);
    return scroll;
}

void preserveControlHeights(QWidget* root)
{
    const auto preserve = [](QWidget* control) {
        control->setMinimumHeight(std::max(control->minimumHeight(), control->sizeHint().height()));
        auto policy = control->sizePolicy();
        policy.setVerticalPolicy(QSizePolicy::Fixed);
        control->setSizePolicy(policy);
    };
    for (auto* control : root->findChildren<QComboBox*>()) preserve(control);
    for (auto* control : root->findChildren<QAbstractSpinBox*>()) preserve(control);
    for (auto* control : root->findChildren<QLineEdit*>()) preserve(control);
    for (auto* control : root->findChildren<QPushButton*>()) preserve(control);
}

}

MainWindow::MainWindow()
{
    setWindowTitle(tr("Untitled[*] — NEC Workbench"));
    setWindowModified(false);

    solverRunner_ = new SolverProcessRunner(this);
    createActions();
    createWorkspace();
    resultWindowCoordinator_ = std::make_unique<ResultWindowCoordinator>(
        impedancePanel_, currentsPanel_, radiationPanel_);
    impedancePanel_->setVisibilityChangedCallback([this](bool visible) {
        controlPanel_->setResultWindowVisible(ControlPanel::Workspace::Impedance, visible);
    });
    currentsPanel_->setVisibilityChangedCallback([this](bool visible) {
        controlPanel_->setResultWindowVisible(ControlPanel::Workspace::Currents, visible);
    });
    radiationPanel_->setVisibilityChangedCallback([this](bool visible) {
        controlPanel_->setResultWindowVisible(ControlPanel::Workspace::Radiation, visible);
    });
    geometryPanel_->setVisibilityChangedCallback([this](bool visible) {
        controlPanel_->setResultWindowVisible(ControlPanel::Workspace::GeometryEditor, visible);
    });
    createDocks();
    createMenusAndToolbar();
    // The main window only shows the dashboard (every module opens in its own window), and
    // the dashboard does not stretch, so the window is fixed to its content instead of
    // letting a short window squeeze sections over each other.
    layout()->setSizeConstraint(QLayout::SetFixedSize);
    statusBar()->setSizeGripEnabled(false);
    connect(dashboardStack_, &QStackedWidget::currentChanged,
        this, [this] { fitWindowToDashboard(); });
    fitWindowToDashboard();
    qApp->installEventFilter(this);
    setWorkspaceDensity(workspaceDensityFromId(QSettings{}.value(
        QStringLiteral("appearance/workspaceDensity"), QStringLiteral("compact")).toString()));

    modelFileStatus_ = new QLabel(this);
    modelFileStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    modelFileStatus_->setMinimumWidth(0);
    modelFileStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addPermanentWidget(modelFileStatus_, 1);
    checkStatus_ = new QLabel(tr("No model loaded"), this);
    statusBar()->addPermanentWidget(checkStatus_);
    connect(solverRunner_, &SolverProcessRunner::started, this, [this] {
        solverActivityPhase_ = tr("Solving");
        updateSolverActivity();
        setCurrentRunStatus(tr("Running"));
        appendSolverOutput(tr("Solver started.\n"));
    });
    connect(solverRunner_, &SolverProcessRunner::outputReady, this,
        [this](const QByteArray& output) { appendSolverOutput(QString::fromLocal8Bit(output)); });
    connect(solverRunner_, &SolverProcessRunner::finished, this,
        [this](SolverProcessResult result) { finishAnalysis(std::move(result)); });

    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        activeSourceCache_.invalidate();
        clearCheckResults();
    });
    connect(editor_->document(), &QTextDocument::modificationChanged, this, [this](bool modified) {
        saveAction_->setEnabled(modified);
        setWindowModified(modified);
        sourceWorkspace_->setTabText(0, modified ? tr("Raw Source *") : tr("Raw Source"));
        if (controlPanel_ != nullptr)
            controlPanel_->setDocumentState(currentFile_.isEmpty()
                    ? QString{} : QFileInfo(currentFile_).fileName(), modified);
    });
    connect(editor_->document(), &QTextDocument::undoAvailable, this, [this] { updateUndoActions(); });
    connect(editor_->document(), &QTextDocument::redoAvailable, this, [this] { updateUndoActions(); });
    connect(editor_->document(), &QTextDocument::undoCommandAdded,
        this, [this] { recordSourceUndoCommand(); });
    connect(workspace_, &QTabWidget::currentChanged, this, [this] { updateUndoActions(); });
    previousModelWorkspaceIndex_ = modelWorkspace_->currentIndex();
    previousAnalysisWorkspaceIndex_ = analysisWorkspace_->currentIndex();
    connect(modelWorkspace_, &QTabWidget::currentChanged, this, [this](int index) {
        if (!restoringWorkspaceTab_ && previousModelWorkspaceIndex_ >= 0
            && index != previousModelWorkspaceIndex_
            && !confirmPendingEdits(modelWorkspace_->widget(previousModelWorkspaceIndex_))) {
            restoringWorkspaceTab_ = true;
            modelWorkspace_->setCurrentIndex(previousModelWorkspaceIndex_);
            restoringWorkspaceTab_ = false;
            return;
        }
        previousModelWorkspaceIndex_ = index;
        updateUndoActions();
    });
    connect(analysisWorkspace_, &QTabWidget::currentChanged, this, [this](int index) {
        if (!restoringWorkspaceTab_ && previousAnalysisWorkspaceIndex_ >= 0
            && index != previousAnalysisWorkspaceIndex_
            && !confirmPendingEdits(analysisWorkspace_->widget(previousAnalysisWorkspaceIndex_))) {
            restoringWorkspaceTab_ = true;
            analysisWorkspace_->setCurrentIndex(previousAnalysisWorkspaceIndex_);
            restoringWorkspaceTab_ = false;
            return;
        }
        previousAnalysisWorkspaceIndex_ = index;
        updateUndoActions();
    });
    connect(sourceWorkspace_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == structuredSourceTabIndex_ && !modelChecked_) checkModel();
        updateUndoActions();
    });
    connect(moduleStack_, &QStackedWidget::currentChanged, this,
        [this] { updateUndoActions(); });

    restoreWorkspaceLayout();
    updateUndoActions();
}

auto MainWindow::eventFilter(QObject* watched, QEvent* event) -> bool
{
    // Dashboard content changes size (results, stale markers, density), so refit the
    // window whenever the visible dashboard page asks for a new layout.
    if (event->type() == QEvent::LayoutRequest && dashboardStack_ != nullptr
        && watched == dashboardStack_->currentWidget())
        QTimer::singleShot(0, this, [this] { fitWindowToDashboard(); });
    if (event->type() == QEvent::Show) {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (dialog != nullptr && dialog->isWindow()
            && dialog->property("workspaceDensityId").toString()
                != workspaceDensityId(workspaceDensity_)) {
            applyWorkspaceDensity(dialog);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::~MainWindow()
{
    delete runReviewWindow_;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (solverRunner_->isRunning() || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning()) {
        const auto answer = QMessageBox::question(this, tr("Solver Running"),
            tr("A solver task is still active. Cancel it and close NEC Workbench?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        solverRunner_->cancelAndWait();
        optimizationWorkspace_->cancelAndWait();
        convergenceWorkspace_->cancelAndWait();
    }
    if (!maybeSaveChanges()) {
        event->ignore();
        return;
    }
    saveWorkspaceLayout();
    event->accept();
}

void MainWindow::createActions()
{
    newAction_ = new QAction(tr("&New NEC Model"), this);
    newAction_->setObjectName(QStringLiteral("newModelAction"));
    newAction_->setIcon(workbenchIcon("new"));
    newAction_->setShortcut(QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, [this] { newModel(); });

    openAction_ = new QAction(tr("&Open NEC File…"), this);
    openAction_->setObjectName(QStringLiteral("openNecFileAction"));
    openAction_->setIcon(workbenchIcon("open"));
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, [this] { openFile(); });

    saveAction_ = new QAction(tr("&Save"), this);
    saveAction_->setIcon(workbenchIcon("save"));
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setEnabled(false);
    connect(saveAction_, &QAction::triggered, this, [this] { saveFile(); });

    saveAsAction_ = new QAction(tr("Save &As…"), this);
    saveAsAction_->setIcon(workbenchIcon("save"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this] { saveFileAs(); });

    exitAction_ = new QAction(tr("E&xit"), this);
    exitAction_->setObjectName(QStringLiteral("exitAction"));
    exitAction_->setShortcut(QKeySequence::Quit);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);

    cutAction_ = new QAction(tr("Cu&t"), this);
    cutAction_->setObjectName(QStringLiteral("cutAction"));
    cutAction_->setShortcut(QKeySequence::Cut);
    connect(cutAction_, &QAction::triggered, this,
        [] { invokeFocusedEditCommand("cut"); });
    copyAction_ = new QAction(tr("&Copy"), this);
    copyAction_->setObjectName(QStringLiteral("copyAction"));
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, this,
        [] { invokeFocusedEditCommand("copy"); });
    pasteAction_ = new QAction(tr("&Paste"), this);
    pasteAction_->setObjectName(QStringLiteral("pasteAction"));
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this,
        [] { invokeFocusedEditCommand("paste"); });

    checkAction_ = new QAction(tr("&Check Model"), this);
    checkAction_->setIcon(workbenchIcon("check"));
    checkAction_->setShortcut(QKeySequence(Qt::Key_F7));
    checkAction_->setStatusTip(tr("Check NEC cards and model geometry"));
    connect(checkAction_, &QAction::triggered, this, [this] { checkModel(true); });

    runAction_ = new QAction(tr("&Run Analysis"), this);
    runAction_->setIcon(workbenchIcon("run"));
    runAction_->setEnabled(false);
    runAction_->setStatusTip(tr("Run the checked model with the selected NEC engine"));
    connect(runAction_, &QAction::triggered, this, [this] { startAnalysis(); });

    quickSweepAction_ = new QAction(tr("Quick Frequency &Sweep…"), this);
    quickSweepAction_->setObjectName(QStringLiteral("quickFrequencySweepAction"));
    quickSweepAction_->setIcon(workbenchIcon("quick-sweep"));
    quickSweepAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F6));
    quickSweepAction_->setEnabled(false);
    quickSweepAction_->setStatusTip(tr(
        "Run a temporary frequency sweep without changing the model's FR card"));
    connect(quickSweepAction_, &QAction::triggered,
        this, [this] { startQuickFrequencySweep(); });

    stopAction_ = new QAction(tr("&Stop"), this);
    stopAction_->setObjectName(QStringLiteral("stopAnalysisAction"));
    stopAction_->setIcon(workbenchIcon("stop"));
    stopAction_->setEnabled(false);
    stopAction_->setStatusTip(tr("Stop the active analysis or validation solver process"));
    connect(stopAction_, &QAction::triggered, this, [this] {
        if (solverRunner_->isRunning()) cancelAnalysis();
        else if (optimizationWorkspace_->isRunning()) optimizationWorkspace_->cancel();
        else if (convergenceWorkspace_->isRunning()) convergenceWorkspace_->cancel();
    });

    averageGainAction_ = new QAction(tr("Run &Average Gain Test…"), this);
    averageGainAction_->setIcon(workbenchIcon("validate"));
    averageGainAction_->setEnabled(false);
    averageGainAction_->setStatusTip(tr(
        "Run a single-frequency lossless Average Gain Test without modifying the source model"));
    connect(averageGainAction_, &QAction::triggered, this, [this] { startAverageGainTest(); });

    convergenceAction_ = new QAction(tr("Segmentation &Convergence…"), this);
    convergenceAction_->setObjectName(QStringLiteral("segmentationConvergenceAction"));
    convergenceAction_->setIcon(workbenchIcon("frequency-sweep"));
    convergenceAction_->setEnabled(false);
    convergenceAction_->setStatusTip(tr(
        "Open the multi-run segmentation convergence study window"));
    connect(convergenceAction_, &QAction::triggered, this, [this] { showConvergenceStudy(); });

    fitGeometryAction_ = new QAction(tr("&Fit Geometry"), this);
    fitGeometryAction_->setIcon(workbenchIcon("fit"));
    fitGeometryAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    fitGeometryAction_->setStatusTip(tr("Fit geometry in all orthographic views"));
    connect(fitGeometryAction_, &QAction::triggered, this, [this] { fitAllGeometryViews(); });

    snapGridAction_ = new QAction(tr("Snap Grid"), this);
    snapGridAction_->setCheckable(true);
    snapGridAction_->setChecked(true);
    snapGridAction_->setStatusTip(tr("Snap moved geometry to the fixed snap interval"));
    connect(snapGridAction_, &QAction::toggled, this, [this](bool enabled) {
        auto settings = geometrySettings_;
        settings.gridSnapping = enabled;
        applyGeometrySettings(settings);
    });

    snapEndpointsAction_ = new QAction(tr("Snap Endpoints"), this);
    snapEndpointsAction_->setCheckable(true);
    snapEndpointsAction_->setChecked(true);
    snapEndpointsAction_->setStatusTip(tr("Snap moved geometry to nearby wire endpoints"));
    connect(snapEndpointsAction_, &QAction::toggled, this, [this](bool enabled) {
        auto settings = geometrySettings_;
        settings.endpointSnapping = enabled;
        applyGeometrySettings(settings);
    });

    geometrySettingsAction_ = new QAction(tr("Geometry Settings…"), this);
    geometrySettingsAction_->setIcon(workbenchIcon("settings"));
    connect(geometrySettingsAction_, &QAction::triggered, this, [this] { showGeometrySettings(); });

    autoSegmentationAction_ = new QAction(tr("Automatic Segmentation…"), this);
    autoSegmentationAction_->setIcon(workbenchIcon("structured-cards"));
    autoSegmentationAction_->setStatusTip(tr("Preview wavelength-based wire segment counts"));
    connect(autoSegmentationAction_, &QAction::triggered, this, [this] { showAutoSegmentation(); });

    undoAction_ = new QAction(tr("&Undo"), this);
    undoAction_->setIcon(workbenchIcon("undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] { performUndo(); });

    redoAction_ = new QAction(tr("&Redo"), this);
    redoAction_->setIcon(workbenchIcon("redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] { performRedo(); });

    resetLayoutAction_ = new QAction(tr("&Reset Layout"), this);
    resetLayoutAction_->setObjectName(QStringLiteral("resetLayoutAction"));
    connect(resetLayoutAction_, &QAction::triggered, this,
        [this] { resetWorkspaceLayout(); });

    modelCheckWindowAction_ = new QAction(tr("Model Check &Report"), this);
    modelCheckWindowAction_->setObjectName(QStringLiteral("showModelCheckWindowAction"));
    connect(modelCheckWindowAction_, &QAction::triggered, this, [this] {
        if (modelCheckWindow_ == nullptr) return;
        modelCheckWindow_->show();
        modelCheckWindow_->raise();
        modelCheckWindow_->activateWindow();
    });
    runMonitorWindowAction_ = new QAction(tr("&Run Monitor"), this);
    runMonitorWindowAction_->setObjectName(QStringLiteral("showRunMonitorWindowAction"));
    connect(runMonitorWindowAction_, &QAction::triggered, this, [this] {
        showRunMonitor(true);
    });

    auto* densityGroup = new QActionGroup(this);
    densityGroup->setExclusive(true);
    compactDensityAction_ = densityGroup->addAction(tr("&Compact"));
    standardDensityAction_ = densityGroup->addAction(tr("&Standard"));
    spaciousDensityAction_ = densityGroup->addAction(tr("&Spacious"));
    compactDensityAction_->setCheckable(true);
    standardDensityAction_->setCheckable(true);
    spaciousDensityAction_->setCheckable(true);
    compactDensityAction_->setObjectName(QStringLiteral("compactWorkspaceDensityAction"));
    standardDensityAction_->setObjectName(QStringLiteral("standardWorkspaceDensityAction"));
    spaciousDensityAction_->setObjectName(QStringLiteral("spaciousWorkspaceDensityAction"));
    const auto savedDensity = workspaceDensityFromId(QSettings{}.value(
        QStringLiteral("appearance/workspaceDensity"), QStringLiteral("compact")).toString());
    switch (savedDensity) {
    case WorkspaceDensity::Compact: compactDensityAction_->setChecked(true); break;
    case WorkspaceDensity::Standard: standardDensityAction_->setChecked(true); break;
    case WorkspaceDensity::Spacious: spaciousDensityAction_->setChecked(true); break;
    }
    connect(compactDensityAction_, &QAction::triggered, this,
        [this] { setWorkspaceDensity(WorkspaceDensity::Compact); });
    connect(standardDensityAction_, &QAction::triggered, this,
        [this] { setWorkspaceDensity(WorkspaceDensity::Standard); });
    connect(spaciousDensityAction_, &QAction::triggered, this,
        [this] { setWorkspaceDensity(WorkspaceDensity::Spacious); });

    gettingStartedAction_ = new QAction(tr("&Getting Started"), this);
    gettingStartedAction_->setIcon(workbenchIcon("help"));
    gettingStartedAction_->setObjectName(QStringLiteral("gettingStartedAction"));
    connect(gettingStartedAction_, &QAction::triggered, this,
        [this] { showGettingStarted(); });
    userGuideAction_ = new QAction(tr("&User Guide"), this);
    userGuideAction_->setIcon(workbenchIcon("help"));
    userGuideAction_->setObjectName(QStringLiteral("userGuideAction"));
    connect(userGuideAction_, &QAction::triggered, this,
        [this] { openUserGuide(); });
    aboutAction_ = new QAction(tr("&About NEC Workbench"), this);
    aboutAction_->setIcon(workbenchIcon("help"));
    aboutAction_->setObjectName(QStringLiteral("aboutAction"));
    connect(aboutAction_, &QAction::triggered, this,
        [this] { showAboutDialog(); });

    auto* moduleGroup = new QActionGroup(this);
    moduleGroup->setExclusive(true);
    homeModuleAction_ = moduleGroup->addAction(tr("&Home"));
    modelModuleAction_ = moduleGroup->addAction(tr("&Model"));
    analysisModuleAction_ = moduleGroup->addAction(tr("&Setup"));
    resultsModuleAction_ = moduleGroup->addAction(tr("&Results"));
    optimizeModuleAction_ = moduleGroup->addAction(tr("&Optimize"));
    modelModuleAction_->setObjectName(QStringLiteral("modelModuleAction"));
    analysisModuleAction_->setObjectName(QStringLiteral("setupModuleAction"));
    optimizeModuleAction_->setObjectName(QStringLiteral("optimizeModuleAction"));
    modelModuleAction_->setIcon(workbenchIcon("geometry"));
    analysisModuleAction_->setIcon(workbenchIcon("settings"));
    resultsModuleAction_->setIcon(workbenchIcon("results"));
    optimizeModuleAction_->setIcon(workbenchIcon("optimize"));
    for (auto* action : moduleGroup->actions()) {
        action->setCheckable(true);
    }
    resultsModuleAction_->setCheckable(false);
    homeModuleAction_->setChecked(true);
    modelModuleAction_->setEnabled(false);
    analysisModuleAction_->setEnabled(false);
    resultsModuleAction_->setEnabled(true);
    optimizeModuleAction_->setEnabled(false);
    connect(homeModuleAction_, &QAction::triggered, this, [this] { showModule(homeModuleIndex_); });
    connect(modelModuleAction_, &QAction::triggered, this, [this] { showModule(modelModuleIndex_); });
    connect(analysisModuleAction_, &QAction::triggered, this, [this] { showModule(analysisModuleIndex_); });
    connect(resultsModuleAction_, &QAction::triggered, this, [this] {
        showPrimaryResultsWindow();
    });
    connect(optimizeModuleAction_, &QAction::triggered,
        this, [this] { showModule(optimizeModuleIndex_); });
}

void MainWindow::createWorkspace()
{
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    moduleStack_ = new QStackedWidget(central);
    moduleStack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    centralLayout->addWidget(moduleStack_, 1);
    setCentralWidget(central);

    modelWorkspace_ = new QTabWidget(moduleStack_);
    modelWorkspace_->setObjectName(QStringLiteral("modelWorkspace"));
    modelWorkspace_->setDocumentMode(true);
    sourceWorkspace_ = new QTabWidget(modelWorkspace_);
    sourceWorkspace_->setObjectName(QStringLiteral("necDeckWorkspace"));
    sourceWorkspace_->setDocumentMode(true);
    editor_ = new NecEditor(sourceWorkspace_);
    editor_->setObjectName(QStringLiteral("necSourceEditor"));
    new NecHighlighter(editor_->document());
    sourceWorkspace_->addTab(editor_, tr("Raw Source"));

    dashboardStack_ = new QStackedWidget(moduleStack_);
    welcomePage_ = new WelcomePage(newAction_, openAction_,
        [this](const QString& path) { openFileAtPath(path); },
        [this] { openExample(); }, [this] { clearRecentFiles(); }, dashboardStack_);
    dashboardStack_->addWidget(welcomePage_);
    controlPanel_ = new ControlPanel(dashboardStack_);
    controlPanel_->setFileActions(openAction_, saveAction_);
    refreshRecentFileViews(recentFiles());
    controlPanel_->setQuickActions(
        checkAction_, runAction_, quickSweepAction_, stopAction_);
    controlPanel_->setAverageGainAction(averageGainAction_);
    controlPanel_->setConvergenceAction(convergenceAction_);
    controlPanel_->setOpenWorkspaceCallback([this](ControlPanel::Workspace destination) {
        switch (destination) {
        case ControlPanel::Workspace::Setup:
            showModule(analysisModuleIndex_);
            break;
        case ControlPanel::Workspace::GeometryEditor:
            showModule(geometryModuleIndex_);
            break;
        case ControlPanel::Workspace::Impedance:
            resultWindowCoordinator_->showImpedance();
            break;
        case ControlPanel::Workspace::Currents:
            resultWindowCoordinator_->showCurrents();
            break;
        case ControlPanel::Workspace::Radiation:
            resultWindowCoordinator_->showRadiation();
            break;
        case ControlPanel::Workspace::Runs:
            showRunHistory();
            break;
        case ControlPanel::Workspace::Optimize:
            showModule(optimizeModuleIndex_);
            break;
        }
    });
    controlPanel_->setOpenEditorCallback([this](ControlPanel::Editor destination) {
        switch (destination) {
        case ControlPanel::Editor::Frequency: showEditor(EditorDestination::Frequency); break;
        case ControlPanel::Editor::Wires: showEditor(EditorDestination::Geometry); break;
        case ControlPanel::Editor::Sources: showEditor(EditorDestination::Sources); break;
        case ControlPanel::Editor::Loads:
        case ControlPanel::Editor::TransmissionLines:
            showEditor(EditorDestination::Loads);
            break;
        case ControlPanel::Editor::Ground: showEditor(EditorDestination::Environment); break;
        case ControlPanel::Editor::Variables: showEditor(EditorDestination::Parameters); break;
        case ControlPanel::Editor::Requests: showEditor(EditorDestination::Requests); break;
        case ControlPanel::Editor::Deck: showEditor(EditorDestination::RawSource); break;
        }
    });
    dashboardStack_->addWidget(controlPanel_);
    QTimer::singleShot(0, this, [this] { refreshDashboardRuns(); });
    homeModuleIndex_ = moduleStack_->addWidget(dashboardStack_);

    auto* structuredContainer = new QWidget(sourceWorkspace_);
    auto* structuredLayout = new QVBoxLayout(structuredContainer);
    structuredLayout->setContentsMargins(6, 6, 6, 6);
    structuredLayout->setSpacing(6);
    auto* deckUnitBar = new QHBoxLayout;
    deckUnitBar->addWidget(new QLabel(tr("NEC deck geometry units:"), structuredContainer));
    deckUnitControl_ = new QComboBox(structuredContainer);
    deckUnitControl_->setObjectName(QStringLiteral("deckGeometryUnitControl"));
    deckUnitControl_->addItem(tr("Meters"), static_cast<int>(model::LengthUnit::Meter));
    deckUnitControl_->addItem(tr("Feet"), static_cast<int>(model::LengthUnit::Foot));
    deckUnitControl_->addItem(tr("Inches"), static_cast<int>(model::LengthUnit::Inch));
    deckUnitControl_->addItem(tr("Centimeters"), static_cast<int>(model::LengthUnit::Centimeter));
    deckUnitControl_->addItem(tr("Millimeters"), static_cast<int>(model::LengthUnit::Millimeter));
    deckUnitControl_->addItem(tr("Custom / mixed GS scale"), -1);
    deckScaleLabel_ = new QLabel(structuredContainer);
    deckUnitBar->addWidget(deckUnitControl_);
    deckUnitBar->addWidget(deckScaleLabel_);
    deckUnitBar->addStretch();
    structuredLayout->addLayout(deckUnitBar);
    structuredParameterizationHint_ = new QLabel(tr(
        "Double-click to edit · Right-click a numeric cell to parameterize it or link an existing SY symbol."),
        structuredContainer);
    structuredParameterizationHint_->setObjectName(
        QStringLiteral("structuredCardParameterizationHint"));
    structuredParameterizationHint_->setWordWrap(true);
    structuredLayout->addWidget(structuredParameterizationHint_);
    auto* structuredPage = new QTabWidget(structuredContainer);
    structuredLayout->addWidget(structuredPage, 1);
    structuredPage->setDocumentMode(true);
    wireCardEditor_ = new WireCardEditor(structuredPage);
    structuredPage->addTab(wireCardEditor_, tr("Wires (GW)"));
    structuredCardEditor_ = new StructuredCardEditor(structuredPage);
    structuredPage->addTab(structuredCardEditor_, tr("Other Supported Cards"));
    sourceWorkspace_->addTab(structuredContainer, tr("Structured Cards"));
    connect(deckUnitControl_, &QComboBox::currentIndexChanged, this, [this] {
        if (updatingDeckUnitControl_ || deckUnitControl_->currentData().toInt() < 0) return;
        changeDeckLengthUnit(
            static_cast<model::LengthUnit>(deckUnitControl_->currentData().toInt()));
    });
    connect(wireCardEditor_, &WireCardEditor::wireSelected, this,
        [this](int tag) {
            selectWireInProject(tag);
            if (const auto* wire = currentModel_.wireByTag(tag)) editor_->goToLine(wire->sourceLine);
        });
    connect(wireCardEditor_, &WireCardEditor::wireEdited, this,
        [this](model::Wire original, model::Wire updated) { editWire(original, updated); });
    connect(wireCardEditor_, &WireCardEditor::addWireRequested, this,
        [this] { addWire({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}); });
    connect(wireCardEditor_, &WireCardEditor::duplicateWireRequested, this,
        [this](int tag) { duplicateWire(tag); });
    connect(wireCardEditor_, &WireCardEditor::deleteWireRequested, this,
        [this](int tag) { deleteWire(tag); });
    connect(wireCardEditor_, &WireCardEditor::fieldParameterizationRequested, this,
        [this](std::size_t sourceLine, std::size_t fieldIndex, const QString& fieldLabel) {
            makeFieldOptimizable(sourceLine, fieldIndex, fieldLabel);
        });
    connect(wireCardEditor_, &WireCardEditor::fieldDetachmentRequested, this,
        [this](std::size_t sourceLine, std::size_t fieldIndex, const QString& fieldLabel) {
            detachFieldParameter(sourceLine, fieldIndex, fieldLabel);
        });
    connect(structuredCardEditor_, &StructuredCardEditor::cardSelected, this,
        [this](std::size_t sourceLine) { editor_->goToLine(sourceLine); });
    connect(structuredCardEditor_, &StructuredCardEditor::cardEdited, this,
        [this](std::size_t sourceLine, const QString& cardText) {
            editStructuredCard(sourceLine, cardText);
        });
    connect(structuredCardEditor_, &StructuredCardEditor::cardAddRequested, this,
        [this](const QString& cardText) { addStructuredCard(cardText); });
    connect(structuredCardEditor_, &StructuredCardEditor::cardDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteStructuredCard(sourceLine); });
    connect(structuredCardEditor_, &StructuredCardEditor::fieldParameterizationRequested, this,
        [this](std::size_t sourceLine, std::size_t fieldIndex, const QString& fieldLabel) {
            makeFieldOptimizable(sourceLine, fieldIndex, fieldLabel);
        });
    connect(structuredCardEditor_, &StructuredCardEditor::fieldDetachmentRequested, this,
        [this](std::size_t sourceLine, std::size_t fieldIndex, const QString& fieldLabel) {
            detachFieldParameter(sourceLine, fieldIndex, fieldLabel);
        });

    workspace_ = new QTabWidget(moduleStack_);
    workspace_->setObjectName(QStringLiteral("geometryWorkspace"));
    workspace_->setDocumentMode(true);
    workspace_->setMovable(true);

    auto* geometryPage = new QWidget(workspace_);
    auto* geometryLayout = new QVBoxLayout(geometryPage);
    geometryLayout->setContentsMargins(0, 0, 0, 0);
    geometryLayout->setSpacing(0);
    auto* geometryToolbar = new QToolBar(tr("Geometry"), geometryPage);
    geometryToolbar->setIconSize({16, 16});
    geometryToolbar->addWidget(new QLabel(tr("Orthographic Views"), geometryToolbar));
    geometryToolbar->addSeparator();
    geometryToolbar->addAction(fitGeometryAction_);
    auto* horizontalSplitter = new QSplitter(Qt::Horizontal, geometryPage);
    auto* verticalSplitter = new QSplitter(Qt::Vertical, horizontalSplitter);
    xyView_ = new GeometryView(geometry::ProjectionPlane::XY, horizontalSplitter);
    xzView_ = new GeometryView(geometry::ProjectionPlane::XZ, verticalSplitter);
    yzView_ = new GeometryView(geometry::ProjectionPlane::YZ, verticalSplitter);
    geometryToolbar->addSeparator();
    geometryToolbar->addAction(snapGridAction_);
    geometryToolbar->addAction(snapEndpointsAction_);
    geometryToolbar->addSeparator();
    geometryToolbar->addWidget(new QLabel(tr("Units:"), geometryToolbar));
    lengthUnitControl_ = new QComboBox(geometryToolbar);
    lengthUnitControl_->addItem(tr("Meters"), static_cast<int>(model::LengthUnit::Meter));
    lengthUnitControl_->addItem(tr("Centimeters"), static_cast<int>(model::LengthUnit::Centimeter));
    lengthUnitControl_->addItem(tr("Millimeters"), static_cast<int>(model::LengthUnit::Millimeter));
    lengthUnitControl_->addItem(tr("Inches"), static_cast<int>(model::LengthUnit::Inch));
    lengthUnitControl_->addItem(tr("Feet"), static_cast<int>(model::LengthUnit::Foot));
    geometryToolbar->addWidget(lengthUnitControl_);
    geometryToolbar->addWidget(new QLabel(tr("Snap:"), geometryToolbar));
    snapSpacingControl_ = new EngineeringSpinBox(geometryToolbar);
    snapSpacingControl_->setDecimals(DisplayDecimalPlaces);
    snapSpacingControl_->setRange(0.000001, 1.0e9);
    snapSpacingControl_->setKeyboardTracking(false);
    snapSpacingControl_->setValue(0.1);
    snapSpacingControl_->setToolTip(tr("Snap interval in the active display unit; unit-change behavior is set in Geometry Settings"));
    geometryToolbar->addWidget(snapSpacingControl_);
    connect(lengthUnitControl_, &QComboBox::currentIndexChanged, this, [this] {
        setDisplayLengthUnit(static_cast<model::LengthUnit>(lengthUnitControl_->currentData().toInt()));
    });
    connect(snapSpacingControl_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        setSnapSpacing(model::toMeters(value, geometrySettings_.lengthUnit));
    });
    geometryToolbar->addAction(geometrySettingsAction_);
    applyGeometrySettings(geometrySettings_);
    horizontalSplitter->addWidget(createGeometryPane(tr("XY Plane"), xyView_, horizontalSplitter));
    verticalSplitter->addWidget(createGeometryPane(tr("XZ Plane"), xzView_, verticalSplitter));
    verticalSplitter->addWidget(createGeometryPane(tr("YZ Plane"), yzView_, verticalSplitter));
    horizontalSplitter->addWidget(verticalSplitter);
    horizontalSplitter->setStretchFactor(0, 3);
    horizontalSplitter->setStretchFactor(1, 2);
    verticalSplitter->setStretchFactor(0, 1);
    verticalSplitter->setStretchFactor(1, 1);
    geometryLayout->addWidget(geometryToolbar);
    geometryLayout->addWidget(horizontalSplitter, 1);
    geometryTabIndex_ = workspace_->addTab(geometryPage, tr("2D Geometry"));
    const auto connectAttachments = [this]<typename View>(View* view) {
        connect(view, &View::wireSelected, this,
            [this](int tag) { selectWireInProject(tag); });
        connect(view, &View::wirePropertiesRequested, this,
            [this](int tag) { showWireProperties(tag); });
        connect(view, &View::excitationSelected, this,
            [this](std::size_t line) { selectExcitation(line); });
        connect(view, &View::addExcitationRequested, this,
            [this](int tag, int segment) { addExcitationAt(tag, segment); });
        connect(view, &View::editExcitationRequested, this,
            [this](std::size_t line) { showExcitationEditor(line); });
        connect(view, &View::openExcitationSetupRequested, this,
            [this](std::size_t line) { showExcitationInSetup(line); });
        connect(view, &View::deleteExcitationRequested, this,
            [this](std::size_t line) { deleteExcitation(line); });
        connect(view, &View::loadSelected, this,
            [this](std::size_t line) { selectLoad(line); });
        connect(view, &View::transmissionLineSelected, this,
            [this](std::size_t line) { selectTransmissionLine(line); });
        connect(view, &View::addLoadRequested, this,
            [this](int tag, int segment) { addLoadAt(tag, segment); });
        connect(view, &View::transmissionLineEndpointRequested, this,
            [this](int tag, int segment) { chooseTransmissionLineEndpoint(tag, segment); });
        connect(view, &View::cancelTransmissionLineRequested, this,
            [this] { setPendingTransmissionLineEndpoint(std::nullopt); });
        connect(view, &View::editLoadRequested, this,
            [this](std::size_t line) { showLoadInEditor(line); });
        connect(view, &View::editTransmissionLineRequested, this,
            [this](std::size_t line) { showTransmissionLineInEditor(line); });
        connect(view, &View::deleteLoadRequested, this,
            [this](std::size_t line) { deleteSetupCard(tr("Delete load"), line); });
        connect(view, &View::deleteTransmissionLineRequested, this,
            [this](std::size_t line) { deleteSetupCard(tr("Delete transmission line"), line); });
    };
    const auto connectEndpointEditing = [this](GeometryView* view) {
        connect(view, &GeometryView::endpointPreviewed, this,
            [this](int tag, model::WireEndpoint endpoint, model::Point3D position) {
                previewEndpointMove(tag, endpoint, position);
            });
        connect(view, &GeometryView::endpointMoveFinished, this,
            [this](int tag, model::WireEndpoint endpoint, model::Point3D original, model::Point3D updated) {
                commitEndpointMove(tag, endpoint, original, updated);
            });
        connect(view, &GeometryView::wirePreviewed, this,
            [this](int tag, model::Point3D start, model::Point3D end) {
                previewWireMove(tag, start, end);
            });
        connect(view, &GeometryView::wireMoveFinished, this,
            [this](int tag, model::Point3D originalStart, model::Point3D originalEnd,
                model::Point3D updatedStart, model::Point3D updatedEnd) {
                commitWireMove(tag, originalStart, originalEnd, updatedStart, updatedEnd);
            });
        connect(view, &GeometryView::addWireRequested, this,
            [this](model::Point3D start, model::Point3D end) { addWire(start, end); });
        connect(view, &GeometryView::splitWireRequested, this,
            [this](int tag, model::Point3D position) { splitWire(tag, position); });
        connect(view, &GeometryView::deleteWireRequested, this,
            [this](int tag) { deleteWire(tag); });
    };
    connectAttachments(xyView_);
    connectAttachments(xzView_);
    connectAttachments(yzView_);
    connectEndpointEditing(xyView_);
    connectEndpointEditing(xzView_);
    connectEndpointEditing(yzView_);
    auto* geometry3DPage = new QWidget(workspace_);
    auto* geometry3DLayout = new QVBoxLayout(geometry3DPage);
    geometry3DLayout->setContentsMargins(0, 0, 0, 0);
    geometry3DLayout->setSpacing(0);
    auto* geometry3DToolbar = new QToolBar(tr("3D Geometry"), geometry3DPage);
    geometry3DToolbar->addWidget(new QLabel(tr("Interactive Model View"), geometry3DToolbar));
    geometry3DToolbar->addSeparator();
    auto* fit3DButton = new QToolButton(geometry3DToolbar);
    fit3DButton->setText(tr("Fit"));
    auto* isometricButton = new QToolButton(geometry3DToolbar);
    isometricButton->setText(tr("Isometric"));
    geometry3DToolbar->addWidget(fit3DButton);
    geometry3DToolbar->addWidget(isometricButton);
    geometry3DToolbar->addSeparator();
    geometry3DToolbar->addWidget(new QLabel(
        tr("Left-drag orbit  •  Shift/middle-drag pan  •  Wheel zoom"), geometry3DToolbar));
    geometry3DView_ = new Geometry3DView(geometry3DPage);
    geometry3DLayout->addWidget(geometry3DToolbar);
    geometry3DLayout->addWidget(geometry3DView_, 1);
    connect(fit3DButton, &QToolButton::clicked, geometry3DView_, &Geometry3DView::fitToView);
    connect(isometricButton, &QToolButton::clicked, geometry3DView_, &Geometry3DView::setIsometricView);
    connectAttachments(geometry3DView_);
    geometry3DTabIndex_ = workspace_->addTab(geometry3DPage, tr("3D Geometry"));
    workspace_->setTabIcon(geometryTabIndex_, workbenchIcon("geometry"));
    workspace_->setTabIcon(geometry3DTabIndex_, workbenchIcon("3d-view"));
    // Geometry and the deck live in separate windows so each keeps its own size: the deck
    // is usually small beside a run, while geometry work wants a large window.
    geometryPanel_ = new DetachablePanel(QStringLiteral("geometry-editor"),
        tr("Geometry Editor"), moduleStack_, {QStringLiteral("workspaceWindows"),
            QStringLiteral("geometryEditorWindow"), QSize{1000, 760}});
    geometryPanel_->setStatusText(tr("No model loaded"));
    geometryPanel_->setContent(workspace_);
    geometryModuleIndex_ = moduleStack_->addWidget(geometryPanel_);

    parameterEditor_ = new ParameterEditor(modelWorkspace_);
    connect(parameterEditor_, &ParameterEditor::parameterChanged, this,
        [this](std::size_t sourceLine, QString originalName, QString name, QString expression) {
            changeParameter(sourceLine, originalName, name, expression);
        });
    connect(parameterEditor_, &ParameterEditor::parameterDeleteRequested, this,
        [this](std::size_t sourceLine, QString name) { deleteParameter(sourceLine, name); });
    connect(parameterEditor_, &ParameterEditor::parameterSelected, this,
        [this](std::size_t sourceLine) { editor_->goToLine(sourceLine); });

    analysisWorkspace_ = new QTabWidget(moduleStack_);
    analysisWorkspace_->setObjectName(QStringLiteral("setupWorkspace"));
    analysisWorkspace_->setDocumentMode(true);

    setupEditor_ = new SetupEditor(analysisWorkspace_);
    connect(setupEditor_, &SetupEditor::frequencyChanged, this,
        [this](model::FrequencyDefinition frequency) { changeFrequency(frequency); });
    connect(setupEditor_, &SetupEditor::frequencyDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteFrequency(sourceLine); });
    connect(setupEditor_, &SetupEditor::groundChanged, this,
        [this](model::GroundDefinition ground) { changeGround(ground); });
    connect(setupEditor_, &SetupEditor::referenceImpedanceChanged, this,
        [this](model::ReferenceImpedanceDefinition reference) {
            changeReferenceImpedance(reference);
        });
    connect(setupEditor_, &SetupEditor::referenceImpedanceDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteReferenceImpedance(sourceLine); });
    connect(setupEditor_, &SetupEditor::excitationChanged, this,
        [this](model::Excitation excitation) { changeExcitation(excitation); });
    connect(setupEditor_, &SetupEditor::excitationDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteExcitation(sourceLine); });
    connect(setupEditor_, &SetupEditor::excitationSelected, this,
        [this](std::size_t sourceLine) { selectExcitation(sourceLine); });
    loadNetworkEditor_ = new LoadNetworkEditor(analysisWorkspace_);
    loadNetworkEditor_->setLengthUnit(geometrySettings_.lengthUnit);
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadChanged, this,
        [this](model::LoadDefinition load) {
            const auto isNew = load.sourceLine == 0;
            changeLoad(load);
            if (!isNew) return;
            const auto found = std::ranges::find_if(currentSetup_.loads.rbegin(),
                currentSetup_.loads.rend(), [&load](const auto& candidate) {
                    return candidate.type == load.type && candidate.wireTag == load.wireTag
                        && candidate.firstSegment == load.firstSegment
                        && candidate.lastSegment == load.lastSegment;
                });
            if (found != currentSetup_.loads.rend()) selectLoad(found->sourceLine);
        });
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete load"), sourceLine); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineChanged, this,
        [this](model::TransmissionLineDefinition line) {
            const auto isNew = line.sourceLine == 0;
            changeTransmissionLine(line);
            if (!isNew) return;
            const auto found = std::ranges::find_if(currentSetup_.transmissionLines.rbegin(),
                currentSetup_.transmissionLines.rend(), [&line](const auto& candidate) {
                    return candidate.wireTag1 == line.wireTag1
                        && candidate.segment1 == line.segment1
                        && candidate.wireTag2 == line.wireTag2
                        && candidate.segment2 == line.segment2;
                });
            if (found != currentSetup_.transmissionLines.rend()) {
                selectTransmissionLine(found->sourceLine);
            }
        });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete transmission line"), sourceLine); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadSelected, this,
        [this](std::size_t sourceLine) { selectLoad(sourceLine); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineSelected, this,
        [this](std::size_t sourceLine) { selectTransmissionLine(sourceLine); });
    modelDeckWorkspaceIndex_ = modelWorkspace_->addTab(sourceWorkspace_, tr("NEC Deck"));
    modelParametersWorkspaceIndex_ = modelWorkspace_->addTab(parameterEditor_, tr("Parameters"));
    modelWorkspace_->setTabIcon(modelParametersWorkspaceIndex_, workbenchIcon("structured-cards"));
    modelWorkspace_->setTabIcon(modelDeckWorkspaceIndex_, workbenchIcon("nec-deck"));
    auto* modelNavigator = new WorkspaceNavigator(
        modelWorkspace_, QStringLiteral("model"), moduleStack_);
    // Keeps the former Model Editor's settings key so its saved size carries over.
    modelPanel_ = new DetachablePanel(QStringLiteral("model-editor"),
        tr("NEC Deck"), moduleStack_, {QStringLiteral("workspaceWindows"),
            QStringLiteral("modelEditorWindow")});
    modelPanel_->setStatusText(tr("No model loaded"));
    modelPanel_->setContent(modelNavigator);
    modelModuleIndex_ = moduleStack_->addWidget(modelPanel_);

    analysisSetupEditor_ = new AnalysisSetupEditor(analysisWorkspace_);
    connect(analysisSetupEditor_, &AnalysisSetupEditor::settingsChanged, this,
        [this](QString backendId, QString executablePath, int timeoutSeconds) {
            const auto backendChanged = solverBackendId_ != backendId;
            solverBackendId_ = std::move(backendId);
            solverExecutablePath_ = std::move(executablePath);
            solverExecutablePaths_.insert(solverBackendId_, solverExecutablePath_);
            solverTimeoutSeconds_ = timeoutSeconds;
            optimizationWorkspace_->setContext(editor_->toPlainText(), currentFile_,
                solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
                modelChecked_ && modelErrorCount_ == 0);
            convergenceWorkspace_->setContext(editor_->toPlainText(), currentFile_,
                solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
                modelChecked_ && modelErrorCount_ == 0);
            controlPanel_->setModel(currentModel_, currentSetup_, activeSource().document(),
                solverBackendId_, modelChecked_, modelErrorCount_, modelWarningCount_);
            runAction_->setStatusTip(solverExecutablePath_.isEmpty()
                    ? tr("Select a NEC engine executable under Setup → Solver")
                    : tr("Run the checked model with the selected NEC engine"));
            if (backendChanged && modelChecked_) checkModel();
            updateAnalysisReadiness();
        });
    connect(analysisSetupEditor_, &AnalysisSetupEditor::executablePathChanged, this,
        [this](const QString& backendId, const QString& executablePath) {
            if (backendId.isEmpty()) return;
            solverExecutablePaths_.insert(backendId, executablePath);
            QSettings{}.setValue(
                QStringLiteral("analysis/executables/%1").arg(backendId), executablePath);
        });
    analysisFrequencyTabIndex_ = analysisWorkspace_->addTab(
        setupEditor_->frequencyPage(), tr("Frequency"));
    modelSourcesWorkspaceIndex_ = analysisWorkspace_->addTab(
        setupEditor_->sourcesPage(), tr("Sources"));
    loadNetworkTabIndex_ = analysisWorkspace_->addTab(
        loadNetworkEditor_, tr("Loads && Transmission Lines"));
    modelEnvironmentWorkspaceIndex_ = analysisWorkspace_->addTab(
        setupEditor_->environmentPage(), tr("Environment"));
    analysisRequestEditor_ = new AnalysisRequestEditor(analysisWorkspace_);
    connect(analysisRequestEditor_, &AnalysisRequestEditor::executionChanged, this,
        [this](bool enabled, model::ExecutionRequest execution) {
            changeExecutionRequest(enabled, execution);
        });
    connect(analysisRequestEditor_, &AnalysisRequestEditor::patternChanged, this,
        [this](model::RadiationPatternRequest pattern) {
            const auto isNew = pattern.sourceLine == 0;
            changeRadiationPattern(pattern);
            if (!isNew) return;
            const auto found = std::ranges::find_if(currentSetup_.radiationPatterns.rbegin(),
                currentSetup_.radiationPatterns.rend(), [&pattern](const auto& candidate) {
                    return candidate.thetaCount == pattern.thetaCount
                        && candidate.phiCount == pattern.phiCount
                        && candidate.thetaStart == pattern.thetaStart
                        && candidate.phiStart == pattern.phiStart
                        && candidate.thetaStep == pattern.thetaStep
                        && candidate.phiStep == pattern.phiStep;
                });
            if (found != currentSetup_.radiationPatterns.rend())
                analysisRequestEditor_->selectPattern(found->sourceLine);
        });
    connect(analysisRequestEditor_, &AnalysisRequestEditor::patternDeleteRequested, this,
        [this](std::size_t sourceLine) {
            deleteSetupCard(tr("Delete radiation pattern"), sourceLine);
        });
    analysisRequestsTabIndex_ = analysisWorkspace_->addTab(
        scrollableEditor(analysisRequestEditor_, analysisWorkspace_), tr("Requests"));
    analysisSolverTabIndex_ = analysisWorkspace_->addTab(
        scrollableEditor(analysisSetupEditor_, analysisWorkspace_), tr("Solver"));
    analysisWorkspace_->setTabIcon(modelSourcesWorkspaceIndex_, workbenchIcon("sources"));
    analysisWorkspace_->setTabIcon(loadNetworkTabIndex_, workbenchIcon("loads"));
    analysisWorkspace_->setTabIcon(modelEnvironmentWorkspaceIndex_, workbenchIcon("environment"));
    analysisWorkspace_->setTabIcon(analysisSolverTabIndex_, workbenchIcon("settings"));
    analysisWorkspace_->setTabIcon(analysisFrequencyTabIndex_, workbenchIcon("frequency-sweep"));
    analysisWorkspace_->setTabIcon(analysisRequestsTabIndex_, workbenchIcon("radiation-pattern"));

    auto* runsPage = new QWidget(moduleStack_);
    auto* runsLayout = new QVBoxLayout(runsPage);
    runsLayout->setContentsMargins(10, 10, 10, 10);
    auto* runsHeading = new QLabel(tr("Runs"), runsPage);
    auto runsHeadingFont = runsHeading->font();
    runsHeadingFont.setBold(true);
    runsHeadingFont.setPointSize(runsHeadingFont.pointSize() + 3);
    runsHeading->setFont(runsHeadingFont);
    auto* runsDescription = new QLabel(tr(
        "Analysis runs appear individually. Optimization candidates are grouped under one session row. "
        "Open or double-click a row to inspect it in the separate, reusable Run Review window."), runsPage);
    runsDescription->setWordWrap(true);
    analysisRuns_ = new QTableWidget(0, RunColumnCount, runsPage);
    analysisRuns_->setObjectName(QStringLiteral("analysisRunsTable"));
    analysisRuns_->setHorizontalHeaderLabels({tr("Started"), tr("Model"), tr("Note"), tr("Type"),
        tr("Results"), tr("Output"), tr("Backend"), tr("Status"), tr("Duration"), tr("Run Folder")});
    analysisRuns_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    analysisRuns_->setSelectionBehavior(QAbstractItemView::SelectRows);
    analysisRuns_->setSelectionMode(QAbstractItemView::SingleSelection);
    analysisRuns_->setAlternatingRowColors(true);
    analysisRuns_->horizontalHeader()->setStretchLastSection(true);
    auto* runViewButtons = new QHBoxLayout;
    auto* runManagementButtons = new QHBoxLayout;
    cancelRunButton_ = new QPushButton(tr("Cancel Active Run"), runsPage);
    cancelRunButton_->setEnabled(false);
    openRunResultsButton_ = new QPushButton(tr("Open Run Review"), runsPage);
    openRunResultsButton_->setObjectName(QStringLiteral("openRunResultsButton"));
    openRunResultsButton_->setEnabled(false);
    inspectRunInputButton_ = new QPushButton(tr("Inspect Input Snapshot"), runsPage);
    inspectRunInputButton_->setObjectName(QStringLiteral("inspectRunInputButton"));
    inspectRunInputButton_->setEnabled(false);
    openRunSnapshotButton_ = new QPushButton(tr("Open Snapshot as New Model"), runsPage);
    openRunSnapshotButton_->setObjectName(QStringLiteral("openRunSnapshotButton"));
    openRunSnapshotButton_->setEnabled(false);
    openRunFolderButton_ = new QPushButton(tr("Open Run Folder"), runsPage);
    openRunFolderButton_->setEnabled(false);
    pinRunReferenceButton_ = new QPushButton(tr("Use as Historical Overlay"), runsPage);
    pinRunReferenceButton_->setObjectName(QStringLiteral("pinRunReferenceButton"));
    pinRunReferenceButton_->setEnabled(false);
    editRunNoteButton_ = new QPushButton(tr("Edit Run Note…"), runsPage);
    editRunNoteButton_->setObjectName(QStringLiteral("editRunNoteButton"));
    editRunNoteButton_->setEnabled(false);
    deleteRunButton_ = new QPushButton(tr("Delete Run…"), runsPage);
    deleteRunButton_->setObjectName(QStringLiteral("deleteRunButton"));
    deleteRunButton_->setEnabled(false);
    runViewButtons->addWidget(openRunResultsButton_);
    runViewButtons->addWidget(inspectRunInputButton_);
    runViewButtons->addWidget(openRunSnapshotButton_);
    runViewButtons->addWidget(pinRunReferenceButton_);
    runViewButtons->addStretch();
    runManagementButtons->addWidget(cancelRunButton_);
    runManagementButtons->addWidget(editRunNoteButton_);
    runManagementButtons->addWidget(openRunFolderButton_);
    runManagementButtons->addWidget(deleteRunButton_);
    runManagementButtons->addStretch();
    runsLayout->addWidget(runsHeading);
    runsLayout->addWidget(runsDescription);
    runsLayout->addWidget(analysisRuns_, 1);
    runsLayout->addLayout(runViewButtons);
    runsLayout->addLayout(runManagementButtons);
    connect(cancelRunButton_, &QPushButton::clicked, this, [this] { cancelAnalysis(); });
    connect(openRunResultsButton_, &QPushButton::clicked, this, [this] { loadSelectedRun(); });
    connect(inspectRunInputButton_, &QPushButton::clicked, this, [this] { inspectSelectedRunInput(); });
    connect(openRunSnapshotButton_, &QPushButton::clicked, this, [this] { openSelectedRunSnapshot(); });
    connect(pinRunReferenceButton_, &QPushButton::clicked,
        this, [this] { pinSelectedRunReference(); });
    connect(editRunNoteButton_, &QPushButton::clicked,
        this, [this] { editSelectedRunNote(); });
    connect(deleteRunButton_, &QPushButton::clicked, this, [this] { deleteSelectedRun(); });
    connect(openRunFolderButton_, &QPushButton::clicked, this, [this] {
        const auto* item = analysisRuns_->currentRow() >= 0
            ? analysisRuns_->item(analysisRuns_->currentRow(), RunStartedColumn) : nullptr;
        const auto directory = item != nullptr
            ? item->data(RunDirectoryRole).toString() : currentRunDirectory_;
        if (!directory.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
        }
    });
    connect(analysisRuns_, &QTableWidget::itemSelectionChanged,
        this, [this] { updateRunSelectionActions(); });
    connect(analysisRuns_, &QTableWidget::cellDoubleClicked,
        this, [this](int, int column) {
            if (column == RunNoteColumn) editSelectedRunNote();
            else loadSelectedRun();
        });
    runHistoryPanel_ = new DetachablePanel(QStringLiteral("run-history"),
        tr("Run History"), moduleStack_, {QStringLiteral("workspaceWindows"),
            QStringLiteral("runHistoryWindow"), kResultWindowSize});
    runHistoryPanel_->setStatusText(tr("Saved analysis and study runs"));
    runHistoryPanel_->setContent(runsPage);
    moduleStack_->addWidget(runHistoryPanel_);
    QTimer::singleShot(0, this, [this] {
        if (runHistoryPanel_->isWindowVisible() && !runHistoryLoaded_) loadRunHistory();
    });

    auto* analysisNavigator = new WorkspaceNavigator(
        analysisWorkspace_, QStringLiteral("analysis"), moduleStack_);
    setupPanel_ = new DetachablePanel(QStringLiteral("analysis-setup"),
        tr("Analysis Setup"), moduleStack_, {QStringLiteral("workspaceWindows"),
            QStringLiteral("analysisSetupWindow")});
    setupPanel_->setStatusText(tr("No model loaded"));
    setupPanel_->setContent(analysisNavigator);
    analysisModuleIndex_ = moduleStack_->addWidget(setupPanel_);

    createResultWindows();
    createOptimizationWorkspace();
    preserveControlHeights(moduleStack_);
    showModule(homeModuleIndex_);
}

void MainWindow::createResultWindows()
{
    resultWindowsHost_ = new QWidget(moduleStack_);
    resultWindowsHost_->setObjectName(QStringLiteral("resultWindowsHost"));
    resultWindowsHost_->hide();
    validationPanel_ = new DetachablePanel(QStringLiteral("validation"),
        tr("Validation"), resultWindowsHost_, {QStringLiteral("resultWindows"),
            QStringLiteral("validationWindow"), kResultWindowSize});
    validationWorkspace_ = new QTabWidget(validationPanel_);
    validationWorkspace_->setObjectName(QStringLiteral("validationWorkspace"));
    validationWorkspace_->setDocumentMode(true);
    auto* validationOverview = new QWidget(validationWorkspace_);
    validationOverview->setObjectName(QStringLiteral("validationOverview"));
    auto* validationOverviewLayout = new QVBoxLayout(validationOverview);
    validationOverviewLayout->setContentsMargins(8, 8, 8, 8);
    validationOverviewLayout->setSpacing(6);
    auto* validationOverviewHeading = new QLabel(tr("Static Model Check"), validationOverview);
    auto validationOverviewFont = validationOverviewHeading->font();
    validationOverviewFont.setBold(true);
    validationOverviewHeading->setFont(validationOverviewFont);
    validationOverviewSummary_ = new QLabel(
        tr("Run Check Model to evaluate the current source."), validationOverview);
    validationOverviewSummary_->setObjectName(QStringLiteral("validationOverviewSummary"));
    validationOverviewScope_ = new QLabel(tr(
        "Checks NEC syntax, backend compatibility, references, geometry, and model adequacy. "
        "Average Gain Test and convergence are separate solver-backed studies."),
        validationOverview);
    validationOverviewScope_->setObjectName(QStringLiteral("validationOverviewScope"));
    validationOverviewScope_->setWordWrap(true);
    auto* validationOverviewButtons = new QHBoxLayout;
    auto* checkFromValidationButton = new QPushButton(tr("Check Model…"), validationOverview);
    checkFromValidationButton->setObjectName(QStringLiteral("checkModelFromValidationButton"));
    auto* openModelCheckButton = new QPushButton(tr("Open Current Report"), validationOverview);
    openModelCheckButton->setObjectName(QStringLiteral("openModelCheckReportButton"));
    auto* changeValidationSolverButton = new QPushButton(tr("Change Solver…"), validationOverview);
    changeValidationSolverButton->setObjectName(QStringLiteral("changeValidationSolverButton"));
    validationOverviewButtons->addWidget(checkFromValidationButton);
    validationOverviewButtons->addWidget(openModelCheckButton);
    validationOverviewButtons->addStretch();
    validationOverviewButtons->addWidget(changeValidationSolverButton);
    validationOverviewLayout->addWidget(validationOverviewHeading);
    validationOverviewLayout->addWidget(validationOverviewSummary_);
    validationOverviewLayout->addWidget(validationOverviewScope_);
    validationOverviewLayout->addLayout(validationOverviewButtons);
    validationOverviewLayout->addStretch();
    connect(checkFromValidationButton, &QPushButton::clicked,
        this, [this] { checkModel(true); });
    connect(openModelCheckButton, &QPushButton::clicked, modelCheckWindowAction_, &QAction::trigger);
    connect(changeValidationSolverButton, &QPushButton::clicked, this, [this] {
        if (!showModule(analysisModuleIndex_)) return;
        analysisWorkspace_->setCurrentIndex(analysisSolverTabIndex_);
    });
    validationOverviewTabIndex_ = validationWorkspace_->addTab(
        validationOverview, tr("Static Check"));
    averageGainResultsView_ = new AverageGainResultsView(validationWorkspace_);
    averageGainResultsView_->setRunAction(averageGainAction_);
    averageGainValidationTabIndex_ = validationWorkspace_->addTab(
        averageGainResultsView_, tr("Average Gain Test"));
    convergenceWorkspace_ = new ConvergenceWorkspace(validationWorkspace_);
    convergenceValidationTabIndex_ = validationWorkspace_->addTab(
        convergenceWorkspace_, tr("Segmentation Convergence"));
    validationPanel_->setContent(validationWorkspace_);
    convergenceWorkspace_->setRunsChangedCallback([this] {
        if (runHistoryLoaded_) loadRunHistory();
        refreshDashboardRuns();
    });
    convergenceWorkspace_->setRunningChangedCallback([this] { synchronizeRunnerState(); });
    convergenceWorkspace_->setSummaryChangedCallback([this](const QString& summary) {
        validationPanel_->setStatusText(summary);
        controlPanel_->setStudyStatus(ControlPanel::Study::Convergence,
            ControlPanel::StudyState::Complete, summary);
    });

    auto* impedanceContent = new QWidget(resultWindowsHost_);
    auto* impedanceLayout = new QVBoxLayout(impedanceContent);
    impedanceLayout->setContentsMargins(0, 0, 0, 0);
    impedanceLayout->setSpacing(2);
    impedanceOverlayControl_ = new HistoricalOverlayControl(
        QStringLiteral("impedance"), impedanceContent);
    auto* impedanceTabs = new QTabWidget(impedanceContent);
    impedanceTabs->setDocumentMode(true);
    analysisResultsView_ = new ImpedanceResultsView(impedanceTabs);
    impedanceTabs->addTab(analysisResultsView_, tr("Table"));
    visualizePlotsView_ = new SweepPlotsView(impedanceTabs);
    impedanceTabs->addTab(visualizePlotsView_, tr("Plots"));
    impedanceTabs->setCurrentWidget(visualizePlotsView_);
    visualizePlotsView_->setReferenceControlVisible(false);
    impedanceOverlayControl_->setVisibilityChangedCallback([this](bool visible) {
        visualizePlotsView_->setReferenceVisible(visible);
    });
    impedanceOverlayControl_->setChooseRequestedCallback([this](QWidget* anchor) {
        chooseResultReference(anchor);
    });
    impedanceLayout->addWidget(impedanceOverlayControl_);
    impedanceLayout->addWidget(impedanceTabs, 1);
    impedancePanel_ = new DetachablePanel(
        QStringLiteral("impedance"), tr("Impedance Results"), resultWindowsHost_,
        {QStringLiteral("resultWindows"), {}, kResultWindowSize});
    impedancePanel_->setContent(impedanceContent);

    currentsPanel_ = new DetachablePanel(
        QStringLiteral("currents"), tr("Current Distribution"), resultWindowsHost_,
        {QStringLiteral("resultWindows"), {}, kResultWindowSize});
    currentResultsView_ = new CurrentDistributionView(currentsPanel_);
    currentsPanel_->setContent(currentResultsView_);

    auto* radiationContent = new QWidget(resultWindowsHost_);
    auto* radiationLayout = new QVBoxLayout(radiationContent);
    radiationLayout->setContentsMargins(0, 0, 0, 0);
    radiationLayout->setSpacing(2);
    radiationOverlayControl_ = new HistoricalOverlayControl(
        QStringLiteral("radiation"), radiationContent);
    auto* radiationTabs = new QTabWidget(radiationContent);
    radiationTabs->setDocumentMode(true);
    radiationPatternView_ = new RadiationPatternView(radiationTabs);
    radiationTabs->addTab(radiationPatternView_, tr("2D Pattern"));
    radiation3DView_ = new Radiation3DView(radiationTabs);
    radiationTabs->addTab(radiation3DView_, tr("3D Pattern"));
    radiationPerformanceView_ = new RadiationPerformanceView(radiationTabs);
    radiationTabs->addTab(radiationPerformanceView_, tr("Performance vs Frequency"));
    radiationPatternView_->setReferenceControlVisible(false);
    radiation3DView_->setReferenceControlVisible(false);
    radiationPerformanceView_->setReferenceControlVisible(false);
    radiationOverlayControl_->setVisibilityChangedCallback([this](bool visible) {
        radiationPatternView_->setReferenceVisible(visible);
        radiation3DView_->setReferenceVisible(visible);
        radiationPerformanceView_->setReferenceVisible(visible);
    });
    radiationOverlayControl_->setChooseRequestedCallback([this](QWidget* anchor) {
        chooseResultReference(anchor);
    });
    radiationLayout->addWidget(radiationOverlayControl_);
    radiationLayout->addWidget(radiationTabs, 1);
    radiationPanel_ = new DetachablePanel(
        QStringLiteral("radiation"), tr("Radiation Results"), resultWindowsHost_,
        {QStringLiteral("resultWindows"), {}, kResultWindowSize});
    radiationPanel_->setContent(radiationContent);
    radiationPatternView_->setSettingsChangedCallback([this](const auto& settings) {
        radiation3DView_->setDisplaySettings(settings);
        radiationPerformanceView_->setComponent(settings.component);
    });
    radiation3DView_->setSettingsChangedCallback([this](const auto& settings) {
        radiationPatternView_->setDisplaySettings(settings);
        radiationPerformanceView_->setComponent(settings.component);
    });
    radiationPerformanceView_->setComponentChangedCallback([this](auto component) {
        radiationPatternView_->setComponent(component);
        radiation3DView_->setComponent(component);
    });

    auto* rawOutputContent = new QWidget(resultWindowsHost_);
    auto* rawOutputLayout = new QVBoxLayout(rawOutputContent);
    rawOutputLayout->setContentsMargins(6, 4, 6, 6);
    rawOutputLayout->setSpacing(4);
    auto* rawOutputDescription = new QLabel(tr(
        "Complete, immutable model.out from the latest loaded analysis. Parsed result windows "
        "do not alter this text."), rawOutputContent);
    rawOutputDescription->setWordWrap(true);
    auto* rawOutputControls = new QHBoxLayout;
    rawOutputControls->setSpacing(6);
    rawOutputFrequencyControl_ = new QComboBox(rawOutputContent);
    rawOutputFrequencyControl_->setObjectName(QStringLiteral("rawOutputFrequencyControl"));
    rawOutputFrequencyControl_->setMinimumContentsLength(12);
    rawOutputFrequencyControl_->setEnabled(false);
    auto* jumpFrequencyButton = new QPushButton(
        tr("Jump to Frequency"), rawOutputContent);
    rawOutputFindControl_ = new QLineEdit(rawOutputContent);
    rawOutputFindControl_->setObjectName(QStringLiteral("rawOutputFindControl"));
    rawOutputFindControl_->setPlaceholderText(tr("Find text"));
    auto* findOutputButton = new QPushButton(tr("Find Next"), rawOutputContent);
    rawOutputControls->addWidget(new QLabel(tr("Frequency:"), rawOutputContent));
    rawOutputControls->addWidget(rawOutputFrequencyControl_);
    rawOutputControls->addWidget(jumpFrequencyButton);
    rawOutputControls->addSpacing(8);
    rawOutputControls->addWidget(rawOutputFindControl_, 1);
    rawOutputControls->addWidget(findOutputButton);
    analysisOutput_ = new QPlainTextEdit(rawOutputContent);
    analysisOutput_->setObjectName(QStringLiteral("analysisRawOutput"));
    analysisOutput_->setReadOnly(true);
    analysisOutput_->setLineWrapMode(QPlainTextEdit::NoWrap);
    analysisOutput_->setPlaceholderText(tr("Complete solver output will appear here."));
    rawOutputLayout->addWidget(rawOutputDescription);
    rawOutputLayout->addLayout(rawOutputControls);
    rawOutputLayout->addWidget(analysisOutput_, 1);
    connect(jumpFrequencyButton, &QPushButton::clicked,
        this, [this] { jumpRawOutputToSelectedFrequency(); });
    connect(findOutputButton, &QPushButton::clicked,
        this, [this] { findInRawOutput(); });
    connect(rawOutputFindControl_, &QLineEdit::returnPressed,
        this, [this] { findInRawOutput(); });
    rawOutputPanel_ = new DetachablePanel(
        QStringLiteral("raw-output"), tr("Raw Solver Output"), resultWindowsHost_,
        {QStringLiteral("resultWindows"), QStringLiteral("rawOutputWindow"),
            kResultWindowSize});
    rawOutputPanel_->setContent(rawOutputContent);
}

void MainWindow::createOptimizationWorkspace()
{
    optimizationPanel_ = new DetachablePanel(QStringLiteral("optimization"),
        tr("Optimization"), moduleStack_, {QStringLiteral("workspaceWindows"),
            QStringLiteral("optimizationWindow")});
    optimizationPanel_->setStatusText(tr("No model loaded"));
    optimizationWorkspace_ = new OptimizationWorkspace(optimizationPanel_);
    optimizationWorkspace_->setObjectName(QStringLiteral("optimizationWorkspace"));
    optimizationWorkspace_->setRunsChangedCallback([this] {
        if (runHistoryLoaded_) loadRunHistory();
        refreshDashboardRuns();
    });
    optimizationWorkspace_->setRunningChangedCallback([this] {
        synchronizeRunnerState();
        const auto modelName = currentFile_.isEmpty()
            ? tr("Untitled model.nec") : QFileInfo(currentFile_).fileName();
        optimizationPanel_->setStatusText(tr("%1 · %2").arg(modelName,
            optimizationWorkspace_->isRunning() ? tr("Running") : tr("Ready")));
    });
    optimizationWorkspace_->setParameterizationHelpCallback([this] {
        if (!showModule(modelModuleIndex_)) return;
        modelWorkspace_->setCurrentIndex(modelDeckWorkspaceIndex_);
        sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
        auto* cardTabs = qobject_cast<QTabWidget*>(wireCardEditor_->parentWidget());
        if (cardTabs != nullptr && cardTabs->currentWidget() == structuredCardEditor_)
            structuredCardEditor_->focusParameterizableField();
        else
            wireCardEditor_->focusParameterizableField();
        const auto originalStyle = structuredParameterizationHint_->styleSheet();
        structuredParameterizationHint_->setStyleSheet(QStringLiteral(
            "QLabel { background: palette(highlight); color: palette(highlighted-text); "
            "padding: 4px; border-radius: 3px; }"));
        QTimer::singleShot(2500, structuredParameterizationHint_,
            [hint = structuredParameterizationHint_, originalStyle] {
                hint->setStyleSheet(originalStyle);
            });
        statusBar()->showMessage(tr(
            "Right-click a numeric field and choose Parameterize Field or Change Parameter Link."),
            6000);
    });
    optimizationWorkspace_->setApplyParameterCallback(
        [this](std::vector<std::pair<QString, double>> values) {
            return applyOptimizedParameters(values);
        });
    optimizationWorkspace_->setApplyAndRunCallback(
        [this](std::vector<std::pair<QString, double>> values) {
            if (!applyOptimizedParameters(values)) return false;
            checkModel();
            if (modelErrorCount_ != 0 || !runAction_->isEnabled()) {
                statusBar()->showMessage(
                    tr("Optimized values were applied, but the model is not ready to run."), 5000);
                return false;
            }
            startAnalysis();
            return solverRunner_->isRunning();
        });
    optimizationWorkspace_->setBaselineRunCallback(
        [this](const analysis::FrequencyPlan& frequencyPlan,
            const analysis::OptimizationObjectiveSpec& objective) {
            if (solverRunner_->isRunning() || convergenceWorkspace_->isRunning()) return false;
            checkModel();
            const auto& cache = activeSource();
            const auto& resolution = cache.resolution();
            if (modelErrorCount_ != 0 || !resolution.ok()) return false;
            const auto solverInput = analysis::prepareOptimizationInput(
                resolution.generatedDeck, frequencyPlan, objective);
            currentBaselineReferenceImpedanceOhms_ = objective.referenceImpedance;
            startAnalysisRun(std::string(cache.source()), solverInput,
                QStringLiteral("optimization-baseline"),
                tr("Optimization baseline · %1 frequencies")
                    .arg(analysis::frequencyPlanPoints(frequencyPlan).size()),
                tr("Running optimization baseline"),
                SolverRunPurpose::OptimizationBaseline);
            return solverRunner_->isRunning();
        });
    optimizationWorkspace_->setCancelBaselineRunCallback(
        [this] { cancelAnalysis(); });
    optimizationWorkspace_->setFindMaximumGainCallback(
        [this](double frequencyMHz) {
            return startOptimizationPeakSearch(frequencyMHz);
        });
    optimizationPanel_->setContent(optimizationWorkspace_);
    optimizeModuleIndex_ = moduleStack_->addWidget(optimizationPanel_);
}

auto MainWindow::showModule(int index) -> bool
{
    if (moduleStack_ == nullptr || index < 0 || index >= moduleStack_->count()) {
        return false;
    }
    if (index == geometryModuleIndex_ && geometryPanel_ != nullptr) {
        geometryPanel_->showWindow();
        updateUndoActions();
        return true;
    }
    if (index == modelModuleIndex_ && modelPanel_ != nullptr) {
        modelPanel_->showWindow();
        const auto current = moduleStack_->currentIndex();
        if (current == homeModuleIndex_) homeModuleAction_->setChecked(true);
        else if (current == analysisModuleIndex_) analysisModuleAction_->setChecked(true);
        else if (current == optimizeModuleIndex_) optimizeModuleAction_->setChecked(true);
        updateUndoActions();
        return true;
    }
    if (index == analysisModuleIndex_ && setupPanel_ != nullptr) {
        setupPanel_->showWindow();
        const auto current = moduleStack_->currentIndex();
        if (current == homeModuleIndex_) homeModuleAction_->setChecked(true);
        else if (current == optimizeModuleIndex_) optimizeModuleAction_->setChecked(true);
        updateUndoActions();
        return true;
    }
    if (index == optimizeModuleIndex_ && optimizationPanel_ != nullptr) {
        optimizationPanel_->showWindow();
        const auto current = moduleStack_->currentIndex();
        if (current == homeModuleIndex_) homeModuleAction_->setChecked(true);
        updateUndoActions();
        return true;
    }
    if (index != moduleStack_->currentIndex() && !confirmCurrentEditorNavigation()) {
        const auto current = moduleStack_->currentIndex();
        if (current == homeModuleIndex_) homeModuleAction_->setChecked(true);
        else if (current == modelModuleIndex_) modelModuleAction_->setChecked(true);
        else if (current == analysisModuleIndex_) analysisModuleAction_->setChecked(true);
        else if (current == optimizeModuleIndex_) optimizeModuleAction_->setChecked(true);
        return false;
    }
    moduleStack_->setCurrentIndex(index);
    if (index == homeModuleIndex_) {
        homeModuleAction_->setChecked(true);
        dashboardStack_->setCurrentIndex(hasNecModel_ ? 1 : 0);
    } else if (index == modelModuleIndex_) {
        modelModuleAction_->setChecked(true);
    } else if (index == analysisModuleIndex_) {
        analysisModuleAction_->setChecked(true);
    } else if (index == optimizeModuleIndex_) {
        optimizeModuleAction_->setChecked(true);
    }
    updateUndoActions();
    return true;
}

auto MainWindow::confirmCurrentEditorNavigation() -> bool
{
    if (moduleStack_ == nullptr) return true;
    if (moduleStack_->currentIndex() == modelModuleIndex_)
        return confirmPendingEdits(modelWorkspace_->currentWidget());
    if (moduleStack_->currentIndex() == analysisModuleIndex_)
        return confirmPendingEdits(analysisWorkspace_->currentWidget());
    return true;
}

auto MainWindow::confirmPendingEdits(QWidget* page) -> bool
{
    const auto setupPending = setupEditor_ != nullptr && setupEditor_->hasPendingEdits(page);
    const auto networkPending = loadNetworkEditor_ != nullptr
        && page == loadNetworkEditor_ && loadNetworkEditor_->hasPendingEdits();
    const auto parameterPending = parameterEditor_ != nullptr
        && page == parameterEditor_ && parameterEditor_->hasPendingEdits();
    const auto requestPending = analysisRequestEditor_ != nullptr && page != nullptr
        && (page == analysisRequestEditor_ || page->isAncestorOf(analysisRequestEditor_))
        && analysisRequestEditor_->hasPendingEdits();
    if (!setupPending && !networkPending && !parameterPending && !requestPending) return true;

    const auto answer = QMessageBox::warning(this, tr("Unapplied Changes"), tr(
        "This page contains changes that have not been applied.\n\n"
        "Choose Cancel to return and use the highlighted Apply button, or discard the edits."),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Discard) return false;
    if (setupPending) setupEditor_->discardPendingEdits(page);
    if (networkPending) loadNetworkEditor_->discardPendingEdits();
    if (parameterPending) parameterEditor_->discardPendingEdits();
    if (requestPending) analysisRequestEditor_->discardPendingEdits();
    return true;
}

void MainWindow::showPrimaryResultsWindow()
{
    if (!resultsAvailable_) {
        showRunHistory();
        return;
    }
    if (!displayedResults_.feedpoints.empty()) resultWindowCoordinator_->showImpedance();
    else if (!displayedResults_.radiation.empty()) resultWindowCoordinator_->showRadiation();
    else if (!displayedResults_.currents.empty()) resultWindowCoordinator_->showCurrents();
    else showRunHistory();
}

void MainWindow::setLiveResultStatus(const QString& status)
{
    if (impedancePanel_ != nullptr) impedancePanel_->setStatusText(status);
    if (currentsPanel_ != nullptr) currentsPanel_->setStatusText(status);
    if (radiationPanel_ != nullptr) radiationPanel_->setStatusText(status);
    if (rawOutputPanel_ != nullptr) rawOutputPanel_->setStatusText(status);
}

void MainWindow::showEditor(EditorDestination destination, int geometryTab)
{
    if (destination == EditorDestination::RawSource) {
        if (!showModule(modelModuleIndex_)) return;
        modelWorkspace_->setCurrentIndex(modelDeckWorkspaceIndex_);
        sourceWorkspace_->setCurrentIndex(0);
    } else if (destination == EditorDestination::Frequency) {
        if (!showModule(analysisModuleIndex_)) return;
        analysisWorkspace_->setCurrentIndex(analysisFrequencyTabIndex_);
    } else if (destination == EditorDestination::Sources) {
        if (!showModule(analysisModuleIndex_)) return;
        analysisWorkspace_->setCurrentIndex(modelSourcesWorkspaceIndex_);
    } else if (destination == EditorDestination::Loads) {
        if (!showModule(analysisModuleIndex_)) return;
        analysisWorkspace_->setCurrentIndex(loadNetworkTabIndex_);
    } else if (destination == EditorDestination::Environment) {
        if (!showModule(analysisModuleIndex_)) return;
        analysisWorkspace_->setCurrentIndex(modelEnvironmentWorkspaceIndex_);
    } else if (destination == EditorDestination::Requests) {
        if (!showModule(analysisModuleIndex_)) return;
        analysisWorkspace_->setCurrentIndex(analysisRequestsTabIndex_);
    } else if (destination == EditorDestination::Parameters) {
        if (!showModule(modelModuleIndex_)) return;
        modelWorkspace_->setCurrentIndex(modelParametersWorkspaceIndex_);
    } else {
        if (!showModule(geometryModuleIndex_)) return;
        workspace_->setCurrentIndex(geometryTab);
    }
}

void MainWindow::setDisplayLengthUnit(model::LengthUnit unit)
{
    auto settings = geometrySettings_;
    settings.lengthUnit = unit;
    if (settings.snapUnitBehavior == SnapUnitBehavior::UnitFriendly) {
        const auto convertedSnap = model::fromMeters(settings.snapSpacingMeters, unit);
        settings.snapSpacingMeters = model::toMeters(model::niceEngineeringStep(convertedSnap), unit);
    }
    applyGeometrySettings(settings);
}

void MainWindow::updateDeckUnitControls(const nec::DeckGeometryUnitInfo& info)
{
    deckScaleToMeters_ = info.uniform ? info.scaleToMeters : 1.0;
    updatingDeckUnitControl_ = true;
    const auto unitIndex = info.uniform && info.standardUnit
        ? deckUnitControl_->findData(static_cast<int>(*info.standardUnit))
        : deckUnitControl_->findData(-1);
    deckUnitControl_->setCurrentIndex(std::max(0, unitIndex));
    updatingDeckUnitControl_ = false;

    QString unitLabel;
    if (info.uniform && info.standardUnit) {
        const auto symbol = model::lengthUnitSymbol(*info.standardUnit);
        unitLabel = QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size()));
    } else {
        unitLabel = info.uniform ? tr("custom") : tr("mixed");
    }
    deckScaleLabel_->setText(!info.uniform
        ? tr("Different GS scales apply to different wires")
        : !info.hasScaleCard && info.standardUnit == model::LengthUnit::Meter
            ? tr("Native meters · no GS card")
            : tr("GS scale ×%1 to meters").arg(formatDecimal(info.scaleToMeters)));
    deckUnitControl_->setToolTip(tr(
        "These are the coordinate and radius units written in NEC geometry cards. "
        "This setting is independent of Geometry display units."));
    wireCardEditor_->setDeckScale(
        info.uniform ? info.scaleToMeters : 1.0, unitLabel);
    structuredCardEditor_->setDeckUnitLabel(unitLabel);
}

auto MainWindow::deckScaleForSourceLine(std::size_t sourceLine) const -> double
{
    const auto& cache = activeSource();
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) return deckScaleToMeters_;
    return nec::geometryScaleForLine(*cache.resolvedDocument(), sourceLine);
}

void MainWindow::changeDeckLengthUnit(model::LengthUnit unit)
{
    const auto targetScale = model::metersPerUnit(unit);
    if (std::abs(targetScale - deckScaleToMeters_) <= 1.0e-12) return;

    const auto source = editor_->toPlainText();
    const auto& cache = activeSource();
    const auto& rawDocument = cache.document();
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) {
        QMessageBox::warning(this, tr("Change NEC Deck Units"),
            tr("Resolve the model's symbol expressions before changing deck geometry units."));
        checkModel();
        return;
    }
    const auto& resolvedDocument = *cache.resolvedDocument();
    const auto currentUnits = nec::inspectDeckGeometryUnits(resolvedDocument);
    if (!currentUnits.uniform) {
        QMessageBox::warning(this, tr("Change NEC Deck Units"),
            tr("This model uses different effective GS scales for different wires. "
               "Workbench will preserve the source instead of guessing how to normalize it."));
        checkModel();
        return;
    }

    const auto unsupportedGeometry = std::ranges::find_if(rawDocument.cards(), [](const auto& card) {
        static const std::array<std::string_view, 10> geometryCards{
            "GA", "GC", "GF", "GH", "GM", "GR", "GX", "SC", "SM", "SP"};
        return std::ranges::find(geometryCards, card.mnemonic) != geometryCards.end();
    });
    if (unsupportedGeometry != rawDocument.cards().end()) {
        QMessageBox::warning(this, tr("Change NEC Deck Units"),
            tr("Automatic unit conversion currently supports GW wire geometry only. "
               "The %1 card on line %2 was left unchanged.")
                .arg(QString::fromStdString(unsupportedGeometry->mnemonic))
                .arg(static_cast<qulonglong>(unsupportedGeometry->lineNumber)));
        checkModel();
        return;
    }

    const auto symbolicWire = std::ranges::find_if(rawDocument.cards(), [](const auto& card) {
        if (card.kind != nec::NecCardKind::GeometryWire || card.fields.size() != 9) return false;
        for (auto index = 2; index < 9; ++index) {
            const auto& field = card.fields[static_cast<std::size_t>(index)];
            double value{};
            const auto [end, error] = std::from_chars(
                field.data(), field.data() + field.size(), value);
            if (error != std::errc{} || end != field.data() + field.size()) return true;
        }
        return false;
    });
    if (symbolicWire != rawDocument.cards().end()) {
        QMessageBox::information(this, tr("Symbolic Geometry Units"),
            tr("This GW card uses SY expressions. Workbench recognizes its GS scale, but will not "
               "rewrite symbolic geometry automatically because that could change optimizer meaning. "
               "Choose deck units before parameterizing the geometry, or update the expressions and GS card together."));
        checkModel();
        return;
    }

    auto lines = source.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const auto& wire : currentModel_.wires()) {
        const auto lineIndex = static_cast<int>(wire.sourceLine) - 1;
        if (lineIndex < 0 || lineIndex >= lines.size()) continue;
        lines[lineIndex] = QString::fromStdString(
            nec::NecWriter{}.writeWireCard(wire, targetScale));
    }
    for (auto index = lines.size() - 1; index >= 0; --index) {
        if (lines[index].trimmed().section(QLatin1Char(' '), 0, 0).compare(
                QStringLiteral("GS"), Qt::CaseInsensitive) == 0) {
            lines.removeAt(index);
        }
    }
    if (unit != model::LengthUnit::Meter) {
        auto insertion = lines.size();
        for (auto index = 0; index < lines.size(); ++index) {
            if (lines[index].trimmed().section(QLatin1Char(' '), 0, 0).compare(
                    QStringLiteral("GE"), Qt::CaseInsensitive) == 0) {
                insertion = index;
                break;
            }
        }
        lines.insert(insertion, QString::fromStdString(
            nec::NecWriter{}.writeGeometryScaleCard(targetScale)));
    }
    const auto symbol = model::lengthUnitSymbol(unit);
    pushGeometrySourceEdit(tr("Change NEC deck geometry units to %1")
            .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size()))),
        lines.join(QLatin1Char('\n')));
}

void MainWindow::setSnapSpacing(double meters)
{
    auto settings = geometrySettings_;
    const auto displayedSnap = model::fromMeters(meters, settings.lengthUnit);
    settings.snapSpacingMeters = settings.snapUnitBehavior == SnapUnitBehavior::UnitFriendly
        ? model::toMeters(model::niceEngineeringStep(displayedSnap), settings.lengthUnit)
        : meters;
    applyGeometrySettings(settings);
}

void MainWindow::applyGeometrySettings(const GeometrySettings& settings)
{
    geometrySettings_ = settings;
    geometrySettings_.manualGridSpacingMeters = std::max(geometrySettings_.manualGridSpacingMeters, 1.0e-12);
    geometrySettings_.snapSpacingMeters = std::max(geometrySettings_.snapSpacingMeters, 1.0e-12);
    geometrySettings_.minorGridDivisions = std::clamp(geometrySettings_.minorGridDivisions, 1, 10);
    geometrySettings_.endpointTolerancePixels = std::clamp(geometrySettings_.endpointTolerancePixels, 1.0, 50.0);
    const auto symbol = model::lengthUnitSymbol(geometrySettings_.lengthUnit);
    const auto suffix = QStringLiteral(" %1")
        .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
    {
        const QSignalBlocker unitBlocker(lengthUnitControl_);
        const QSignalBlocker blocker(snapSpacingControl_);
        const QSignalBlocker gridBlocker(snapGridAction_);
        const QSignalBlocker endpointBlocker(snapEndpointsAction_);
        const auto unitIndex = lengthUnitControl_->findData(static_cast<int>(geometrySettings_.lengthUnit));
        if (unitIndex >= 0) {
            lengthUnitControl_->setCurrentIndex(unitIndex);
        }
        snapSpacingControl_->setSuffix(suffix);
        snapSpacingControl_->setValue(model::fromMeters(
            geometrySettings_.snapSpacingMeters, geometrySettings_.lengthUnit));
        snapGridAction_->setChecked(geometrySettings_.gridSnapping);
        snapEndpointsAction_->setChecked(geometrySettings_.endpointSnapping);
    }
    xyView_->setSettings(geometrySettings_);
    xzView_->setSettings(geometrySettings_);
    yzView_->setSettings(geometrySettings_);
    if (geometry3DView_ != nullptr) {
        geometry3DView_->setLengthUnit(geometrySettings_.lengthUnit);
    }
    if (loadNetworkEditor_ != nullptr) {
        loadNetworkEditor_->setLengthUnit(geometrySettings_.lengthUnit);
    }
}

void MainWindow::showGeometrySettings()
{
    GeometrySettingsDialog dialog(geometrySettings_, this);
    if (dialog.exec() == QDialog::Accepted) {
        applyGeometrySettings(dialog.settings());
    }
}

void MainWindow::showAutoSegmentation()
{
    if (!modelChecked_ || modelErrorCount_ != 0 || currentModel_.wires().empty()) {
        QMessageBox::information(this, tr("Automatic Segmentation"),
            tr("Run Check Model and resolve geometry errors before calculating segment counts."));
        return;
    }
    if (!currentSetup_.frequency) {
        QMessageBox::information(this, tr("Automatic Segmentation"),
            tr("Add an analysis frequency before calculating wavelength-based segment counts."));
        return;
    }
    const auto& document = activeSource().document();
    const auto unsupportedAttachment = std::ranges::find_if(document.cards(), [this](const auto& card) {
        if (card.kind == nec::NecCardKind::Network) return true;
        if (card.kind == nec::NecCardKind::Load) {
            return std::ranges::none_of(currentSetup_.loads,
                [line = card.lineNumber](const auto& load) { return load.sourceLine == line; });
        }
        if (card.kind == nec::NecCardKind::TransmissionLine) {
            return std::ranges::none_of(currentSetup_.transmissionLines,
                [line = card.lineNumber](const auto& transmissionLine) {
                    return transmissionLine.sourceLine == line;
                });
        }
        return false;
    });
    const auto unsupportedExcitation = std::ranges::find_if(document.cards(), [this](const auto& card) {
        return card.kind == nec::NecCardKind::Excitation
            && std::ranges::none_of(currentSetup_.excitations,
                [line = card.lineNumber](const auto& excitation) {
                    return excitation.sourceLine == line;
                });
    });
    if (unsupportedAttachment != document.cards().end()
        || unsupportedExcitation != document.cards().end()) {
        QMessageBox::warning(this, tr("Automatic Segmentation"),
            tr("This deck contains an unsupported EX, LD, TL, or NT card. Supported EX 0, LD, and TL segment references are remapped automatically; NT networks remain unsupported."));
        return;
    }
    const auto maximumFrequencyMHz = std::max(currentSetup_.frequency->startMHz,
        model::frequencyEndMHz(*currentSetup_.frequency));
    AutoSegmentationDialog dialog(currentModel_, currentSetup_.excitations,
        currentSetup_.loads, currentSetup_.transmissionLines, maximumFrequencyMHz, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto proposal = dialog.proposal();
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    auto changed = false;
    auto replacementFailed = false;
    const auto replaceFields = [&lines, &changed, &replacementFailed](std::size_t sourceLine,
        std::span<const nec::NecFieldReplacement> replacements) {
        const auto lineIndex = static_cast<int>(sourceLine) - 1;
        if (lineIndex < 0 || lineIndex >= lines.size()) {
            replacementFailed = true;
            return;
        }
        const auto updated = nec::replaceNecCardFields(lines[lineIndex].toStdString(), replacements);
        if (!updated) {
            replacementFailed = true;
            return;
        }
        lines[lineIndex] = QString::fromStdString(*updated);
        changed = true;
    };
    for (const auto& wireProposal : proposal.wires) {
        const auto* original = currentModel_.wireByTag(wireProposal.wireTag);
        if (original == nullptr || original->segments == wireProposal.newSegments) {
            continue;
        }
        const std::array replacements{
            nec::NecFieldReplacement{1, std::to_string(wireProposal.newSegments)}};
        replaceFields(original->sourceLine, replacements);
    }
    for (std::size_t index = 0; index < currentSetup_.excitations.size(); ++index) {
        const auto& original = currentSetup_.excitations[index];
        const auto& updated = proposal.remappedExcitations[index];
        if (original.segment == updated.segment) {
            continue;
        }
        const std::array replacements{
            nec::NecFieldReplacement{2, std::to_string(updated.segment)}};
        replaceFields(updated.sourceLine, replacements);
    }
    for (std::size_t index = 0; index < currentSetup_.loads.size(); ++index) {
        const auto& original = currentSetup_.loads[index];
        const auto& updated = proposal.remappedLoads[index];
        if (original.firstSegment == updated.firstSegment
            && original.lastSegment == updated.lastSegment) continue;
        const std::array replacements{
            nec::NecFieldReplacement{2, std::to_string(updated.firstSegment)},
            nec::NecFieldReplacement{3, std::to_string(updated.lastSegment)}};
        replaceFields(updated.sourceLine, replacements);
    }
    for (std::size_t index = 0; index < currentSetup_.transmissionLines.size(); ++index) {
        const auto& original = currentSetup_.transmissionLines[index];
        const auto& updated = proposal.remappedTransmissionLines[index];
        if (original.segment1 == updated.segment1
            && original.segment2 == updated.segment2) continue;
        const std::array replacements{
            nec::NecFieldReplacement{1, std::to_string(updated.segment1)},
            nec::NecFieldReplacement{3, std::to_string(updated.segment2)}};
        replaceFields(updated.sourceLine, replacements);
    }
    if (replacementFailed) {
        QMessageBox::warning(this, tr("Automatic Segmentation"), tr(
            "A segment-reference field could not be updated safely. No source changes were applied."));
        return;
    }
    if (changed) {
        pushGeometrySourceEdit(tr("Automatically segment wires"), lines.join(QLatin1Char('\n')));
    } else {
        statusBar()->showMessage(tr("Wire segmentation already matches this recommendation."), 5000);
    }
}

void MainWindow::createDocks()
{
    projectDock_ = new QDockWidget(tr("Project Tree"), this);
    projectDock_->setObjectName(QStringLiteral("projectDock"));
    projectTree_ = new QTreeWidget(projectDock_);
    projectTree_->setObjectName(QStringLiteral("projectTree"));
    projectTree_->setHeaderHidden(true);
    projectTree_->setAlternatingRowColors(true);
    projectDock_->setWidget(projectTree_);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock_);
    projectDock_->hide();

    modelCheckWindow_ = new QDialog(this, Qt::Tool);
    modelCheckWindow_->setObjectName(QStringLiteral("modelCheckWindow"));
    modelCheckWindow_->setWindowTitle(tr("Model Check Report — NEC Workbench"));
    modelCheckWindow_->setModal(false);
    modelCheckWindow_->setAttribute(Qt::WA_DeleteOnClose, false);
    modelCheckWindow_->resize(820, 440);
    auto* validationLayout = new QVBoxLayout(modelCheckWindow_);
    validationLayout->setContentsMargins(6, 4, 6, 4);
    validationLayout->setSpacing(4);
    validationSummary_ = new QLabel(
        tr("Run Check Model to evaluate the current source."), modelCheckWindow_);
    auto summaryFont = validationSummary_->font();
    summaryFont.setBold(true);
    validationSummary_->setFont(summaryFont);
    validationScope_ = new QLabel(tr(
        "Static source and geometry findings. AGT and convergence results are available in "
        "the Validation window."), modelCheckWindow_);
    validationScope_->setWordWrap(true);
    diagnostics_ = new QTreeWidget(modelCheckWindow_);
    diagnostics_->setHeaderLabels({tr("Severity"), tr("Category"), tr("Line"), tr("Message")});
    diagnostics_->setRootIsDecorated(false);
    diagnostics_->setAlternatingRowColors(true);
    diagnostics_->header()->setStretchLastSection(true);
    diagnostics_->setColumnWidth(0, 90);
    diagnostics_->setColumnWidth(1, 130);
    diagnostics_->setColumnWidth(2, 60);
    validationLayout->addWidget(validationSummary_);
    validationLayout->addWidget(validationScope_);
    validationLayout->addWidget(diagnostics_, 1);
    auto* validationButtons = new QHBoxLayout;
    auto* recheckButton = new QPushButton(tr("Recheck"), modelCheckWindow_);
    auto* goToFindingButton = new QPushButton(tr("Go to Selected Finding"), modelCheckWindow_);
    auto* generatedDeckButton = new QPushButton(tr("View Generated Solver Deck…"), modelCheckWindow_);
    auto* changeSolverButton = new QPushButton(tr("Change Solver…"), modelCheckWindow_);
    auto* closeValidationButton = new QPushButton(tr("Close"), modelCheckWindow_);
    validationButtons->addWidget(recheckButton);
    validationButtons->addWidget(goToFindingButton);
    validationButtons->addWidget(generatedDeckButton);
    validationButtons->addStretch();
    validationButtons->addWidget(changeSolverButton);
    validationButtons->addWidget(closeValidationButton);
    validationLayout->addLayout(validationButtons);
    connect(recheckButton, &QPushButton::clicked, this, [this] { checkModel(true); });
    connect(goToFindingButton, &QPushButton::clicked, this, [this] {
        if (auto* item = diagnostics_->currentItem()) goToDiagnostic(item);
    });
    connect(generatedDeckButton, &QPushButton::clicked,
        this, [this] { showGeneratedSolverDeck(); });
    connect(changeSolverButton, &QPushButton::clicked, this, [this] {
        modelCheckWindow_->hide();
        if (!showModule(analysisModuleIndex_)) return;
        analysisWorkspace_->setCurrentIndex(analysisSolverTabIndex_);
    });
    connect(closeValidationButton, &QPushButton::clicked,
        modelCheckWindow_, &QDialog::hide);

    runMonitorWindow_ = new QDialog(this, Qt::Tool);
    runMonitorWindow_->setObjectName(QStringLiteral("runMonitorWindow"));
    runMonitorWindow_->setWindowTitle(tr("Run Monitor — NEC Workbench"));
    runMonitorWindow_->setModal(false);
    runMonitorWindow_->setAttribute(Qt::WA_DeleteOnClose, false);
    runMonitorWindow_->resize(760, 460);
    runMonitorAutoHideTimer_ = new QTimer(this);
    runMonitorAutoHideTimer_->setSingleShot(true);
    runMonitorAutoHideTimer_->setInterval(1250);
    connect(runMonitorAutoHideTimer_, &QTimer::timeout, this, [this] {
        if (!solverRunner_->isRunning() && !runMonitorPinnedOpen_)
            runMonitorWindow_->hide();
    });
    auto* monitorLayout = new QVBoxLayout(runMonitorWindow_);
    monitorLayout->setContentsMargins(6, 6, 6, 6);
    monitorLayout->setSpacing(6);
    runMonitorContextLabel_ = new QLabel(tr("No solver run has started."), runMonitorWindow_);
    runMonitorContextLabel_->setObjectName(QStringLiteral("runMonitorContext"));
    runMonitorContextLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    runMonitorContextLabel_->setWordWrap(true);
    runMonitorStatusLabel_ = new QLabel(tr("Idle"), runMonitorWindow_);
    runMonitorStatusLabel_->setObjectName(QStringLiteral("runMonitorStatus"));
    runMonitorProgress_ = new QProgressBar(runMonitorWindow_);
    runMonitorProgress_->setObjectName(QStringLiteral("runMonitorProgress"));
    runMonitorProgress_->setRange(0, 1);
    runMonitorProgress_->setValue(0);
    runMonitorProgress_->setTextVisible(false);
    solverOutput_ = new QPlainTextEdit(runMonitorWindow_);
    solverOutput_->setObjectName(QStringLiteral("runMonitorLog"));
    solverOutput_->setReadOnly(true);
    solverOutput_->setPlaceholderText(tr(
        "External solver command, progress, and diagnostics will appear here."));
    monitorLayout->addWidget(runMonitorContextLabel_);
    monitorLayout->addWidget(runMonitorStatusLabel_);
    monitorLayout->addWidget(runMonitorProgress_);
    monitorLayout->addWidget(solverOutput_, 1);
    auto* monitorButtons = new QHBoxLayout;
    auto* copyCommandButton = new QPushButton(tr("Copy Command"), runMonitorWindow_);
    auto* openActiveRunFolderButton = new QPushButton(tr("Open Run Folder"), runMonitorWindow_);
    runMonitorStopButton_ = new QPushButton(tr("Stop"), runMonitorWindow_);
    runMonitorStopButton_->setEnabled(false);
    auto* hideMonitorButton = new QPushButton(tr("Hide"), runMonitorWindow_);
    monitorButtons->addWidget(copyCommandButton);
    monitorButtons->addWidget(openActiveRunFolderButton);
    monitorButtons->addStretch();
    monitorButtons->addWidget(runMonitorStopButton_);
    monitorButtons->addWidget(hideMonitorButton);
    monitorLayout->addLayout(monitorButtons);
    connect(copyCommandButton, &QPushButton::clicked, this, [this] {
        if (!currentSolverCommand_.isEmpty())
            QApplication::clipboard()->setText(currentSolverCommand_);
    });
    connect(openActiveRunFolderButton, &QPushButton::clicked, this, [this] {
        if (!currentRunDirectory_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(currentRunDirectory_));
    });
    connect(runMonitorStopButton_, &QPushButton::clicked,
        this, [this] { cancelAnalysis(); });
    connect(hideMonitorButton, &QPushButton::clicked, this, [this] {
        runMonitorPinnedOpen_ = false;
        if (runMonitorAutoHideTimer_ != nullptr) runMonitorAutoHideTimer_->stop();
        runMonitorWindow_->hide();
    });

    connect(diagnostics_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) { goToDiagnostic(item); });
    connect(diagnostics_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item) { goToDiagnostic(item); });
    connect(projectTree_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) { synchronizeProjectItemSelection(item); });
    connect(projectTree_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item) { activateProjectItem(item); });

    updateProjectTree({}, nec::NecDocument{});
}

void MainWindow::createMenusAndToolbar()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->setObjectName(QStringLiteral("fileMenu"));
    fileMenu->addAction(newAction_);
    fileMenu->addAction(openAction_);
    recentModelsMenu_ = fileMenu->addMenu(tr("Open &Recent"));
    recentModelsMenu_->setObjectName(QStringLiteral("recentModelsMenu"));
    refreshRecentFileViews(recentFiles());
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction_);
    fileMenu->addAction(saveAsAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction_);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->setObjectName(QStringLiteral("editMenu"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);
    editMenu->addSeparator();
    editMenu->addAction(cutAction_);
    editMenu->addAction(copyAction_);
    editMenu->addAction(pasteAction_);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->setObjectName(QStringLiteral("viewMenu"));
    auto* densityMenu = viewMenu->addMenu(tr("&Workspace Density"));
    densityMenu->setObjectName(QStringLiteral("workspaceDensityMenu"));
    densityMenu->addAction(compactDensityAction_);
    densityMenu->addAction(standardDensityAction_);
    densityMenu->addAction(spaciousDensityAction_);
    auto* resultWindowsMenu = viewMenu->addMenu(tr("&Result Windows"));
    resultWindowsMenu->setObjectName(QStringLiteral("resultWindowsMenu"));
    auto* showImpedanceWindowAction = resultWindowsMenu->addAction(
        workbenchIcon("frequency-sweep"), tr("Impedance && SWR"));
    showImpedanceWindowAction->setObjectName(QStringLiteral("showImpedanceWindowAction"));
    showImpedanceWindowAction->setStatusTip(
        tr("Open or raise the live impedance and SWR result window"));
    connect(showImpedanceWindowAction, &QAction::triggered, this,
        [this] { resultWindowCoordinator_->showImpedance(); });
    auto* showCurrentsWindowAction = resultWindowsMenu->addAction(
        workbenchIcon("sources"), tr("Currents"));
    showCurrentsWindowAction->setObjectName(QStringLiteral("showCurrentsWindowAction"));
    showCurrentsWindowAction->setStatusTip(
        tr("Open or raise the live current-distribution window"));
    connect(showCurrentsWindowAction, &QAction::triggered, this,
        [this] { resultWindowCoordinator_->showCurrents(); });
    auto* showRadiationWindowAction = resultWindowsMenu->addAction(
        workbenchIcon("radiation-pattern"), tr("Radiation"));
    showRadiationWindowAction->setObjectName(QStringLiteral("showRadiationWindowAction"));
    showRadiationWindowAction->setStatusTip(
        tr("Open or raise the live radiation-result window"));
    connect(showRadiationWindowAction, &QAction::triggered, this,
        [this] { resultWindowCoordinator_->showRadiation(); });
    auto* showRawOutputWindowAction = resultWindowsMenu->addAction(
        workbenchIcon("nec-deck"), tr("Raw Solver Output"));
    showRawOutputWindowAction->setObjectName(QStringLiteral("showRawOutputWindowAction"));
    showRawOutputWindowAction->setStatusTip(
        tr("Open or raise the complete output from the latest loaded analysis"));
    connect(showRawOutputWindowAction, &QAction::triggered, this,
        [this] { rawOutputPanel_->showWindow(); });
    resultWindowsMenu->addSeparator();
    auto* showRunHistoryAction = resultWindowsMenu->addAction(
        workbenchIcon("analysis"), tr("Run History"));
    showRunHistoryAction->setObjectName(QStringLiteral("showRunHistoryAction"));
    showRunHistoryAction->setStatusTip(tr("Open the saved analysis-run history"));
    connect(showRunHistoryAction, &QAction::triggered, this, [this] {
        showRunHistory();
    });
    viewMenu->addSeparator();
    auto* projectTreeAction = projectDock_->toggleViewAction();
    projectTreeAction->setText(tr("Project Tree"));
    viewMenu->addAction(projectTreeAction);
    viewMenu->addAction(modelCheckWindowAction_);
    viewMenu->addAction(runMonitorWindowAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(resetLayoutAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(fitGeometryAction_);

    auto* modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->setObjectName(QStringLiteral("modelMenu"));
    auto* geometryEditorAction = modelMenu->addAction(
        workbenchIcon("geometry"), tr("&Geometry Editor"));
    geometryEditorAction->setObjectName(QStringLiteral("geometryEditorAction"));
    connect(geometryEditorAction, &QAction::triggered,
        this, [this] { showModule(geometryModuleIndex_); });
    auto* necDeckAction = modelMenu->addAction(workbenchIcon("nec-deck"), tr("NEC &Deck"));
    necDeckAction->setObjectName(QStringLiteral("necDeckAction"));
    connect(necDeckAction, &QAction::triggered,
        this, [this] { showEditor(EditorDestination::RawSource); });
    for (auto* action : {geometryEditorAction, necDeckAction}) {
        action->setEnabled(modelModuleAction_->isEnabled());
        connect(modelModuleAction_, &QAction::enabledChanged, action, &QAction::setEnabled);
    }
    modelMenu->addSeparator();
    modelMenu->addAction(autoSegmentationAction_);
    modelMenu->addSeparator();
    modelMenu->addAction(geometrySettingsAction_);

    auto* validateMenu = menuBar()->addMenu(tr("&Validate"));
    validateMenu->setObjectName(QStringLiteral("validateMenu"));
    validateMenu->addAction(checkAction_);
    validateMenu->addSeparator();
    validateMenu->addAction(averageGainAction_);
    validateMenu->addAction(convergenceAction_);
    validateMenu->addSeparator();
    auto* openValidationAction = validateMenu->addAction(tr("Open Validation Results"));
    openValidationAction->setObjectName(QStringLiteral("openValidationResultsAction"));
    connect(openValidationAction, &QAction::triggered, this, [this] {
        validationWorkspace_->setCurrentIndex(validationOverviewTabIndex_);
        validationPanel_->showWindow();
    });

    auto* runMenu = menuBar()->addMenu(tr("&Run"));
    runMenu->setObjectName(QStringLiteral("runMenu"));
    runMenu->addAction(runAction_);
    runMenu->addAction(quickSweepAction_);
    runMenu->addAction(stopAction_);
    runMenu->addSeparator();
    auto* reviewRunsAction = runMenu->addAction(tr("Review Run History"));
    reviewRunsAction->setObjectName(QStringLiteral("reviewRunHistoryAction"));
    connect(reviewRunsAction, &QAction::triggered, this, [this] {
        showRunHistory();
    });
    runMenu->addAction(runMonitorWindowAction_);
    auto* openRunFolderAction = runMenu->addAction(tr("Open Run Folder"));
    openRunFolderAction->setObjectName(QStringLiteral("openRunFolderAction"));
    // The run started in this session, otherwise the newest run in the history.
    const auto runFolder = [this] {
        return currentRunDirectory_.isEmpty() ? latestStoredRunDirectory_ : currentRunDirectory_;
    };
    connect(runMenu, &QMenu::aboutToShow, this, [openRunFolderAction, runFolder] {
        const auto directory = runFolder();
        openRunFolderAction->setEnabled(!directory.isEmpty() && QDir(directory).exists());
        openRunFolderAction->setStatusTip(directory);
    });
    connect(openRunFolderAction, &QAction::triggered, this, [runFolder] {
        const auto directory = runFolder();
        if (!directory.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setObjectName(QStringLiteral("helpMenu"));
    helpMenu->addAction(gettingStartedAction_);
    helpMenu->addAction(userGuideAction_);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction_);

}

void MainWindow::resetWorkspaceLayout()
{
    projectDock_->hide();
    modelCheckWindow_->hide();
    runMonitorWindow_->hide();
    statusBar()->showMessage(tr("Workspace layout reset."), 4000);
}

void MainWindow::setWorkspaceDensity(WorkspaceDensity density)
{
    workspaceDensity_ = density;
    QSettings{}.setValue(QStringLiteral("appearance/workspaceDensity"), workspaceDensityId(density));
    applyWorkspaceDensity();

    switch (density) {
    case WorkspaceDensity::Compact: compactDensityAction_->setChecked(true); break;
    case WorkspaceDensity::Standard: standardDensityAction_->setChecked(true); break;
    case WorkspaceDensity::Spacious: spaciousDensityAction_->setChecked(true); break;
    }
}

void MainWindow::applyWorkspaceDensity()
{
    auto roots = QList<QWidget*>{this};
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget != this && qobject_cast<QDialog*>(widget) != nullptr)
            roots.push_back(widget);
    }
    for (auto* root : roots) applyWorkspaceDensity(root);

    auto applicationFont = QApplication::font();
    applicationFont.setPointSize(workspaceDensityFontSize(workspaceDensity_));
    QApplication::setFont(applicationFont);

    const auto controlHeight = workspaceDensity_ == WorkspaceDensity::Compact ? 20
        : workspaceDensity_ == WorkspaceDensity::Standard ? 24 : 28;
    const auto horizontalPadding = workspaceDensity_ == WorkspaceDensity::Compact ? 4
        : workspaceDensity_ == WorkspaceDensity::Standard ? 6 : 8;
    const auto tabPadding = workspaceDensity_ == WorkspaceDensity::Compact ? 3
        : workspaceDensity_ == WorkspaceDensity::Standard ? 5 : 7;
    const auto toolbarHeight = workspaceDensity_ == WorkspaceDensity::Compact ? 24
        : workspaceDensity_ == WorkspaceDensity::Standard ? 29 : 34;
    qApp->setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 2px; padding: 0px 2px; min-height: %1px; }"
        "QToolButton { padding: 1px %2px; }"
        "QPushButton, QComboBox, QLineEdit, QAbstractSpinBox { min-height: %3px; padding: 1px %2px; }"
        "QPushButton[pendingChanges=\"true\"] { background-color: #d98218; color: white; font-weight: 600; }"
        "QTabBar::tab { padding: %4px %5px; }"
        "QHeaderView::section { padding: 2px %2px; }"
        "QTableView::item, QTreeView::item, QListView::item { padding: 1px %2px; }"
        "QDockWidget::title { padding: 2px %2px; }"
        "QStatusBar { min-height: %3px; }"
    ).arg(toolbarHeight).arg(horizontalPadding).arg(controlHeight).arg(tabPadding).arg(tabPadding + 4));
}

void MainWindow::applyWorkspaceDensity(QWidget* root)
{
    if (root == nullptr) return;

    const auto baseFontSize = workspaceDensityFontSize(workspaceDensity_);
    const auto initialBaseFontSize = QApplication::font().pointSize();
    const auto updateFont = [baseFontSize, initialBaseFontSize](QWidget* widget) {
        if (!widget->property("workspaceDensityFontDelta").isValid()) {
            const auto pointSize = widget->font().pointSize();
            if (pointSize > 0)
                widget->setProperty("workspaceDensityFontDelta", pointSize - initialBaseFontSize);
        }
        const auto delta = widget->property("workspaceDensityFontDelta").toInt();
        auto font = widget->font();
        font.setPointSize(std::max(6, baseFontSize + delta));
        widget->setFont(font);
    };
    updateFont(root);
    for (auto* widget : root->findChildren<QWidget*>()) updateFont(widget);

    const auto scale = workspaceDensityScale(workspaceDensity_);
    for (auto* layout : root->findChildren<QLayout*>()) {
        if (!layout->property("workspaceDensityMargins").isValid()) {
            const auto margins = layout->contentsMargins();
            layout->setProperty("workspaceDensityMargins", QVariantList{
                margins.left(), margins.top(), margins.right(), margins.bottom()});
            layout->setProperty("workspaceDensitySpacing", layout->spacing());
        }
        const auto margins = layout->property("workspaceDensityMargins").toList();
        if (margins.size() == 4) {
            layout->setContentsMargins(qRound(margins[0].toInt() * scale),
                qRound(margins[1].toInt() * scale),
                qRound(margins[2].toInt() * scale),
                qRound(margins[3].toInt() * scale));
        }
        const auto spacing = layout->property("workspaceDensitySpacing").toInt();
        if (spacing >= 0) layout->setSpacing(qRound(spacing * scale));
    }

    const auto minimumScale = workspaceDensity_ == WorkspaceDensity::Compact ? 0.75
        : workspaceDensity_ == WorkspaceDensity::Standard ? 0.9 : 1.0;
    const auto updateMinimumSize = [minimumScale](QWidget* widget) {
        if (!widget->property("workspaceDensityMinimumSize").isValid()) {
            const auto minimumSize = widget->minimumSize();
            widget->setProperty("workspaceDensityMinimumSize", QVariantList{
                minimumSize.width(), minimumSize.height()});
        }
        const auto minimumSize = widget->property("workspaceDensityMinimumSize").toList();
        if (minimumSize.size() != 2) return;
        widget->setMinimumSize(qRound(minimumSize[0].toInt() * minimumScale),
            qRound(minimumSize[1].toInt() * minimumScale));
    };
    for (auto* button : root->findChildren<QCommandLinkButton*>())
        updateMinimumSize(button);
    for (auto* view : root->findChildren<GeometryView*>()) updateMinimumSize(view);
    for (auto* view : root->findChildren<Geometry3DView*>()) updateMinimumSize(view);
    for (auto* widget : root->findChildren<QWidget*>()) {
        if (dynamic_cast<Radiation3DView*>(widget) != nullptr)
            updateMinimumSize(widget);
    }

    const auto tableRowHeight = workspaceDensity_ == WorkspaceDensity::Compact ? 21
        : workspaceDensity_ == WorkspaceDensity::Standard ? 24 : 28;
    for (auto* table : root->findChildren<QTableView*>())
        table->verticalHeader()->setDefaultSectionSize(tableRowHeight);

    root->setProperty("workspaceDensityId", workspaceDensityId(workspaceDensity_));
}

void MainWindow::showGettingStarted()
{
    QMessageBox::information(this, tr("Getting Started"),
        tr("1. Create or open a NEC model.\n"
           "2. Edit geometry and electrical model data under Model.\n"
           "3. Run Check Model and resolve blocking errors.\n"
           "4. Configure frequency, sources, environment, requests, and the solver under Setup.\n"
           "5. Run Analysis, then inspect Results."));
}

void MainWindow::openUserGuide()
{
    const QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("../docs/user-guide.md")),
        QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("docs/user-guide.md")),
        QDir::current().filePath(QStringLiteral("docs/user-guide.md")),
    };
    for (const auto& path : candidates) {
        if (!QFileInfo::exists(path)) continue;
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath())))
            return;
    }
    QMessageBox::information(this, tr("User Guide"),
        tr("The local user guide could not be opened.\n"
           "Look for docs/user-guide.md in the NEC Workbench installation or source tree."));
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, tr("About NEC Workbench"),
        tr("NEC Workbench %1\n\n"
           "A cross-platform desktop workbench for creating, checking, analyzing, "
           "visualizing, and optimizing NEC antenna models.\n\n"
           "Licensed under the GNU General Public License version 3 or later.")
            .arg(QStringLiteral(NECWB_DISPLAY_VERSION)));
}

void MainWindow::newModel()
{
    if (!maybeSaveChanges()) {
        return;
    }
    clearDisplayedResults();
    activeModelResultsDirectory_.clear();
    activeModelResultsContext_.clear();
    editor_->setPlainText(tr("CM New NEC Workbench model\nCE\nGE 0\nEN\n"));
    resetSourceUndoHistory();
    hasNecModel_ = true;
    modelModuleAction_->setEnabled(true);
    analysisModuleAction_->setEnabled(true);
    resultsModuleAction_->setEnabled(true);
    dashboardStack_->setCurrentIndex(1);
    analysisRequestEditor_->resetFrequencySelectionForModel();
    setCurrentFile({});
    editor_->document()->setModified(false);
    showEditor(EditorDestination::RawSource);
    checkModel();
}

void MainWindow::openFile()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Open NEC File"), {}, tr("NEC files (*.nec);;All files (*)"));
    if (!path.isEmpty()) openFileAtPath(path);
}

void MainWindow::openFileAtPath(const QString& path)
{
    if (path.isEmpty() || !maybeSaveChanges()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Open Failed"), file.errorString());
        auto files = recentFiles();
        files.removeAll(QFileInfo(path).absoluteFilePath());
        QSettings{}.setValue(QStringLiteral("files/recentModels"), files);
        welcomePage_->setRecentFiles(files);
        return;
    }
    clearDisplayedResults();
    activeModelResultsDirectory_.clear();
    activeModelResultsContext_.clear();
    editor_->setPlainText(QString::fromUtf8(file.readAll()));
    resetSourceUndoHistory();
    hasNecModel_ = true;
    modelModuleAction_->setEnabled(true);
    analysisModuleAction_->setEnabled(true);
    resultsModuleAction_->setEnabled(true);
    dashboardStack_->setCurrentIndex(1);
    analysisRequestEditor_->resetFrequencySelectionForModel();
    setCurrentFile(path);
    editor_->document()->setModified(false);
    showEditor(EditorDestination::RawSource);
    checkModel();
}

void MainWindow::fitWindowToDashboard()
{
    auto* page = dashboardStack_ == nullptr ? nullptr : dashboardStack_->currentWidget();
    if (page == nullptr) return;
    if (isMaximized() || isFullScreen()) showNormal();
    // The window is fixed to its layout (see the constructor), so sizing the central
    // widget to the dashboard sizes the whole window, including any visible dock.
    centralWidget()->setFixedSize(page->sizeHint().expandedTo(page->minimumSizeHint()));
}

void MainWindow::openExample()
{
    QString examplesDirectory;
    for (const auto& candidate : {
             QDir::current().absoluteFilePath(QStringLiteral("examples")),
             QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share/nec-workbench/examples"))}) {
        if (QDir(candidate).exists()) {
            examplesDirectory = candidate;
            break;
        }
    }
    const auto path = QFileDialog::getOpenFileName(this, tr("Open Example NEC Model"),
        examplesDirectory, tr("NEC files (*.nec);;All files (*)"));
    if (!path.isEmpty()) openFileAtPath(path);
}

auto MainWindow::recentFiles() const -> QStringList
{
    const auto stored = QSettings{}.value(QStringLiteral("files/recentModels")).toStringList();
    QStringList result;
    for (const auto& path : stored) {
        const auto absolutePath = QFileInfo(path).absoluteFilePath();
        if (QFileInfo::exists(absolutePath) && !result.contains(absolutePath)) result.append(absolutePath);
        if (result.size() == 8) break;
    }
    return result;
}

void MainWindow::refreshRecentFileViews(const QStringList& files)
{
    if (welcomePage_ != nullptr) welcomePage_->setRecentFiles(files);
    if (controlPanel_ != nullptr) controlPanel_->setRecentModels(files,
        [this](const QString& path) { openFileAtPath(path); },
        [this] { clearRecentFiles(); });
    if (recentModelsMenu_ == nullptr) return;
    recentModelsMenu_->clear();
    if (files.isEmpty()) {
        auto* empty = recentModelsMenu_->addAction(tr("No Recent Models"));
        empty->setEnabled(false);
    } else {
        for (const auto& path : files) {
            const QFileInfo file(path);
            auto* action = recentModelsMenu_->addAction(
                tr("%1 — %2").arg(file.fileName(), file.dir().dirName()));
            action->setData(path);
            action->setToolTip(path);
            connect(action, &QAction::triggered, this,
                [this, path] { openFileAtPath(path); });
        }
    }
    recentModelsMenu_->addSeparator();
    auto* clear = recentModelsMenu_->addAction(tr("Clear Recent Models"));
    clear->setEnabled(!files.isEmpty());
    connect(clear, &QAction::triggered, this, [this] { clearRecentFiles(); });
}

void MainWindow::rememberRecentFile(const QString& path)
{
    if (path.isEmpty()) return;
    auto files = recentFiles();
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    files.removeAll(absolutePath);
    files.prepend(absolutePath);
    while (files.size() > 8) files.removeLast();
    QSettings{}.setValue(QStringLiteral("files/recentModels"), files);
    refreshRecentFileViews(files);
}

void MainWindow::clearRecentFiles()
{
    QSettings{}.remove(QStringLiteral("files/recentModels"));
    refreshRecentFileViews({});
}

auto MainWindow::saveFile() -> bool
{
    return currentFile_.isEmpty() ? saveFileAs() : writeFile(currentFile_);
}

auto MainWindow::saveFileAs() -> bool
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Save NEC File"), currentFile_,
        tr("NEC files (*.nec);;All files (*)"));
    return path.isEmpty() ? false : writeFile(path);
}

auto MainWindow::writeFile(const QString& path) -> bool
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Save Failed"), file.errorString());
        return false;
    }
    file.write(editor_->toPlainText().toUtf8());
    setCurrentFile(path);
    editor_->document()->setModified(false);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

auto MainWindow::maybeSaveChanges() -> bool
{
    if (!confirmCurrentEditorNavigation()) return false;
    if (!editor_->document()->isModified()) {
        return true;
    }
    const auto answer = QMessageBox::warning(this, tr("Unsaved Changes"),
        tr("The NEC model has unsaved changes."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    return answer == QMessageBox::Discard || saveFile();
}

void MainWindow::checkModel(bool presentReport)
{
    const auto& cache = activeSource();
    const auto& document = cache.document();
    const auto& resolution = cache.resolution();
    auto deckUnitInfo = nec::inspectDeckGeometryUnits(document);
    nec::ModelCheckResult result;
    if (resolution.ok()) {
        const auto& resolvedDocument = *cache.resolvedDocument();
        deckUnitInfo = nec::inspectDeckGeometryUnits(resolvedDocument);
        result = nec::NecModelChecker{}.check(resolvedDocument,
            {nec::compatibilityProfileForBackend(solverBackendId_.toStdString()),
                resolution.definitions.size()});
        currentSetup_ = nec::NecSetupConverter{}.convert(resolvedDocument);
    } else {
        for (const auto& diagnostic : resolution.diagnostics) {
            result.diagnostics.push_back({nec::DiagnosticSeverity::Error,
                diagnostic.lineNumber, diagnostic.message});
        }
        currentSetup_ = {};
    }
    currentModel_ = result.model;
    if (pendingTransmissionLineEndpoint_
        && !model::wireSegmentPosition(currentModel_, pendingTransmissionLineEndpoint_->first,
            pendingTransmissionLineEndpoint_->second)) {
        setPendingTransmissionLineEndpoint(std::nullopt);
    }
    modelErrorCount_ = result.errorCount();
    modelWarningCount_ = result.warningCount();
    modelChecked_ = true;

    diagnostics_->clear();
    for (const auto& diagnostic : result.diagnostics) {
        const bool isError = diagnostic.severity == nec::DiagnosticSeverity::Error;
        const bool isWarning = diagnostic.severity == nec::DiagnosticSeverity::Warning;
        auto* item = new QTreeWidgetItem(diagnostics_, {
            isError ? tr("Error") : isWarning ? tr("Warning") : tr("Converted"),
            QString::fromStdString(diagnostic.category),
            QString::number(diagnostic.lineNumber),
            QString::fromStdString(diagnostic.message)});
        item->setData(0, SourceLineRole, static_cast<qulonglong>(diagnostic.lineNumber));
        item->setIcon(0, style()->standardIcon(isError ? QStyle::SP_MessageBoxCritical
            : isWarning ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxInformation));
    }
    if (result.diagnostics.empty()) {
        auto* item = new QTreeWidgetItem(diagnostics_,
            {tr("OK"), tr("Static checks"), {}, tr("No issues found")});
        item->setIcon(0, style()->standardIcon(QStyle::SP_DialogApplyButton));
    }
    const auto adequacyWarnings = std::ranges::count_if(result.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.severity == nec::DiagnosticSeverity::Warning
                && diagnostic.category == "Model adequacy";
        });
    const auto incomplete = currentModel_.empty() || !currentSetup_.frequency
        || currentSetup_.excitations.empty();
    const auto profileName = nec::compatibilityProfileName(
        nec::compatibilityProfileForBackend(solverBackendId_.toStdString()));
    const auto compatibilityName = QString::fromUtf8(
        profileName.data(), static_cast<qsizetype>(profileName.size()));
    validationScope_->setText(tr(
        "Validation target: %1. Static source and geometry findings are shown below; AGT and "
        "convergence results are available in the Validation window.").arg(compatibilityName));
    if (result.errorCount() != 0) {
        validationSummary_->setText(tr("Not ready for %1 · %2 blocking error(s)")
            .arg(compatibilityName)
            .arg(static_cast<qulonglong>(result.errorCount())));
    } else if (incomplete) {
        validationSummary_->setText(tr("Incomplete model · finish geometry and analysis setup"));
    } else if (adequacyWarnings != 0) {
        validationSummary_->setText(tr("Static adequacy: Caution · %1 finding(s) to review")
            .arg(static_cast<qulonglong>(adequacyWarnings)));
    } else {
        validationSummary_->setText(result.informationCount() == 0
                ? tr("Compatible with %1 · static adequacy checks passed").arg(compatibilityName)
                : tr("Compatible with %1 · %2 automatic conversion(s)")
                    .arg(compatibilityName)
                    .arg(static_cast<qulonglong>(result.informationCount())));
    }
    validationOverviewSummary_->setText(validationSummary_->text());
    validationOverviewScope_->setText(tr(
        "Validation target: %1. Open the report for individual findings; use the other tabs "
        "for solver-backed Average Gain Test and convergence studies.").arg(compatibilityName));

    editor_->setDiagnostics(result.diagnostics);
    updateDeckUnitControls(deckUnitInfo);
    std::vector<std::string> parameterNames;
    parameterNames.reserve(resolution.definitions.size());
    for (const auto& definition : resolution.definitions)
        parameterNames.push_back(definition.name);
    const auto parameterFields = nec::necCardFieldsReferencingSymbols(
        cache.source(), parameterNames);
    wireCardEditor_->setParameterControlledFields(parameterFields);
    updateWireCardEditor();
    xyView_->setModel(currentModel_);
    xzView_->setModel(currentModel_);
    yzView_->setModel(currentModel_);
    geometry3DView_->setModel(currentModel_);
    xyView_->setExcitations(currentSetup_.excitations);
    xzView_->setExcitations(currentSetup_.excitations);
    yzView_->setExcitations(currentSetup_.excitations);
    geometry3DView_->setExcitations(currentSetup_.excitations);
    xyView_->setAttachments(currentSetup_.loads, currentSetup_.transmissionLines);
    xzView_->setAttachments(currentSetup_.loads, currentSetup_.transmissionLines);
    yzView_->setAttachments(currentSetup_.loads, currentSetup_.transmissionLines);
    geometry3DView_->setAttachments(currentSetup_.loads, currentSetup_.transmissionLines);
    parameterEditor_->setResolution(resolution);
    setupEditor_->setData(currentModel_, currentSetup_);
    loadNetworkEditor_->setData(currentModel_, currentSetup_);
    analysisRequestEditor_->setData(currentSetup_);
    structuredCardEditor_->setParameterControlledFields(parameterFields);
    structuredCardEditor_->setDocument(document);
    controlPanel_->setModel(currentModel_, currentSetup_, document, solverBackendId_, true,
        modelErrorCount_, modelWarningCount_);
    updateProjectTree(currentModel_, document);
    optimizationWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
        modelErrorCount_ == 0);
    convergenceWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
        modelErrorCount_ == 0);
    const auto checkDetail = tr("Checked: %1 errors, %2 warnings, %3 wires, %4 patches, %5 cards")
        .arg(static_cast<qulonglong>(result.errorCount()))
        .arg(static_cast<qulonglong>(result.warningCount()))
        .arg(static_cast<qulonglong>(currentModel_.wireCount()))
        .arg(static_cast<qulonglong>(currentModel_.surfacePatchCount()))
        .arg(static_cast<qulonglong>(document.cards().size()));
    checkStatus_->setText(tr("Check: %1E · %2W")
        .arg(result.errorCount()).arg(result.warningCount()));
    checkStatus_->setToolTip(checkDetail);
    if (presentReport || result.errorCount() != 0 || result.warningCount() != 0) {
        modelCheckWindow_->show();
        modelCheckWindow_->raise();
        if (presentReport) modelCheckWindow_->activateWindow();
    }
    updateAnalysisReadiness();
}

void MainWindow::showGeneratedSolverDeck()
{
    const auto& resolution = activeSource().resolution();
    if (!resolution.ok()) {
        QMessageBox::warning(this, tr("Generated Solver Deck"),
            tr("The parameterized source cannot be expanded until its formula errors are fixed."));
        return;
    }

    QDialog dialog(modelCheckWindow_);
    dialog.setWindowTitle(tr("Generated Solver Deck — NEC Workbench"));
    dialog.resize(820, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(tr(
        "This numeric base deck is generated from the authored source. NEC Workbench may apply "
        "additional request-specific changes when a run starts; the authored model is unchanged."),
        &dialog);
    explanation->setWordWrap(true);
    auto* source = new QPlainTextEdit(&dialog);
    source->setReadOnly(true);
    source->setLineWrapMode(QPlainTextEdit::NoWrap);
    source->setPlainText(QString::fromStdString(
        analysis::normalizeSolverDeck(resolution.generatedDeck)));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(explanation);
    layout->addWidget(source, 1);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::clearCheckResults()
{
    diagnostics_->clear();
    editor_->setDiagnostics(std::span<const nec::ModelDiagnostic>{});
    validationSummary_->setText(tr("Model changed · run Check Model again"));
    checkStatus_->setText(tr("Model changed — check required"));
    modelChecked_ = false;
    optimizationWorkspace_->setModelValid(false);
    convergenceWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_, false);
    const auto& cache = activeSource();
    auto dashboardModel = currentModel_;
    auto dashboardSetup = currentSetup_;
    if (const auto* resolved = cache.resolvedDocument(); resolved != nullptr) {
        dashboardModel = nec::NecModelConverter{}.convert(*resolved).model;
        dashboardSetup = nec::NecSetupConverter{}.convert(*resolved);
    }
    controlPanel_->setModel(dashboardModel, dashboardSetup, cache.document(),
        solverBackendId_, false, modelErrorCount_, modelWarningCount_);
    averageGainResultsView_->markStale();
    convergenceWorkspace_->markStale();
    controlPanel_->setStudyStatus(ControlPanel::Study::AverageGain,
        ControlPanel::StudyState::Stale);
    controlPanel_->setStudyStatus(ControlPanel::Study::Convergence,
        ControlPanel::StudyState::Stale);
    if (resultsAvailable_) controlPanel_->markResultsStale();
    if (resultsAvailable_) {
        liveResultsStale_ = true;
        setLiveResultStatus(tr("Stale — model changed; run again"));
    }
    updateAnalysisReadiness();
}

void MainWindow::goToDiagnostic(QTreeWidgetItem* item)
{
    const auto lineNumber = item->data(0, SourceLineRole).toULongLong();
    if (lineNumber == 0) {
        return;
    }
    showEditor(EditorDestination::RawSource);
    editor_->goToLine(static_cast<std::size_t>(lineNumber));
}

void MainWindow::activateProjectItem(QTreeWidgetItem* item)
{
    const auto kind = item->data(0, ItemKindRole).toString();
    const auto sourceLine = item->data(0, SourceLineRole).toULongLong();
    if (kind == QStringLiteral("geometry")) {
        showEditor(EditorDestination::Geometry, geometryTabIndex_);
        return;
    }
    if (kind == QStringLiteral("parameters")) {
        showEditor(EditorDestination::Parameters);
        return;
    }
    if (kind == QStringLiteral("excitation")) {
        showEditor(EditorDestination::Sources);
        setupEditor_->selectExcitation(item->data(0, SourceLineRole).toULongLong());
        return;
    }
    if (kind == QStringLiteral("load")) {
        showLoadInEditor(item->data(0, SourceLineRole).toULongLong());
        return;
    }
    if (kind == QStringLiteral("transmissionLine")) {
        showTransmissionLineInEditor(item->data(0, SourceLineRole).toULongLong());
        return;
    }
    if (kind == QStringLiteral("source") || kind == QStringLiteral("wire")) {
        if (kind == QStringLiteral("wire")) {
            showEditor(EditorDestination::RawSource);
            sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
            if (auto* tabs = qobject_cast<QTabWidget*>(wireCardEditor_->parentWidget()))
                tabs->setCurrentWidget(wireCardEditor_);
            wireCardEditor_->selectWire(item->data(0, WireTagRole).toInt());
        } else {
            showEditor(EditorDestination::RawSource);
        }
        if (sourceLine != 0) editor_->goToLine(static_cast<std::size_t>(sourceLine));
        return;
    }
    if (kind != QStringLiteral("card") || sourceLine == 0) return;

    const auto mnemonic = item->data(0, CardMnemonicRole).toString();
    if (mnemonic == QStringLiteral("FR")) {
        showEditor(EditorDestination::Frequency);
        return;
    }
    if (mnemonic == QStringLiteral("SY")) {
        showEditor(EditorDestination::Parameters);
        parameterEditor_->selectParameter(sourceLine);
        return;
    }
    if (mnemonic == QStringLiteral("GN") || mnemonic == QStringLiteral("GE")) {
        showEditor(EditorDestination::Environment);
        return;
    }
    if (mnemonic == QStringLiteral("EX")) {
        showEditor(EditorDestination::Sources);
        setupEditor_->selectExcitation(sourceLine);
        return;
    }
    if (mnemonic == QStringLiteral("LD")) {
        showLoadInEditor(sourceLine);
        return;
    }
    if (mnemonic == QStringLiteral("TL")) {
        showTransmissionLineInEditor(sourceLine);
        return;
    }
    if (mnemonic == QStringLiteral("RP") || mnemonic == QStringLiteral("XQ")) {
        showModule(analysisModuleIndex_);
        analysisWorkspace_->setCurrentIndex(analysisRequestsTabIndex_);
        return;
    }
    if (mnemonic == QStringLiteral("GW")) {
        showEditor(EditorDestination::RawSource);
        sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
        if (auto* tabs = qobject_cast<QTabWidget*>(wireCardEditor_->parentWidget()))
            tabs->setCurrentWidget(wireCardEditor_);
        wireCardEditor_->selectWire(item->data(0, WireTagRole).toInt());
        return;
    }
    if (structuredCardEditor_->selectCard(sourceLine)) {
        showEditor(EditorDestination::RawSource);
        sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
        if (auto* tabs = qobject_cast<QTabWidget*>(structuredCardEditor_->parentWidget()))
            tabs->setCurrentWidget(structuredCardEditor_);
        return;
    }
    showEditor(EditorDestination::RawSource);
    editor_->goToLine(sourceLine);
}

void MainWindow::selectWireInProject(int tag)
{
    synchronizeGeometrySelection(tag);
    wireCardEditor_->selectWire(tag);
    setupEditor_->selectExcitation(0);
    if (tag < 0) {
        projectTree_->clearSelection();
        return;
    }

    if (auto* item = findProjectItem(projectTree_, QStringLiteral("wire"),
            WireTagRole, tag)) {
        projectTree_->setCurrentItem(item);
        synchronizeProjectItemSelection(item);
    }
}

void MainWindow::selectExcitation(std::size_t sourceLine)
{
    xyView_->selectExcitation(sourceLine);
    xzView_->selectExcitation(sourceLine);
    yzView_->selectExcitation(sourceLine);
    geometry3DView_->selectExcitation(sourceLine);
    wireCardEditor_->selectWire(-1);
    setupEditor_->selectExcitation(sourceLine);

    if (auto* item = findProjectItem(projectTree_, QStringLiteral("excitation"),
            SourceLineRole, QVariant::fromValue(static_cast<qulonglong>(sourceLine)))) {
        projectTree_->setCurrentItem(item);
        synchronizeProjectItemSelection(item);
    }
}

void MainWindow::selectLoad(std::size_t sourceLine)
{
    xyView_->selectLoad(sourceLine);
    xzView_->selectLoad(sourceLine);
    yzView_->selectLoad(sourceLine);
    geometry3DView_->selectLoad(sourceLine);
    wireCardEditor_->selectWire(-1);
    setupEditor_->selectExcitation(0);
    loadNetworkEditor_->selectLoad(sourceLine);
    if (auto* item = findProjectItem(projectTree_, QStringLiteral("load"),
            SourceLineRole, QVariant::fromValue(static_cast<qulonglong>(sourceLine))))
        projectTree_->setCurrentItem(item);
}

void MainWindow::selectTransmissionLine(std::size_t sourceLine)
{
    xyView_->selectTransmissionLine(sourceLine);
    xzView_->selectTransmissionLine(sourceLine);
    yzView_->selectTransmissionLine(sourceLine);
    geometry3DView_->selectTransmissionLine(sourceLine);
    wireCardEditor_->selectWire(-1);
    setupEditor_->selectExcitation(0);
    loadNetworkEditor_->selectTransmissionLine(sourceLine);
    if (auto* item = findProjectItem(projectTree_, QStringLiteral("transmissionLine"),
            SourceLineRole, QVariant::fromValue(static_cast<qulonglong>(sourceLine))))
        projectTree_->setCurrentItem(item);
}

void MainWindow::synchronizeGeometrySelection(int tag)
{
    xyView_->selectWire(tag);
    xzView_->selectWire(tag);
    yzView_->selectWire(tag);
    geometry3DView_->selectWire(tag);
}

void MainWindow::fitAllGeometryViews()
{
    xyView_->fitToView();
    xzView_->fitToView();
    yzView_->fitToView();
    geometry3DView_->fitToView();
}

void MainWindow::previewEndpointMove(int tag, model::WireEndpoint endpoint, const model::Point3D& position)
{
    auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr) {
        return;
    }
    (endpoint == model::WireEndpoint::Start ? wire->start : wire->end) = position;
    refreshGeometryViews();
    selectWireInProject(tag);
    checkStatus_->setText(tr("Previewing wire %1 endpoint").arg(tag));
}

void MainWindow::commitEndpointMove(int tag, model::WireEndpoint endpoint,
    const model::Point3D& original, const model::Point3D& updated)
{
    if (original == updated) {
        return;
    }
    auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr || wire->start == wire->end) {
        if (wire != nullptr) {
            (endpoint == model::WireEndpoint::Start ? wire->start : wire->end) = original;
            refreshGeometryViews();
            selectWireInProject(tag);
        }
        return;
    }
    if (wireHasSymbolicGeometry(wire->sourceLine)) {
        (endpoint == model::WireEndpoint::Start ? wire->start : wire->end) = original;
        refreshGeometryViews();
        selectWireInProject(tag);
        showSymbolicGeometryEditBlocked();
        return;
    }
    const auto endpointName = endpoint == model::WireEndpoint::Start ? tr("start") : tr("end");
    (endpoint == model::WireEndpoint::Start ? wire->start : wire->end) = updated;
    const auto source = nec::replaceSourceLine(editor_->toPlainText().toStdString(), wire->sourceLine,
        nec::NecWriter{}.writeWireCard(*wire, deckScaleForSourceLine(wire->sourceLine)));
    if (!source) return;
    pushGeometrySourceEdit(tr("Move wire %1 %2 endpoint").arg(tag).arg(endpointName),
        QString::fromStdString(*source));
    selectWireInProject(tag);
}

void MainWindow::previewWireMove(int tag, const model::Point3D& start, const model::Point3D& end)
{
    auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr) {
        return;
    }
    wire->start = start;
    wire->end = end;
    refreshGeometryViews();
    selectWireInProject(tag);
    checkStatus_->setText(tr("Previewing wire %1 move").arg(tag));
}

void MainWindow::commitWireMove(int tag, const model::Point3D& originalStart,
    const model::Point3D& originalEnd, const model::Point3D& updatedStart,
    const model::Point3D& updatedEnd)
{
    if (originalStart == updatedStart && originalEnd == updatedEnd) {
        return;
    }
    auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr) return;
    if (wireHasSymbolicGeometry(wire->sourceLine)) {
        wire->start = originalStart;
        wire->end = originalEnd;
        refreshGeometryViews();
        selectWireInProject(tag);
        showSymbolicGeometryEditBlocked();
        return;
    }
    wire->start = updatedStart;
    wire->end = updatedEnd;
    const auto source = nec::replaceSourceLine(editor_->toPlainText().toStdString(), wire->sourceLine,
        nec::NecWriter{}.writeWireCard(*wire, deckScaleForSourceLine(wire->sourceLine)));
    if (!source) return;
    pushGeometrySourceEdit(tr("Move wire %1").arg(tag), QString::fromStdString(*source));
    selectWireInProject(tag);
}

void MainWindow::addWire(const model::Point3D& start, const model::Point3D& end)
{
    const auto originalSource = editor_->toPlainText();
    auto lines = originalSource.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto& document = activeSource().document();
    auto insertionIndex = lines.size();
    auto lastGeometryIndex = -1;
    auto hasGeometryEnd = false;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::GeometryWire) {
            lastGeometryIndex = static_cast<int>(card.lineNumber)-1;
        }
        if (card.kind == nec::NecCardKind::GeometryScale
            && insertionIndex == lines.size()) {
            insertionIndex = static_cast<int>(card.lineNumber) - 1;
        }
        if (card.kind == nec::NecCardKind::GeometryEnd) {
            insertionIndex = std::min(insertionIndex,
                static_cast<qsizetype>(card.lineNumber) - 1);
            hasGeometryEnd = true;
            break;
        }
        const auto beginsControlSection = card.kind == nec::NecCardKind::Excitation
            || card.kind == nec::NecCardKind::Load
            || card.kind == nec::NecCardKind::Ground
            || card.kind == nec::NecCardKind::Frequency
            || card.kind == nec::NecCardKind::RadiationPattern
            || card.kind == nec::NecCardKind::Execute
            || card.kind == nec::NecCardKind::TransmissionLine
            || card.kind == nec::NecCardKind::Network
            || card.kind == nec::NecCardKind::End;
        if (beginsControlSection && insertionIndex == lines.size()) {
            insertionIndex = static_cast<int>(card.lineNumber) - 1;
        }
    }

    const model::Wire wire{nextWireTag(), start, end, 11, 0.001, 0};
    if (!hasGeometryEnd && lastGeometryIndex >= 0) insertionIndex = lastGeometryIndex+1;
    lines.insert(insertionIndex, QString::fromStdString(
        nec::NecWriter{}.writeWireCard(wire,
            deckScaleForSourceLine(static_cast<std::size_t>(insertionIndex) + 1))));
    if (!hasGeometryEnd) {
        const auto grounded = currentSetup_.ground
            && currentSetup_.ground->type != model::GroundType::FreeSpace;
        auto geometryEndIndex = lines.size();
        for (auto index = insertionIndex + 1; index < lines.size(); ++index) {
            const auto mnemonic = lines[index].trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
            if (mnemonic == QStringLiteral("EX") || mnemonic == QStringLiteral("LD")
                || mnemonic == QStringLiteral("GN") || mnemonic == QStringLiteral("FR")
                || mnemonic == QStringLiteral("RP") || mnemonic == QStringLiteral("XQ")
                || mnemonic == QStringLiteral("TL") || mnemonic == QStringLiteral("NT")
                || mnemonic == QStringLiteral("EN")) {
                geometryEndIndex = index;
                break;
            }
        }
        lines.insert(geometryEndIndex,
            QString::fromStdString(nec::NecWriter{}.writeGeometryEndCard(grounded ? 1 : 0)));
    }
    pushGeometrySourceEdit(tr("Add wire %1").arg(wire.tag), lines.join(QLatin1Char('\n')));
    selectWireInProject(wire.tag);
}

void MainWindow::splitWire(int tag, const model::Point3D& position)
{
    const auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr || !wire->editable || position == wire->start || position == wire->end) {
        return;
    }
    if (wireHasSymbolicGeometry(wire->sourceLine)) {
        showSymbolicGeometryEditBlocked();
        return;
    }
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(wire->sourceLine) - 1;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        return;
    }

    auto first = *wire;
    auto second = *wire;
    first.end = position;
    second.tag = nextWireTag();
    second.start = position;
    first.segments = std::max(1, wire->segments / 2);
    second.segments = std::max(1, wire->segments - first.segments);
    const nec::NecWriter writer;
    const auto sourceScale = deckScaleForSourceLine(wire->sourceLine);
    lines[lineIndex] = QString::fromStdString(writer.writeWireCard(first, sourceScale));
    lines.insert(lineIndex + 1,
        QString::fromStdString(writer.writeWireCard(second, sourceScale)));
    pushGeometrySourceEdit(tr("Split wire %1").arg(tag), lines.join(QLatin1Char('\n')));
    selectWireInProject(second.tag);
}

void MainWindow::deleteWire(int tag)
{
    const auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr || !wire->editable) {
        return;
    }
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(wire->sourceLine) - 1;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        return;
    }
    lines.removeAt(lineIndex);
    pushGeometrySourceEdit(tr("Delete wire %1").arg(tag), lines.join(QLatin1Char('\n')));
    selectWireInProject(-1);
}

void MainWindow::duplicateWire(int tag)
{
    const auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr || !wire->editable) {
        return;
    }
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(wire->sourceLine) - 1;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        return;
    }
    auto duplicate = *wire;
    duplicate.tag = nextWireTag();
    lines.insert(lineIndex + 1, QString::fromStdString(
        nec::NecWriter{}.writeWireCard(duplicate, deckScaleForSourceLine(wire->sourceLine))));
    pushGeometrySourceEdit(tr("Duplicate wire %1").arg(tag), lines.join(QLatin1Char('\n')));
    selectWireInProject(duplicate.tag);
}

void MainWindow::showWireProperties(int tag)
{
    const auto* selectedWire = currentModel_.wireByTag(tag);
    if (selectedWire == nullptr || !selectedWire->editable) {
        return;
    }
    const auto original = *selectedWire;
    WirePropertiesDialog dialog(original, currentModel_, geometrySettings_.lengthUnit, this);
    if (dialog.exec() == QDialog::Accepted) {
        editWire(original, dialog.wire());
    }
}

void MainWindow::changeFrequency(const model::FrequencyDefinition& frequency)
{
    const auto description = frequency.count <= 1
        ? tr("Change frequency to %1 MHz").arg(formatDecimal(frequency.startMHz))
        : tr("Change frequency sweep to %1–%2 MHz")
            .arg(formatDecimal(frequency.startMHz),
                formatDecimal(model::frequencyEndMHz(frequency)));
    upsertSetupCard(description,
        frequency.sourceLine, QString::fromStdString(nec::NecWriter{}.writeFrequencyCard(frequency)), true);
}

void MainWindow::deleteFrequency(std::size_t sourceLine)
{
    deleteSetupCard(tr("Delete frequency definition"), sourceLine);
}

void MainWindow::changeParameter(std::size_t sourceLine, const QString& originalName,
    const QString& name, const QString& expression)
{
    const auto source = editor_->toPlainText().toStdString();
    const auto updated = sourceLine == 0
        ? std::optional{nec::insertSymbolDefinition(source, name.toStdString(), expression.toStdString())}
        : nec::replaceSymbolDefinition(source, sourceLine, originalName.toStdString(),
            name.toStdString(), expression.toStdString());
    if (!updated) {
        statusBar()->showMessage(tr("The selected SY definition could not be updated."), 5000);
        return;
    }
    const auto resolution = nec::NecSymbolResolver{}.resolve(*updated);
    if (!resolution.ok()) {
        parameterEditor_->showEditError(tr("Change not applied: %1")
            .arg(QString::fromStdString(resolution.diagnostics.front().message)));
        return;
    }
    pushGeometrySourceEdit(sourceLine == 0 ? tr("Add parameter %1").arg(name)
                                          : tr("Edit parameter %1").arg(originalName),
        QString::fromStdString(*updated));
    const auto found = std::ranges::find_if(resolution.definitions, [&name](const auto& definition) {
        return QString::fromStdString(definition.name).compare(name, Qt::CaseInsensitive) == 0;
    });
    if (found != resolution.definitions.end())
        parameterEditor_->selectParameter(found->lineNumber, name);
}

void MainWindow::deleteParameter(std::size_t sourceLine, const QString& name)
{
    const auto updated = nec::removeSymbolDefinition(editor_->toPlainText().toStdString(),
        sourceLine, name.toStdString());
    if (!updated) {
        statusBar()->showMessage(tr("The selected SY definition could not be deleted."), 5000);
        return;
    }
    const auto resolution = nec::NecSymbolResolver{}.resolve(*updated);
    if (!resolution.ok()) {
        parameterEditor_->showEditError(tr("Delete not applied: %1")
            .arg(QString::fromStdString(resolution.diagnostics.front().message)));
        return;
    }
    pushGeometrySourceEdit(tr("Delete parameter %1").arg(name),
        QString::fromStdString(*updated));
}

auto MainWindow::applyOptimizedParameters(
    const std::vector<std::pair<QString, double>>& values) -> bool
{
    if (values.empty()) return false;
    auto source = editor_->toPlainText().toStdString();
    const auto& resolution = activeSource().resolution();
    QStringList names;
    for (const auto& [name, value] : values) {
        const auto found = std::ranges::find_if(resolution.definitions, [&name](const auto& definition) {
            return QString::fromStdString(definition.name).compare(name, Qt::CaseInsensitive) == 0;
        });
        if (found == resolution.definitions.end()) {
            statusBar()->showMessage(
                tr("Parameter %1 no longer exists in the active model.").arg(name), 5000);
            return false;
        }
        const auto updated = nec::replaceSymbolDefinition(source, found->lineNumber,
            found->name, found->name, QString::number(value, 'g', 15).toStdString());
        if (!updated) return false;
        source = *updated;
        names.append(name);
    }
    const auto updatedResolution = nec::NecSymbolResolver{}.resolve(source);
    if (!updatedResolution.ok()) return false;
    if (QString::fromStdString(source) == editor_->toPlainText()) return false;
    pushGeometrySourceEdit(tr("Apply optimized parameters"), QString::fromStdString(source));
    showEditor(EditorDestination::Parameters);
    if (values.size() == 1) {
        const auto found = std::ranges::find_if(updatedResolution.definitions,
            [&values](const auto& definition) {
                return QString::fromStdString(definition.name).compare(
                    values.front().first, Qt::CaseInsensitive) == 0;
            });
        if (found != updatedResolution.definitions.end())
            parameterEditor_->selectParameter(found->lineNumber,
                QString::fromStdString(found->name));
    }
    statusBar()->showMessage(tr("Applied optimized values to %1. Use Undo to restore them.")
        .arg(names.join(QStringLiteral(", "))), 5000);
    return true;
}

void MainWindow::changeGround(const model::GroundDefinition& ground)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const nec::NecWriter writer;
    const auto groundCard = QString::fromStdString(writer.writeGroundCard(ground));
    if (ground.sourceLine != 0) {
        const auto lineIndex = static_cast<int>(ground.sourceLine) - 1;
        if (lineIndex < 0 || lineIndex >= lines.size()) {
            return;
        }
        lines[lineIndex] = groundCard;
    } else {
        const auto& document = activeSource().document();
        auto insertionIndex = lines.size();
        for (const auto& card : document.cards()) {
            if (card.kind == nec::NecCardKind::Excitation
                || card.kind == nec::NecCardKind::Frequency
                || card.kind == nec::NecCardKind::RadiationPattern
                || card.kind == nec::NecCardKind::End) {
                insertionIndex = static_cast<int>(card.lineNumber) - 1;
                break;
            }
        }
        lines.insert(insertionIndex, groundCard);
    }

    if (ground.geometryEndSourceLine != 0) {
        const auto geometryEndIndex = static_cast<int>(ground.geometryEndSourceLine) - 1;
        if (geometryEndIndex >= 0 && geometryEndIndex < lines.size()) {
            lines[geometryEndIndex] = QString::fromStdString(
                writer.writeGeometryEndCard(ground.geometryGroundFlag));
        }
    }
    pushGeometrySourceEdit(tr("Change ground environment"), lines.join(QLatin1Char('\n')));
}

void MainWindow::changeReferenceImpedance(
    const model::ReferenceImpedanceDefinition& reference)
{
    upsertSetupCard(reference.sourceLine == 0
            ? tr("Add reference impedance") : tr("Edit reference impedance"),
        reference.sourceLine,
        QStringLiteral("Z0 %1").arg(reference.ohms, 0, 'g', 15), false);
}

void MainWindow::deleteReferenceImpedance(std::size_t sourceLine)
{
    deleteSetupCard(tr("Remove reference impedance"), sourceLine);
}

void MainWindow::changeExcitation(const model::Excitation& excitation)
{
    upsertSetupCard(excitation.sourceLine == 0
            ? tr("Add source on wire %1").arg(excitation.wireTag)
            : tr("Edit source on wire %1").arg(excitation.wireTag),
        excitation.sourceLine, QString::fromStdString(nec::NecWriter{}.writeExcitationCard(excitation)), false);
}

void MainWindow::changeLoad(const model::LoadDefinition& load)
{
    upsertSetupCard(load.sourceLine == 0 ? tr("Add load") : tr("Edit load"),
        load.sourceLine, QString::fromStdString(nec::NecWriter{}.writeLoadCard(load)), false);
}

void MainWindow::changeTransmissionLine(const model::TransmissionLineDefinition& line)
{
    upsertSetupCard(line.sourceLine == 0 ? tr("Add transmission line") : tr("Edit transmission line"),
        line.sourceLine, QString::fromStdString(nec::NecWriter{}.writeTransmissionLineCard(line)), false);
}

void MainWindow::addExcitationAt(int wireTag, int segment)
{
    changeExcitation({0, wireTag, segment, 1.0, 0.0, 0});
    const auto found = std::ranges::find_if(currentSetup_.excitations.rbegin(),
        currentSetup_.excitations.rend(), [wireTag, segment](const model::Excitation& excitation) {
            return excitation.wireTag == wireTag && excitation.segment == segment;
        });
    if (found != currentSetup_.excitations.rend()) {
        selectExcitation(found->sourceLine);
    }
}

void MainWindow::showExcitationEditor(std::size_t sourceLine)
{
    const auto found = std::ranges::find(currentSetup_.excitations, sourceLine,
        &model::Excitation::sourceLine);
    if (found == currentSetup_.excitations.end()) {
        return;
    }
    ExcitationPropertiesDialog dialog(*found, currentModel_, this);
    if (dialog.exec() == QDialog::Accepted) {
        const auto updated = dialog.excitation();
        if (updated != *found) {
            changeExcitation(updated);
        }
        selectExcitation(sourceLine);
    }
}

void MainWindow::showExcitationInSetup(std::size_t sourceLine)
{
    showEditor(EditorDestination::Sources);
    selectExcitation(sourceLine);
}

void MainWindow::deleteExcitation(std::size_t sourceLine)
{
    deleteSetupCard(tr("Delete voltage source"), sourceLine);
    selectWireInProject(-1);
}

void MainWindow::addLoadAt(int wireTag, int segment)
{
    if (!model::wireSegmentPosition(currentModel_, wireTag, segment)) return;
    changeLoad({4, wireTag, segment, segment, 50.0, 0.0, 0.0, 0});
    const auto found = std::ranges::find_if(currentSetup_.loads.rbegin(), currentSetup_.loads.rend(),
        [wireTag, segment](const model::LoadDefinition& load) {
            return load.wireTag == wireTag && load.firstSegment == segment
                && load.lastSegment == segment;
        });
    if (found != currentSetup_.loads.rend()) selectLoad(found->sourceLine);
}

void MainWindow::chooseTransmissionLineEndpoint(int wireTag, int segment)
{
    if (!model::wireSegmentPosition(currentModel_, wireTag, segment)) return;
    const auto endpoint = std::pair{wireTag, segment};
    if (!pendingTransmissionLineEndpoint_) {
        setPendingTransmissionLineEndpoint(endpoint);
        checkStatus_->setText(tr("Transmission line start: wire %1, segment %2 — right-click the second endpoint")
            .arg(wireTag).arg(segment));
        return;
    }
    if (*pendingTransmissionLineEndpoint_ == endpoint) {
        checkStatus_->setText(tr("Choose a different segment for the second transmission-line endpoint"));
        return;
    }
    const auto first = *pendingTransmissionLineEndpoint_;
    setPendingTransmissionLineEndpoint(std::nullopt);
    changeTransmissionLine({first.first, first.second, wireTag, segment,
        50.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0});
    const auto found = std::ranges::find_if(currentSetup_.transmissionLines.rbegin(),
        currentSetup_.transmissionLines.rend(), [first, endpoint](const auto& line) {
            return line.wireTag1 == first.first && line.segment1 == first.second
                && line.wireTag2 == endpoint.first && line.segment2 == endpoint.second;
        });
    if (found != currentSetup_.transmissionLines.rend()) selectTransmissionLine(found->sourceLine);
}

void MainWindow::setPendingTransmissionLineEndpoint(
    std::optional<std::pair<int, int>> endpoint)
{
    pendingTransmissionLineEndpoint_ = endpoint;
    xyView_->setPendingTransmissionLineEndpoint(endpoint);
    xzView_->setPendingTransmissionLineEndpoint(endpoint);
    yzView_->setPendingTransmissionLineEndpoint(endpoint);
    geometry3DView_->setPendingTransmissionLineEndpoint(endpoint);
}

void MainWindow::showLoadInEditor(std::size_t sourceLine)
{
    showEditor(EditorDestination::Loads);
    selectLoad(sourceLine);
}

void MainWindow::showTransmissionLineInEditor(std::size_t sourceLine)
{
    showEditor(EditorDestination::Loads);
    selectTransmissionLine(sourceLine);
}

void MainWindow::upsertSetupCard(const QString& description, std::size_t sourceLine,
    const QString& cardText, bool frequencyCard)
{
    const auto source = editor_->toPlainText().toStdString();
    if (sourceLine != 0) {
        const auto updated = nec::replaceSourceLine(source, sourceLine, cardText.toStdString());
        if (updated) pushGeometrySourceEdit(description, QString::fromStdString(*updated));
        return;
    }
    const auto& document = activeSource().document();
    auto insertionIndex = nec::sourceLineCount(source);
    for (const auto& card : document.cards()) {
        const bool insertionBoundary = card.kind == nec::NecCardKind::End
            || card.kind == nec::NecCardKind::RadiationPattern
            || (!frequencyCard && card.kind == nec::NecCardKind::Frequency);
        if (insertionBoundary) {
            insertionIndex = card.lineNumber - 1;
            break;
        }
    }
    const auto updated = nec::insertSourceLine(source, insertionIndex, cardText.toStdString());
    if (updated) pushGeometrySourceEdit(description, QString::fromStdString(*updated));
}

void MainWindow::deleteSetupCard(const QString& description, std::size_t sourceLine)
{
    const auto updated = nec::removeSourceLine(
        editor_->toPlainText().toStdString(), sourceLine);
    if (updated) pushGeometrySourceEdit(description, QString::fromStdString(*updated));
}

void MainWindow::changeExecutionRequest(bool enabled,
    const model::ExecutionRequest& execution)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const nec::NecWriter writer;
    if (currentSetup_.executionRequest) {
        const auto index = static_cast<int>(currentSetup_.executionRequest->sourceLine) - 1;
        if (enabled) {
            lines[index] = QString::fromStdString(writer.writeExecutionCard(execution));
        } else {
            lines.removeAt(index);
        }
    } else if (enabled) {
        const auto updatedDocument = nec::NecParser{}.parse(lines.join(QLatin1Char('\n')).toStdString());
        auto insertionIndex = lines.size();
        for (const auto& card : updatedDocument.cards()) {
            if (card.kind == nec::NecCardKind::End) {
                insertionIndex = static_cast<int>(card.lineNumber) - 1;
                break;
            }
        }
        lines.insert(insertionIndex, QString::fromStdString(writer.writeExecutionCard(execution)));
    }
    pushGeometrySourceEdit(tr("Change current and impedance request"),
        lines.join(QLatin1Char('\n')));
}

void MainWindow::changeRadiationPattern(const model::RadiationPatternRequest& pattern)
{
    if (pattern.sourceLine != 0) {
        upsertSetupCard(tr("Edit radiation pattern"), pattern.sourceLine,
            QString::fromStdString(nec::NecWriter{}.writeRadiationPatternCard(pattern)), false);
        return;
    }
    const auto source = editor_->toPlainText().toStdString();
    const auto& document = activeSource().document();
    auto insertionIndex = nec::sourceLineCount(source);
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Execute || card.kind == nec::NecCardKind::End) {
            insertionIndex = card.lineNumber - 1;
            break;
        }
    }
    const auto updated = nec::insertSourceLine(source, insertionIndex,
        nec::NecWriter{}.writeRadiationPatternCard(pattern));
    if (updated) pushGeometrySourceEdit(
        tr("Add radiation pattern"), QString::fromStdString(*updated));
}

void MainWindow::updateAnalysisReadiness()
{
    if (analysisRequestEditor_ == nullptr) {
        return;
    }
    QStringList blockers;
    if (!modelChecked_) {
        blockers.append(tr("Run Check Model after the latest source change."));
    }
    if (currentModel_.empty()) {
        blockers.append(tr("No valid geometry."));
    }
    if (modelErrorCount_ != 0) {
        blockers.append(tr("Model check reports %1 error(s).").arg(static_cast<qulonglong>(modelErrorCount_)));
    }
    if (currentSetup_.excitations.empty()) {
        blockers.append(tr("No supported EX voltage source."));
    }
    auto quickSweepBlockers = blockers;
    if (!currentSetup_.frequency) {
        blockers.append(tr("No supported FR frequency definition."));
    }
    auto averageGainBlockers = blockers;
    if (!currentSetup_.executionRequest && currentSetup_.radiationPatterns.empty()) {
        blockers.append(tr("No XQ or RP result request."));
    }
    if (!analysis::isBackendRunnable(solverBackendId_.toStdString())) {
        blockers.append(tr("The selected backend does not have a process adapter yet."));
        quickSweepBlockers.append(tr("The selected backend does not have a process adapter yet."));
    }
    const QFileInfo executable(solverExecutablePath_);
    if (solverExecutablePath_.isEmpty()) {
        blockers.append(tr("No solver executable selected."));
        quickSweepBlockers.append(tr("No solver executable selected."));
    } else if (!executable.exists() || !executable.isFile() || !executable.isExecutable()) {
        blockers.append(tr("Solver executable path is not runnable."));
        quickSweepBlockers.append(tr("Solver executable path is not runnable."));
    }
    if (const auto capacity = analysis::solverSegmentCapacity(
            solverBackendId_.toStdString(), solverExecutablePath_.toStdString())) {
        const auto segments = modelSegmentCount(currentModel_);
        if (segments > *capacity) {
            const auto message = tr(
                "The selected NEC2dXS executable supports %1 segments, but this model has %2.")
                                     .arg(*capacity).arg(segments);
            blockers.append(message);
            quickSweepBlockers.append(message);
            averageGainBlockers.append(message);
        }
    }
    if (optimizationWorkspace_->isRunning()) {
        blockers.append(tr("An optimization sweep is running."));
        quickSweepBlockers.append(tr("An optimization sweep is running."));
        averageGainBlockers.append(tr("An optimization sweep is running."));
    }
    if (convergenceWorkspace_->isRunning()) {
        blockers.append(tr("A segmentation convergence study is running."));
        quickSweepBlockers.append(tr("A segmentation convergence study is running."));
        averageGainBlockers.append(tr("A segmentation convergence study is running."));
    }
    if (!analysis::isBackendRunnable(solverBackendId_.toStdString())) {
        averageGainBlockers.append(tr("The selected backend does not have a process adapter yet."));
    }
    if (solverExecutablePath_.isEmpty()) {
        averageGainBlockers.append(tr("No solver executable selected."));
    } else if (!executable.exists() || !executable.isFile() || !executable.isExecutable()) {
        averageGainBlockers.append(tr("Solver executable path is not runnable."));
    }
    analysisRequestEditor_->setReadiness(blockers);
    runAction_->setEnabled(blockers.empty() && !solverRunner_->isRunning());
    quickSweepAction_->setEnabled(quickSweepBlockers.empty() && !solverRunner_->isRunning());
    averageGainAction_->setEnabled(averageGainBlockers.empty() && !solverRunner_->isRunning());
    convergenceAction_->setEnabled(modelChecked_ && modelErrorCount_ == 0
        && !solverRunner_->isRunning() && !optimizationWorkspace_->isRunning()
        && !convergenceWorkspace_->isRunning());
    optimizeModuleAction_->setEnabled(modelChecked_ && modelErrorCount_ == 0);
}

void MainWindow::synchronizeRunnerState()
{
    stopAction_->setEnabled(solverRunner_->isRunning() || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning());
    optimizationWorkspace_->setExternalRunActive(
        solverRunner_->isRunning() || convergenceWorkspace_->isRunning());
    convergenceWorkspace_->setExternalRunActive(
        solverRunner_->isRunning() || optimizationWorkspace_->isRunning());
    updateAnalysisReadiness();
}

void MainWindow::showConvergenceStudy()
{
    validationWorkspace_->setCurrentIndex(convergenceValidationTabIndex_);
    validationPanel_->showWindow();
}

void MainWindow::startAnalysis()
{
    if (solverRunner_->isRunning() || !runAction_->isEnabled()) {
        return;
    }

    checkModel();
    if (modelErrorCount_ != 0 || !runAction_->isEnabled()) {
        statusBar()->showMessage(tr("Solve canceled: model validation found blocking errors."), 5000);
        return;
    }

    const auto& cache = activeSource();
    const auto source = std::string(cache.source());
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) {
        QMessageBox::critical(this, tr("Run Failed"),
            tr("The parameterized source could not be resolved."));
        return;
    }
    const auto generatedDocument = nec::NecParser{}.parse(resolution.generatedDeck);
    const auto generatedSetup = nec::NecSetupConverter{}.convert(generatedDocument);
    const auto solverInput = analysis::prepareSolverInput(
        resolution.generatedDeck, generatedSetup,
        analysisRequestEditor_->radiationFrequencyPlan());
    startAnalysisRun(source, solverInput, QStringLiteral("analysis"), {}, tr("NEC analysis"));
}

void MainWindow::startQuickFrequencySweep()
{
    if (solverRunner_->isRunning() || !quickSweepAction_->isEnabled()) return;
    checkModel();
    if (modelErrorCount_ != 0 || !quickSweepAction_->isEnabled()) {
        statusBar()->showMessage(
            tr("Quick sweep canceled: model validation found blocking errors."), 5000);
        return;
    }

    const auto initial = currentSetup_.frequency.value_or(model::FrequencyDefinition{});
    QuickSweepDialog dialog(initial, this, !currentSetup_.radiationPatterns.empty());
    if (dialog.exec() != QDialog::Accepted) return;

    const auto& cache = activeSource();
    const auto source = std::string(cache.source());
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) {
        QMessageBox::critical(this, tr("Quick Sweep Failed"),
            tr("The parameterized source could not be resolved."));
        return;
    }
    const auto sweep = dialog.frequencyDefinition();
    const auto includeRadiation = dialog.includeRadiationPatterns();
    const auto solverInput = analysis::prepareFrequencySweepInput(
        resolution.generatedDeck, sweep, includeRadiation);
    const auto summary = tr("%1–%2 MHz · %3 points · %4")
        .arg(formatDecimal(sweep.startMHz), formatDecimal(model::frequencyEndMHz(sweep)))
        .arg(sweep.count)
        .arg(includeRadiation ? tr("with RP") : tr("impedance/current"));
    startAnalysisRun(source, solverInput, QStringLiteral("quick-sweep"), summary,
        tr("Quick frequency sweep"));
}

auto MainWindow::startOptimizationPeakSearch(double frequencyMHz) -> bool
{
    if (solverRunner_->isRunning() || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning()
        || !std::isfinite(frequencyMHz) || frequencyMHz <= 0.0) return false;
    checkModel();
    const QFileInfo executable(solverExecutablePath_);
    if (modelErrorCount_ != 0 || currentModel_.empty()
        || currentSetup_.excitations.empty()
        || !analysis::isBackendRunnable(solverBackendId_.toStdString())
        || !executable.exists() || !executable.isFile() || !executable.isExecutable()) {
        statusBar()->showMessage(tr(
            "Maximum-gain search canceled: check the model and selected solver."), 5000);
        return false;
    }

    const auto& cache = activeSource();
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) {
        statusBar()->showMessage(tr(
            "Maximum-gain search canceled: model expressions could not be resolved."), 5000);
        return false;
    }
    const auto aboveGroundOnly = currentSetup_.ground
        && currentSetup_.ground->type != model::GroundType::FreeSpace;
    const auto solverInput = analysis::prepareMaximumGainSearchInput(
        resolution.generatedDeck, frequencyMHz, aboveGroundOnly);
    currentPeakSearchFrequencyMHz_ = frequencyMHz;
    const auto summary = tr("Maximum gain at %1 MHz").arg(formatDecimal(frequencyMHz));
    startAnalysisRun(std::string(cache.source()), solverInput,
        QStringLiteral("optimization-peak-search"), summary,
        tr("Finding maximum gain"), SolverRunPurpose::OptimizationPeakSearch);
    return solverRunner_->isRunning();
}

void MainWindow::startAnalysisRun(const std::string& authoredSource,
    const std::string& solverInput, const QString& runType, const QString& summary,
    const QString& activity, SolverRunPurpose purpose)
{
    const auto backendSolverInput = analysis::formatSolverInputForBackend(
        solverInput, solverBackendId_.toStdString());
    currentRunPurpose_ = purpose;
    currentRunRecord_ = runStore_.create(solverBackendId_, currentFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : currentFile_, runType);
    currentRunRecord_->summary = summary;
    currentRunRecord_->modelFingerprint = QString::fromStdString(
        analysis::optimizationModelFingerprint(solverInput));
    runStore_.save(*currentRunRecord_);
    currentRunDirectory_ = currentRunRecord_->directory;
    if (!QFileInfo::exists(currentRunDirectory_)) {
        QMessageBox::critical(this, tr("Run Failed"), tr("Could not create the solver run folder."));
        currentRunRecord_.reset();
        return;
    }

    const auto inputPath = QDir(currentRunDirectory_).filePath(QStringLiteral("model.nec"));
    const auto sourcePath = QDir(currentRunDirectory_).filePath(QStringLiteral("model.source.nec"));
    currentRunOutputPath_ = QDir(currentRunDirectory_).filePath(QStringLiteral("model.out"));
    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Run Failed"), sourceFile.errorString());
        currentRunRecord_.reset();
        return;
    }
    sourceFile.write(QByteArray::fromStdString(authoredSource));
    sourceFile.close();

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Run Failed"), inputFile.errorString());
        currentRunRecord_.reset();
        return;
    }
    inputFile.write(QByteArray::fromStdString(backendSolverInput));
    inputFile.close();

    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(solverBackendId_.toStdString(),
            solverExecutablePath_.toStdString(), "model.nec", "model.out");
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Run Failed"), QString::fromLocal8Bit(error.what()));
        currentRunRecord_.reset();
        return;
    }

    startSolverProcess(command, activity);
}

void MainWindow::startAverageGainTest()
{
    if (solverRunner_->isRunning() || !averageGainAction_->isEnabled()) return;
    checkModel();
    if (modelErrorCount_ != 0 || !averageGainAction_->isEnabled()) {
        statusBar()->showMessage(tr("AGT canceled: model validation found blocking errors."), 5000);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Average Gain Test"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(tr(
        "The solver runs a temporary lossless copy of this model at one frequency. Wire, ground, "
        "load, network, and transmission-line losses are removed; the authored NEC source is unchanged."),
        &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* form = new QFormLayout;
    auto* frequency = new QDoubleSpinBox(&dialog);
    frequency->setDecimals(DisplayDecimalPlaces);
    frequency->setRange(0.000001, 1.0e9);
    frequency->setSuffix(tr(" MHz"));
    frequency->setValue(currentSetup_.frequency->startMHz);
    auto* environment = new QComboBox(&dialog);
    environment->addItem(tr("Free space — full sphere, expected AGT 1.0"),
        static_cast<int>(analysis::AverageGainEnvironment::FreeSpace));
    environment->addItem(tr("Perfect ground — hemisphere, expected AGT 2.0"),
        static_cast<int>(analysis::AverageGainEnvironment::PerfectGround));
    if (currentSetup_.ground
        && currentSetup_.ground->type != model::GroundType::FreeSpace) {
        environment->setCurrentIndex(1);
    }
    form->addRow(tr("Frequency:"), frequency);
    form->addRow(tr("Environment:"), environment);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Run AGT"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    currentRunPurpose_ = SolverRunPurpose::AverageGainTest;
    currentAverageGainFrequencyMHz_ = frequency->value();
    currentAverageGainEnvironment_ = static_cast<analysis::AverageGainEnvironment>(
        environment->currentData().toInt());
    currentRunRecord_ = runStore_.create(solverBackendId_, currentFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : currentFile_, QStringLiteral("average-gain-test"));
    currentRunDirectory_ = currentRunRecord_->directory;
    if (!QFileInfo::exists(currentRunDirectory_)) {
        QMessageBox::critical(this, tr("AGT Failed"), tr("Could not create the solver run folder."));
        currentRunRecord_.reset();
        return;
    }

    const auto inputPath = QDir(currentRunDirectory_).filePath(QStringLiteral("model.nec"));
    const auto sourcePath = QDir(currentRunDirectory_).filePath(QStringLiteral("model.source.nec"));
    currentRunOutputPath_ = QDir(currentRunDirectory_).filePath(QStringLiteral("model.out"));
    const auto& cache = activeSource();
    const auto source = std::string(cache.source());
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) {
        QMessageBox::critical(this, tr("AGT Failed"),
            tr("The parameterized source could not be resolved."));
        currentRunRecord_.reset();
        return;
    }
    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("AGT Failed"), sourceFile.errorString());
        currentRunRecord_.reset();
        return;
    }
    sourceFile.write(QByteArray::fromStdString(source));
    sourceFile.close();

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("AGT Failed"), inputFile.errorString());
        currentRunRecord_.reset();
        return;
    }
    const auto averageGainInput = analysis::prepareAverageGainTestInput(
        resolution.generatedDeck, currentAverageGainFrequencyMHz_, currentAverageGainEnvironment_);
    inputFile.write(QByteArray::fromStdString(analysis::formatSolverInputForBackend(
        averageGainInput, solverBackendId_.toStdString())));
    inputFile.close();
    writeAverageGainMetadata(currentRunDirectory_, currentAverageGainFrequencyMHz_,
        currentAverageGainEnvironment_);

    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(solverBackendId_.toStdString(),
            solverExecutablePath_.toStdString(), "model.nec", "model.out");
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("AGT Failed"), QString::fromLocal8Bit(error.what()));
        currentRunRecord_.reset();
        return;
    }
    startSolverProcess(command, tr("Average Gain Test"));
}

void MainWindow::startSolverProcess(const analysis::SolverCommand& command, const QString& activity)
{
    if (!currentRunRecord_) return;
    addRunRecord(*currentRunRecord_, true);
    refreshDashboardRuns();
    currentRunRow_ = 0;
    analysisRuns_->selectRow(0);

    solverOutput_->clear();
    analysisOutput_->clear();
    rawOutputFrequencyControl_->clear();
    rawOutputFrequencyControl_->setEnabled(false);
    const auto program = QString::fromStdString(command.executable);
    QStringList arguments;
    for (const auto& argument : command.arguments) {
        arguments.append(QString::fromStdString(argument));
    }
    currentSolverCommand_ = tr("%1 %2").arg(program, arguments.join(QLatin1Char(' ')));
    auto standardInputDescription = QString{};
    if (!command.standardInput.empty()) {
        const auto names = QString::fromStdString(command.standardInput)
                               .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        standardInputDescription = names.size() >= 2
            ? tr("\nStandard input: %1 → %2").arg(names[0], names[1])
            : tr("\nStandard input supplied");
        currentSolverCommand_ += standardInputDescription;
    }
    appendSolverOutput(tr("%1\nRun folder: %2\nCommand: %3 %4\n\n")
        .arg(runContext(*currentRunRecord_), currentRunDirectory_, program,
            arguments.join(QLatin1Char(' '))) + standardInputDescription + QStringLiteral("\n"));
    runMonitorContextLabel_->setText(tr("%1\nRun folder: %2\nCommand: %3")
        .arg(runContext(*currentRunRecord_), currentRunDirectory_, currentSolverCommand_));
    runMonitorProgress_->setRange(0, 0);
    runMonitorStopButton_->setEnabled(true);
    showRunMonitor(false);

    solverActivityName_ = activity;
    solverActivityPhase_ = tr("Starting solver");
    solverElapsed_.start();
    updateSolverActivity();
    if (solverActivityTimer_ == nullptr) {
        solverActivityTimer_ = new QTimer(this);
        solverActivityTimer_->setInterval(250);
        connect(solverActivityTimer_, &QTimer::timeout,
            this, [this] { updateSolverActivity(); });
    }
    solverActivityTimer_->start();
    cancelRunButton_->setEnabled(true);
    openRunFolderButton_->setEnabled(true);
    runAction_->setEnabled(false);
    if (currentRunPurpose_ == SolverRunPurpose::Analysis)
        setLiveResultStatus(tr("Running…"));
    if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
        averageGainResultsView_->setRunning(currentAverageGainFrequencyMHz_,
            currentAverageGainEnvironment_, runContext(*currentRunRecord_));
        controlPanel_->setStudyStatus(ControlPanel::Study::AverageGain,
            ControlPanel::StudyState::Running,
            tr("running @ %1 MHz").arg(formatDecimal(currentAverageGainFrequencyMHz_)));
        validationWorkspace_->setCurrentIndex(averageGainValidationTabIndex_);
        validationPanel_->showWindow();
    }
    updateRunSelectionActions();
    statusBar()->showMessage(tr("Running %1…").arg(activity));
    solverRunner_->start(command, currentRunDirectory_, solverTimeoutSeconds_);
    synchronizeRunnerState();
}

void MainWindow::cancelAnalysis()
{
    if (!solverRunner_->isRunning()) return;
    stopAction_->setEnabled(false);
    solverActivityPhase_ = tr("Canceling");
    runMonitorStopButton_->setEnabled(false);
    updateSolverActivity();
    appendSolverOutput(tr("\nCancellation requested; stopping solver.\n"));
    solverRunner_->cancel();
}

void MainWindow::appendSolverOutput(const QString& text)
{
    if (text.isEmpty()) {
        return;
    }
    const auto appendToEditor = [&text](QPlainTextEdit* output) {
        output->moveCursor(QTextCursor::End);
        output->insertPlainText(text);
        output->moveCursor(QTextCursor::End);
    };
    appendToEditor(solverOutput_);
    if (!currentRunDirectory_.isEmpty()) {
        QFile logFile(QDir(currentRunDirectory_).filePath(QStringLiteral("run.log")));
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            logFile.write(text.toUtf8());
        }
    }
}

void MainWindow::showRunMonitor(bool manuallyRequested)
{
    if (runMonitorWindow_ == nullptr) return;
    if (manuallyRequested) runMonitorPinnedOpen_ = true;
    if (runMonitorAutoHideTimer_ != nullptr) runMonitorAutoHideTimer_->stop();
    if (runMonitorWindow_->isMinimized()) runMonitorWindow_->showNormal();
    else runMonitorWindow_->show();
    runMonitorWindow_->raise();
    runMonitorWindow_->activateWindow();
}

void MainWindow::updateSolverActivity()
{
    if (runMonitorStatusLabel_ == nullptr) return;
    auto detail = tr("%1 · %2 · %3 s")
        .arg(solverActivityName_, solverActivityPhase_)
        .arg(formatDecimal(solverElapsed_.isValid() ? solverElapsed_.elapsed() / 1000.0 : 0.0));
    const QFileInfo output(currentRunOutputPath_);
    if (output.exists() && output.size() > 0)
        detail += tr(" · output %1").arg(formatByteSize(output.size()));
    runMonitorStatusLabel_->setText(detail);
}

void MainWindow::completeSolverActivity(const QString& status)
{
    if (solverActivityTimer_ != nullptr) solverActivityTimer_->stop();
    solverActivityPhase_ = status;
    runMonitorProgress_->setRange(0, 1);
    runMonitorProgress_->setValue(status == tr("Completed") ? 1 : 0);
    runMonitorStopButton_->setEnabled(false);
    updateSolverActivity();
    if (status == tr("Completed") && !runMonitorPinnedOpen_
        && runMonitorAutoHideTimer_ != nullptr) {
        runMonitorAutoHideTimer_->start();
    }
}

void MainWindow::finishAnalysis(SolverProcessResult processResult)
{
    const auto processSucceeded = processResult.status == SolverProcessStatus::Succeeded;
    if (processResult.status == SolverProcessStatus::FailedToStart)
        appendSolverOutput(tr("\nFailed to start solver: %1\n").arg(processResult.detail));
    else if (processResult.status == SolverProcessStatus::TimedOut)
        appendSolverOutput(tr("\nRun exceeded the %1-second timeout; solver stopped.\n")
            .arg(solverTimeoutSeconds_));
    solverActivityPhase_ = processSucceeded ? tr("Parsing results") : tr("Finishing run");
    updateSolverActivity();

    auto averageGainParsed = false;
    auto peakGainParsed = false;
    auto optimizationBaselineParsed = false;
    auto analysisResultsAvailable = false;
    QFile outputFile(currentRunOutputPath_);
    if (processSucceeded && outputFile.open(QIODevice::ReadOnly)) {
        const auto outputBytes = outputFile.readAll();
        analysisOutput_->setPlainText(QString::fromLocal8Bit(outputBytes));
        auto result = analysis::NecOutputParser{}.parse(std::string_view(
            outputBytes.constData(), static_cast<std::size_t>(outputBytes.size())));
        // A baseline is judged against the study's reference, which may differ from the model's.
        result.referenceImpedanceOhms = currentRunPurpose_ == SolverRunPurpose::OptimizationBaseline
            ? currentBaselineReferenceImpedanceOhms_
            : model::referenceImpedanceOhms(currentSetup_);
        if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
            if (!result.averagePowerGain) {
                if (const auto integrated = analysis::integrateAverageGain(
                        result.radiation, currentAverageGainEnvironment_)) {
                    result.averagePowerGain = integrated->averagePowerGain;
                    result.averagingSolidAnglePi = integrated->solidAnglePi;
                    appendSolverOutput(tr(
                        "\nThe solver did not report an AGT summary; NEC Workbench integrated "
                        "the requested radiation grid.\n"));
                }
            }
            const auto context = runContext(*currentRunRecord_);
            currentRunRecord_->outputBytes = outputBytes.size();
            currentRunRecord_->frequencyCount = 1;
            if (result.averagePowerGain) {
                const auto expected = currentAverageGainEnvironment_
                        == analysis::AverageGainEnvironment::PerfectGround
                    ? 2.0 : 1.0;
                const auto assessment = analysis::assessAverageGain(*result.averagePowerGain, expected);
                averageGainParsed = true;
                currentRunRecord_->summary = tr("AGT %1 · %2")
                    .arg(formatDecimal(assessment.normalizedGain))
                    .arg(averageGainClassificationName(assessment.classification));
                averageGainResultsView_->setResult(assessment, currentAverageGainFrequencyMHz_,
                    currentAverageGainEnvironment_, result.averagingSolidAnglePi, context);
                controlPanel_->setStudyStatus(ControlPanel::Study::AverageGain,
                    ControlPanel::StudyState::Complete,
                    tr("%1 @ %2 MHz").arg(formatDecimal(assessment.normalizedGain),
                        formatDecimal(currentAverageGainFrequencyMHz_)));
                writeAverageGainMetadata(currentRunDirectory_, currentAverageGainFrequencyMHz_,
                    currentAverageGainEnvironment_, assessment, result.averagingSolidAnglePi);
                displayedRunDirectory_ = currentRunDirectory_;
                validationPanel_->setStatusText(tr("Average Gain Test — %1").arg(context));
                validationWorkspace_->setCurrentIndex(averageGainValidationTabIndex_);
                validationPanel_->showWindow();
                const auto expectedSolidAngle = currentAverageGainEnvironment_
                        == analysis::AverageGainEnvironment::PerfectGround
                    ? 2.0 : 4.0;
                if (result.averagingSolidAnglePi
                    && std::abs(*result.averagingSolidAnglePi - expectedSolidAngle) > 0.01) {
                    appendSolverOutput(tr("\nWarning: solver reported a %1π averaging solid angle; expected %2π.\n")
                        .arg(formatDecimal(*result.averagingSolidAnglePi),
                            formatDecimal(expectedSolidAngle)));
                }
            } else {
                averageGainResultsView_->setFailure(
                    tr("The solver output contains no AVERAGE POWER GAIN value."), context);
                controlPanel_->setStudyStatus(ControlPanel::Study::AverageGain,
                    ControlPanel::StudyState::Failed, {},
                    tr("No average power gain was reported."));
            }
            analysisRuns_->setItem(currentRunRow_, RunResultsColumn,
                new QTableWidgetItem(runResultsText(*currentRunRecord_)));
            analysisRuns_->setItem(currentRunRow_, RunOutputSizeColumn,
                new QTableWidgetItem(formatByteSize(outputBytes.size())));
        } else if (currentRunRecord_) {
            setRunResultMetadata(*currentRunRecord_, result, outputBytes.size());
            if (currentRunRow_ >= 0) {
                analysisRuns_->setItem(currentRunRow_, RunResultsColumn,
                    new QTableWidgetItem(runResultsText(*currentRunRecord_)));
                analysisRuns_->setItem(currentRunRow_, RunOutputSizeColumn,
                    new QTableWidgetItem(formatByteSize(currentRunRecord_->outputBytes)));
            }
        }
        if (currentRunPurpose_ == SolverRunPurpose::Analysis) {
            const auto context = runContext(*currentRunRecord_);
            analysisResultsView_->setResults(result, context);
            visualizePlotsView_->setResults(result, context);
            currentResultsView_->setModel(currentModel_);
            currentResultsView_->setResults(result, context);
            radiationPatternView_->setResults(result, context);
            radiation3DView_->setModel(currentModel_);
            radiation3DView_->setResults(result, context);
            radiationPerformanceView_->setResults(result, context);
            setDisplayedResults(result);
            optimizationWorkspace_->setLatestAnalysisResult(result);
            displayedRunDirectory_ = currentRunDirectory_;
            activeModelResultsDirectory_ = currentRunDirectory_;
            activeModelResultsContext_ = context;
            controlPanel_->setResults(result, context, false);
            controlPanel_->setModel(currentModel_, currentSetup_, activeSource().document(),
                solverBackendId_, true, modelErrorCount_, modelWarningCount_);
            resultsAvailable_ = true;
            liveResultsStale_ = false;
            setLiveResultStatus(tr("Current"));
            analysisResultsAvailable = !result.feedpoints.empty() || !result.currents.empty()
                || !result.radiation.empty();
            if (analysisResultsAvailable)
                resultWindowCoordinator_->presentCompletedAnalysis(result);
            if (analysisResultsAvailable && currentRunRecord_)
                optimizationWorkspace_->setOptimizationBaseline(*currentRunRecord_, result);
        } else if (currentRunPurpose_ == SolverRunPurpose::OptimizationBaseline) {
            optimizationBaselineParsed = !result.feedpoints.empty() || !result.radiation.empty();
            if (optimizationBaselineParsed && currentRunRecord_)
                optimizationWorkspace_->setOptimizationBaseline(*currentRunRecord_, result);
        } else if (currentRunPurpose_ == SolverRunPurpose::OptimizationPeakSearch) {
            const auto peak = analysis::radiationPeakAtNearestFrequency(
                result.radiation, analysis::RadiationComponent::Total,
                currentPeakSearchFrequencyMHz_);
            if (peak && peak->sampleCount > 1) {
                peakGainParsed = true;
                optimizationWorkspace_->setLatestAnalysisResult(result);
                currentRunRecord_->summary = tr(
                    "Maximum %1 dBi at elevation %2°, azimuth %3° · %4 MHz")
                    .arg(formatDecimal(peak->metrics.peakGainDb))
                    .arg(formatDecimal(90.0 - peak->metrics.peakThetaDegrees))
                    .arg(formatDecimal(peak->metrics.peakPhiDegrees))
                    .arg(formatDecimal(peak->frequencyMHz));
                runStore_.save(*currentRunRecord_);
            } else {
                appendSolverOutput(tr(
                    "\nThe maximum-gain search produced no usable multi-angle radiation pattern.\n"));
            }
        }
    } else if (processSucceeded && currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
        const auto message = tr("The solver did not produce a readable model.out file.");
        averageGainResultsView_->setFailure(message, runContext(*currentRunRecord_));
        controlPanel_->setStudyStatus(ControlPanel::Study::AverageGain,
            ControlPanel::StudyState::Failed, {}, message);
    } else if (currentRunRecord_) {
        if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
            const auto message = processResult.detail.isEmpty()
                ? tr("The solver did not complete the Average Gain Test.")
                : processResult.detail;
            averageGainResultsView_->setFailure(message, runContext(*currentRunRecord_));
            controlPanel_->setStudyStatus(ControlPanel::Study::AverageGain,
                ControlPanel::StudyState::Failed, {}, message);
        }
        currentRunRecord_->outputBytes = QFileInfo(currentRunOutputPath_).size();
        if (currentRunRow_ >= 0) {
            analysisRuns_->setItem(currentRunRow_, RunOutputSizeColumn,
                new QTableWidgetItem(formatByteSize(currentRunRecord_->outputBytes)));
        }
        analysisOutput_->setPlainText(tr(
            "This run did not complete successfully. Its partial solver output was not loaded "
            "or parsed, preventing large canceled runs from blocking the interface.\n\n"
            "Run folder: %1").arg(currentRunDirectory_));
    }

    const auto runCompleted = processSucceeded
        && (currentRunPurpose_ != SolverRunPurpose::AverageGainTest || averageGainParsed)
        && (currentRunPurpose_ != SolverRunPurpose::OptimizationPeakSearch || peakGainParsed)
        && (currentRunPurpose_ != SolverRunPurpose::OptimizationBaseline
            || optimizationBaselineParsed);
    QString status;
    if (processResult.status == SolverProcessStatus::TimedOut) {
        status = tr("Timed out");
    } else if (processResult.status == SolverProcessStatus::Canceled) {
        status = tr("Canceled");
    } else if (runCompleted) {
        status = tr("Completed");
    } else if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest
        && processSucceeded) {
        status = tr("Failed (AGT result missing)");
    } else if (currentRunPurpose_ == SolverRunPurpose::OptimizationPeakSearch
        && processSucceeded) {
        status = tr("Failed (maximum gain missing)");
    } else if (currentRunPurpose_ == SolverRunPurpose::OptimizationBaseline
        && processSucceeded) {
        status = tr("Failed (baseline results missing)");
    } else if (processResult.status == SolverProcessStatus::FailedToStart) {
        status = tr("Failed to start");
    } else {
        status = tr("Failed (exit %1)").arg(processResult.exitCode);
    }
    if (currentRunPurpose_ == SolverRunPurpose::Analysis
        && (!runCompleted || !analysisResultsAvailable)) {
        setLiveResultStatus(!resultsAvailable_ ? tr("No results")
            : liveResultsStale_ ? tr("Stale — previous results retained")
                                : tr("Previous results retained"));
    }
    setCurrentRunStatus(status);
    if (currentRunRow_ >= 0) {
        const auto duration = processResult.durationSeconds;
        analysisRuns_->setItem(currentRunRow_, RunDurationColumn,
            new QTableWidgetItem(tr("%1 s").arg(formatDecimal(duration))));
        if (currentRunRecord_) {
            currentRunRecord_->durationSeconds = duration;
            runStore_.save(*currentRunRecord_);
        }
    }
    if (currentRunPurpose_ == SolverRunPurpose::Analysis && runCompleted
        && currentRunRecord_) {
        resultReferenceController_.selectAutomatic(*currentRunRecord_);
        updateResultReference();
    }
    if (currentRunPurpose_ == SolverRunPurpose::OptimizationBaseline) {
        optimizationWorkspace_->finishOptimizationBaseline(runCompleted,
            runCompleted ? QString{} : status);
    }
    appendSolverOutput(tr("\n\nRun status: %1\nElapsed: %2 seconds\n")
        .arg(status).arg(formatDecimal(processResult.durationSeconds)));
    const auto activity = currentRunPurpose_ == SolverRunPurpose::AverageGainTest
        ? tr("Average Gain Test")
        : currentRunPurpose_ == SolverRunPurpose::OptimizationPeakSearch
            ? tr("Maximum-gain search")
        : currentRunPurpose_ == SolverRunPurpose::OptimizationBaseline
            ? tr("Optimization baseline")
        : currentRunRecord_ && currentRunRecord_->runType == QStringLiteral("quick-sweep")
            ? tr("Quick sweep") : tr("Analysis");
    statusBar()->showMessage(tr("%1 %2. Artifacts: %3")
        .arg(activity, status.toLower(), currentRunDirectory_), 10000);
    completeSolverActivity(status);
    if (!runCompleted && processResult.status != SolverProcessStatus::Canceled) {
        showRunMonitor(false);
    }
    cancelRunButton_->setEnabled(false);
    synchronizeRunnerState();
    updateRunSelectionActions();
    updateAnalysisReadiness();
    refreshDashboardRuns();
}

void MainWindow::setCurrentRunStatus(const QString& status)
{
    if (currentRunRow_ >= 0) {
        analysisRuns_->setItem(currentRunRow_, RunStatusColumn, new QTableWidgetItem(status));
    }
    if (currentRunRecord_) {
        currentRunRecord_->status = status;
        runStore_.save(*currentRunRecord_);
    }
}

void MainWindow::loadRunHistory()
{
    analysisRuns_->setRowCount(0);
    analysisRuns_->setUpdatesEnabled(false);
    for (const auto& record : runStore_.load()) {
        if (record.runType == QStringLiteral("optimization-candidate")
            || record.runType == QStringLiteral("convergence-step")) continue;
        addRunRecord(record, false);
    }
    analysisRuns_->setUpdatesEnabled(true);
    analysisRuns_->resizeColumnsToContents();
    analysisRuns_->setColumnWidth(RunNoteColumn,
        std::clamp(analysisRuns_->columnWidth(RunNoteColumn), 120, 240));
    analysisRuns_->clearSelection();
    runHistoryLoaded_ = true;
    updateRunSelectionActions();
}

void MainWindow::showRunHistory()
{
    if (!runHistoryLoaded_) loadRunHistory();
    runHistoryPanel_->showWindow();
}

void MainWindow::refreshDashboardRuns()
{
    const auto recentRuns = runStore_.loadRecent(100);
    latestStoredRunDirectory_ = recentRuns.empty() ? QString{} : recentRuns.front().directory;
    if (controlPanel_ != nullptr) controlPanel_->setRecentRuns(recentRuns);
}

void MainWindow::addRunRecord(const AnalysisRunRecord& record, bool prepend)
{
    const auto row = prepend ? 0 : analysisRuns_->rowCount();
    analysisRuns_->insertRow(row);
    const auto outputBytes = record.outputBytes > 0 ? record.outputBytes
        : QFileInfo(QDir(record.directory).filePath(QStringLiteral("model.out"))).size();
    auto displayRecord = record;
    displayRecord.outputBytes = outputBytes;
    const auto type = record.runType == QStringLiteral("optimization-session")
        ? tr("Optimization")
        : record.runType == QStringLiteral("convergence-session") ? tr("Convergence")
        : record.runType == QStringLiteral("average-gain-test") ? tr("AGT")
        : record.runType == QStringLiteral("quick-sweep") ? tr("Quick Sweep") : tr("Analysis");
    const QStringList values{
        record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        record.sourceFile.isEmpty() ? tr("Archived model.nec") : QFileInfo(record.sourceFile).fileName(),
        record.userNote.trimmed().isEmpty() ? tr("—") : record.userNote.simplified(),
        type,
        runResultsText(displayRecord),
        formatByteSize(outputBytes),
        record.backend,
        record.status,
        record.durationSeconds > 0.0
            ? tr("%1 s").arg(formatDecimal(record.durationSeconds)) : tr("—"),
        record.directory,
    };
    for (auto column = 0; column < values.size(); ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        item->setData(RunDirectoryRole, record.directory);
        item->setData(RunContextRole, runContext(record));
        item->setData(RunTypeRole, record.runType);
        item->setData(RunIdRole, record.id);
        if (column == RunNoteColumn && !record.userNote.trimmed().isEmpty())
            item->setToolTip(record.userNote);
        analysisRuns_->setItem(row, column, item);
    }
    if (prepend) {
        analysisRuns_->resizeColumnsToContents();
        analysisRuns_->setColumnWidth(RunNoteColumn,
            std::clamp(analysisRuns_->columnWidth(RunNoteColumn), 120, 240));
    }
}

void MainWindow::loadSelectedRun()
{
    if (solverRunner_->isRunning() || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning()
        || analysisRuns_->currentRow() < 0) {
        return;
    }
    const auto row = analysisRuns_->currentRow();
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item != nullptr) {
        AnalysisRunRecord record;
        record.id = item->data(RunIdRole).toString();
        record.directory = item->data(RunDirectoryRole).toString();
        record.runType = item->data(RunTypeRole).toString();
        record.sourceFile = analysisRuns_->item(row, RunModelColumn)->text();
        record.started = QDateTime::fromString(
            analysisRuns_->item(row, RunStartedColumn)->text(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        record.backend = analysisRuns_->item(row, RunBackendColumn)->text();
        record.status = analysisRuns_->item(row, RunStatusColumn)->text();
        if (runReviewWindow_ == nullptr) {
            runReviewWindow_ = new RunReviewWindow;
            runReviewWindow_->setOpenSnapshotCallback(
                [this](const QString& directory, const QString& modelName, const QString& context) {
                    if (!loadRunModel(directory, modelName, context)) return;
                    activeModelResultsDirectory_ = directory;
                    activeModelResultsContext_ = context;
                    displayRunArtifacts(directory, context);
                });
        }
        const auto activeModelName = !hasNecModel_ ? QString{}
            : currentFile_.isEmpty() ? tr("Untitled model.nec") : QFileInfo(currentFile_).fileName();
        runReviewWindow_->showRun(record, activeModelName);
    }
}

void MainWindow::inspectSelectedRunInput()
{
    const auto row = analysisRuns_->currentRow();
    if (row < 0) return;
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item == nullptr) return;
    const auto inputPath = QDir(item->data(RunDirectoryRole).toString())
        .filePath(QStringLiteral("model.nec"));
    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::information(this, tr("Input Snapshot Unavailable"),
            tr("The selected run has no readable archived model.nec file.\n\n%1").arg(inputPath));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Input Snapshot — %1")
        .arg(analysisRuns_->item(row, RunModelColumn)->text()));
    dialog.resize(850, 650);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(tr(
        "Read-only NEC deck used by this run. Opening this snapshot does not change the active model."),
        &dialog);
    description->setWordWrap(true);
    auto* snapshot = new QPlainTextEdit(&dialog);
    snapshot->setObjectName(QStringLiteral("runInputSnapshot"));
    snapshot->setReadOnly(true);
    snapshot->setPlainText(QString::fromUtf8(inputFile.readAll()));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(description);
    layout->addWidget(snapshot, 1);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::openSelectedRunSnapshot()
{
    if (solverRunner_->isRunning() || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning()) return;
    const auto row = analysisRuns_->currentRow();
    if (row < 0) return;
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item == nullptr) return;
    const auto directory = item->data(RunDirectoryRole).toString();
    const auto modelName = analysisRuns_->item(row, RunModelColumn)->text();
    const auto context = item->data(RunContextRole).toString();
    if (!loadRunModel(directory, modelName, context)) return;
    activeModelResultsDirectory_ = directory;
    activeModelResultsContext_ = context;
    displayRunArtifacts(directory, context);
}

auto MainWindow::loadRunModel(const QString& directory, const QString& modelName,
    const QString& runContext) -> bool
{
    QFile inputFile(QDir(directory).filePath(QStringLiteral("model.nec")));
    if (!inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusBar()->showMessage(tr("Selected run has no archived NEC input: %1").arg(directory), 5000);
        return true;
    }
    if (!maybeSaveChanges()) return false;

    clearDisplayedResults();
    editor_->setPlainText(QString::fromUtf8(inputFile.readAll()));
    resetSourceUndoHistory();
    hasNecModel_ = true;
    modelModuleAction_->setEnabled(true);
    analysisModuleAction_->setEnabled(true);
    resultsModuleAction_->setEnabled(true);
    dashboardStack_->setCurrentIndex(1);
    analysisRequestEditor_->resetFrequencySelectionForModel();
    setCurrentFile({});
    const auto displayName = modelName.isEmpty() ? tr("Archived model.nec") : modelName;
    setWindowTitle(tr("%1 (snapshot)[*] — NEC Workbench").arg(displayName));
    modelFileStatus_->setText(tr("Open snapshot as new model: %1").arg(displayName));
    modelFileStatus_->setToolTip(tr("%1\nArchived input: %2\nUse Save As to name this editable copy.")
        .arg(runContext, inputFile.fileName()));
    editor_->document()->setModified(false);
    checkModel();
    controlPanel_->setDocumentState(displayName, false);
    return true;
}

void MainWindow::updateRunSelectionActions()
{
    const auto row = analysisRuns_->currentRow();
    const auto hasSelection = row >= 0 && analysisRuns_->item(row, RunStartedColumn) != nullptr;
    auto activeSelection = false;
    if (hasSelection && solverRunner_->isRunning()) {
        activeSelection = analysisRuns_->item(row, RunStartedColumn)->data(RunDirectoryRole).toString()
            == currentRunDirectory_;
    }
    const auto solverBusy = solverRunner_->isRunning() || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning();
    openRunResultsButton_->setEnabled(hasSelection && !activeSelection && !solverBusy);
    const auto selectedType = hasSelection
        ? analysisRuns_->item(row, RunStartedColumn)->data(RunTypeRole).toString() : QString{};
    openRunResultsButton_->setText(selectedType == QStringLiteral("optimization-session")
        ? tr("Review Optimization")
        : selectedType == QStringLiteral("convergence-session") ? tr("Review Convergence")
        : selectedType == QStringLiteral("average-gain-test") ? tr("Review AGT") : tr("Open Run Review"));
    const auto hasInputSnapshot = hasSelection
        && QFileInfo::exists(QDir(analysisRuns_->item(row, RunStartedColumn)
            ->data(RunDirectoryRole).toString()).filePath(QStringLiteral("model.nec")));
    const auto isSession = selectedType == QStringLiteral("optimization-session")
        || selectedType == QStringLiteral("convergence-session");
    inspectRunInputButton_->setEnabled(hasInputSnapshot && !isSession);
    openRunSnapshotButton_->setEnabled(hasInputSnapshot && !isSession && !activeSelection && !solverBusy);
    const auto canPinReference = hasSelection && !activeSelection && !solverBusy
        && (selectedType == QStringLiteral("analysis")
            || selectedType == QStringLiteral("quick-sweep"))
        && analysisRuns_->item(row, RunStatusColumn)->text() == tr("Completed")
        && QFileInfo::exists(QDir(analysisRuns_->item(row, RunStartedColumn)
            ->data(RunDirectoryRole).toString()).filePath(QStringLiteral("model.out")));
    pinRunReferenceButton_->setEnabled(canPinReference);
    editRunNoteButton_->setEnabled(hasSelection && !solverBusy);
    openRunFolderButton_->setEnabled(hasSelection);
    deleteRunButton_->setEnabled(hasSelection && !activeSelection && !solverBusy);
}

auto MainWindow::selectedRunRecord() const -> std::optional<AnalysisRunRecord>
{
    const auto row = analysisRuns_->currentRow();
    if (row < 0) return std::nullopt;
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item == nullptr) return std::nullopt;
    const auto id = item->data(RunIdRole).toString();
    const auto records = runStore_.load();
    const auto found = std::ranges::find(records, id, &AnalysisRunRecord::id);
    return found == records.end() ? std::nullopt : std::optional<AnalysisRunRecord>{*found};
}

void MainWindow::pinSelectedRunReference()
{
    const auto record = selectedRunRecord();
    if (!record) return;
    resultReferenceController_.pin(*record);
    updateResultReference();
    statusBar()->showMessage(tr(
        "Historical overlay selected: %1. Turn it on in an Impedance or Radiation window.")
        .arg(resultReferenceController_.label()), 7000);
}

void MainWindow::chooseResultReference(QWidget* anchor)
{
    QMenu menu(this);
    auto eligibleCount = 0;
    const auto* selectedReference = resultReferenceController_.record();
    if (selectedReference != nullptr) {
        auto* clearAction = menu.addAction(tr("Clear Historical Overlay"));
        connect(clearAction, &QAction::triggered, this, [this] { clearResultReference(); });
        menu.addSeparator();
    }
    for (const auto& record : runStore_.load()) {
        if (record.directory == displayedRunDirectory_
            || record.status != QStringLiteral("Completed")
            || (record.runType != QStringLiteral("analysis")
                && record.runType != QStringLiteral("quick-sweep"))
            || !QFileInfo::exists(QDir(record.directory).filePath(
                QStringLiteral("model.out")))) continue;
        const auto modelName = record.sourceFile.isEmpty()
            ? tr("Archived model.nec") : QFileInfo(record.sourceFile).fileName();
        const auto detail = record.userNote.trimmed().isEmpty()
            ? record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : record.userNote.simplified();
        auto* action = menu.addAction(tr("%1 — %2").arg(modelName, detail));
        action->setCheckable(true);
        action->setChecked(selectedReference != nullptr
            && selectedReference->id == record.id);
        connect(action, &QAction::triggered, this, [this, record] {
            resultReferenceController_.pin(record);
            updateResultReference();
            statusBar()->showMessage(tr(
                "Historical overlay selected. Use Show Historical Overlay in either result window."),
                6000);
        });
        ++eligibleCount;
    }
    if (eligibleCount == 0) {
        auto* empty = menu.addAction(tr("No completed runs available"));
        empty->setEnabled(false);
    }
    menu.addSeparator();
    auto* historyAction = menu.addAction(tr("Open Run History…"));
    connect(historyAction, &QAction::triggered, this, [this] { showRunHistory(); });
    auto* menuAnchor = anchor != nullptr ? anchor : this;
    menu.exec(menuAnchor->mapToGlobal(QPoint(0, menuAnchor->height())));
}

void MainWindow::editSelectedRunNote()
{
    auto record = selectedRunRecord();
    if (!record) return;
    auto accepted = false;
    const auto note = QInputDialog::getMultiLineText(this, tr("Edit Run Note"),
        tr("Describe what changed or why this run matters:"), record->userNote, &accepted);
    if (!accepted) return;
    record->userNote = note.trimmed();
    if (!runStore_.save(*record)) {
        QMessageBox::critical(this, tr("Run Note Not Saved"),
            tr("Could not update run metadata in:\n%1").arg(record->directory));
        return;
    }
    if (currentRunRecord_ && currentRunRecord_->id == record->id)
        currentRunRecord_->userNote = record->userNote;
    const auto row = analysisRuns_->currentRow();
    if (row >= 0) {
        auto* noteItem = analysisRuns_->item(row, RunNoteColumn);
        if (noteItem == nullptr) {
            noteItem = new QTableWidgetItem;
            analysisRuns_->setItem(row, RunNoteColumn, noteItem);
        }
        noteItem->setText(record->userNote.isEmpty() ? tr("—") : record->userNote.simplified());
        noteItem->setToolTip(record->userNote);
    }
    resultReferenceController_.refreshMetadata(*record);
    updateResultReference();
    refreshDashboardRuns();
}

void MainWindow::clearResultReference()
{
    resultReferenceController_.clear();
    setResultReferenceVisible(false);
    updateResultReference();
}

void MainWindow::setResultReferenceVisible(bool visible)
{
    if (impedanceOverlayControl_ != nullptr) impedanceOverlayControl_->setChecked(visible);
    else if (visualizePlotsView_ != nullptr) visualizePlotsView_->setReferenceVisible(visible);
    if (radiationOverlayControl_ != nullptr) radiationOverlayControl_->setChecked(visible);
    else {
        radiationPatternView_->setReferenceVisible(visible);
        radiation3DView_->setReferenceVisible(visible);
        radiationPerformanceView_->setReferenceVisible(visible);
    }
}

void MainWindow::updateResultReference()
{
    if (visualizePlotsView_ == nullptr || impedanceOverlayControl_ == nullptr
        || radiationOverlayControl_ == nullptr) return;
    const auto* reference = resultReferenceController_.result();
    const auto hasReference = reference != nullptr;
    if (hasReference) {
        const auto* record = resultReferenceController_.record();
        const auto modelName = record->sourceFile.isEmpty()
            ? tr("Archived model.nec") : QFileInfo(record->sourceFile).fileName();
        const auto note = record->userNote.simplified();
        const auto plotLabel = note.isEmpty()
            ? tr("%1 — %2").arg(modelName,
                record->started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
            : tr("%1 — “%2”").arg(modelName, note);
        visualizePlotsView_->setReferenceResults(
            *reference, plotLabel);
        radiationPatternView_->setReferenceResults(*reference, plotLabel);
        radiation3DView_->setReferenceResults(*reference, plotLabel);
        radiationPerformanceView_->setReferenceResults(*reference, plotLabel);
        const auto compatibility = analysis::evaluateHistoricalOverlayCompatibility(
            displayedResults_, *reference);
        impedanceOverlayControl_->setSelection(
            plotLabel, historicalOverlayStatusText(compatibility.impedance));
        radiationOverlayControl_->setSelection(
            plotLabel, historicalOverlayStatusText(compatibility.radiation));
        visualizePlotsView_->setReferenceVisible(impedanceOverlayControl_->isChecked());
        const auto radiationVisible = radiationOverlayControl_->isChecked();
        radiationPatternView_->setReferenceVisible(radiationVisible);
        radiation3DView_->setReferenceVisible(radiationVisible);
        radiationPerformanceView_->setReferenceVisible(radiationVisible);
    } else {
        visualizePlotsView_->clearReferenceResults();
        radiationPatternView_->clearReferenceResults();
        radiation3DView_->clearReferenceResults();
        radiationPerformanceView_->clearReferenceResults();
        impedanceOverlayControl_->clearSelection();
        radiationOverlayControl_->clearSelection();
    }
    updateRunSelectionActions();
}

void MainWindow::deleteSelectedRun()
{
    if (optimizationWorkspace_->isRunning() || convergenceWorkspace_->isRunning()) return;
    const auto row = analysisRuns_->currentRow();
    if (row < 0) return;
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item == nullptr) return;
    const auto directory = item->data(RunDirectoryRole).toString();
    if (solverRunner_->isRunning() && directory == currentRunDirectory_) {
        QMessageBox::information(this, tr("Run Is Active"),
            tr("The active solver run cannot be deleted."));
        return;
    }
    const auto modelName = analysisRuns_->item(row, RunModelColumn)->text();
    const auto answer = QMessageBox::warning(this, tr("Delete Run"),
        tr("Permanently delete the selected run for %1?\n\n%2")
            .arg(modelName, directory), QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    const auto runType = item->data(RunTypeRole).toString();
    const auto removed = runType == QStringLiteral("optimization-session")
            || runType == QStringLiteral("convergence-session")
        ? runStore_.removeGroup(item->data(RunIdRole).toString(), directory)
        : runStore_.remove(directory);
    if (!removed) {
        QMessageBox::critical(this, tr("Delete Failed"),
            tr("Could not delete the run folder:\n%1").arg(directory));
        return;
    }
    if (const auto* reference = resultReferenceController_.record();
        reference != nullptr && reference->directory == directory) {
        clearResultReference();
    }
    analysisRuns_->removeRow(row);
    if (activeModelResultsDirectory_ == directory) {
        activeModelResultsDirectory_.clear();
        activeModelResultsContext_.clear();
    }
    if (displayedRunDirectory_ == directory) clearDisplayedResults();
    updateRunSelectionActions();
    refreshDashboardRuns();
}

void MainWindow::displayRunArtifacts(const QString& directory, const QString& context)
{
    QFile logFile(QDir(directory).filePath(QStringLiteral("run.log")));
    const auto logText = logFile.open(QIODevice::ReadOnly)
        ? QString::fromLocal8Bit(logFile.readAll()) : tr("No run log is available.");
    solverOutput_->setPlainText(logText);

    QFile outputFile(QDir(directory).filePath(QStringLiteral("model.out")));
    if (!outputFile.open(QIODevice::ReadOnly)) {
        clearDisplayedResults();
        const auto message = tr("Historical run is incomplete — model.out is missing or unreadable. %1")
            .arg(context);
        analysisOutput_->setPlainText(tr("Solver output unavailable: %1").arg(outputFile.fileName()));
        rawOutputPanel_->setStatusText(message);
        showRunHistory();
        statusBar()->showMessage(message, 7000);
        return;
    }
    const auto outputBytes = outputFile.readAll();
    analysisOutput_->setPlainText(QString::fromLocal8Bit(outputBytes));
    auto result = analysis::NecOutputParser{}.parse(std::string_view(
        outputBytes.constData(), static_cast<std::size_t>(outputBytes.size())));
    QFile archivedSourceFile(QDir(directory).filePath(QStringLiteral("model.source.nec")));
    if (archivedSourceFile.open(QIODevice::ReadOnly)) {
        const auto sourceDocument = nec::NecParser{}.parse(
            QString::fromUtf8(archivedSourceFile.readAll()).toStdString());
        result.referenceImpedanceOhms = model::referenceImpedanceOhms(
            nec::NecSetupConverter{}.convert(sourceDocument));
    }
    AnalysisRunRecord summary;
    setRunResultMetadata(summary, result, outputBytes.size());
    const auto selectedRow = analysisRuns_->currentRow();
    const auto* selectedItem = selectedRow >= 0
        ? analysisRuns_->item(selectedRow, RunStartedColumn) : nullptr;
    if (selectedItem != nullptr
        && selectedItem->data(RunDirectoryRole).toString() == directory) {
        analysisRuns_->setItem(selectedRow, RunResultsColumn,
            new QTableWidgetItem(runResultsText(summary)));
        analysisRuns_->setItem(selectedRow, RunOutputSizeColumn,
            new QTableWidgetItem(formatByteSize(summary.outputBytes)));
    }
    const auto hasParsedResults = !result.feedpoints.empty()
        || !result.currents.empty() || !result.radiation.empty();
    if (!hasParsedResults) {
        clearDisplayedResults();
        const auto message = tr("Historical run contains no supported result data. %1").arg(context);
        rawOutputPanel_->setStatusText(message);
        rawOutputPanel_->showWindow();
        statusBar()->showMessage(message, 7000);
        return;
    }
    analysisResultsView_->setResults(result, context);
    visualizePlotsView_->setResults(result, context);
    currentResultsView_->setModel({});
    currentResultsView_->setResults(result, context);
    radiationPatternView_->setResults(result, context);
    radiation3DView_->setModel({});
    QFile inputFile(QDir(directory).filePath(QStringLiteral("model.nec")));
    if (inputFile.open(QIODevice::ReadOnly)) {
        const auto runDocument = nec::NecParser{}.parse(
            QString::fromUtf8(inputFile.readAll()).toStdString());
        const auto conversion = nec::NecModelConverter{}.convert(runDocument);
        if (!conversion.model.empty()) {
            currentResultsView_->setModel(conversion.model);
            radiation3DView_->setModel(conversion.model);
        }
    }
    radiation3DView_->setResults(result, context);
    radiationPerformanceView_->setResults(result, context);
    setDisplayedResults(result);
    displayedRunDirectory_ = directory;
    resultsAvailable_ = true;
    liveResultsStale_ = false;
    setLiveResultStatus(tr("Current"));
    if (!result.feedpoints.empty()) resultWindowCoordinator_->showImpedance();
    else if (!result.radiation.empty()) resultWindowCoordinator_->showRadiation();
    else if (!result.currents.empty()) resultWindowCoordinator_->showCurrents();
    statusBar()->showMessage(tr("Displaying active model results: %1").arg(directory), 5000);
}

void MainWindow::setDisplayedResults(const analysis::AnalysisResult& result)
{
    displayedResults_ = result;
    std::vector<double> frequencies;
    const auto addFrequency = [&frequencies](double frequencyMHz) {
        if (std::ranges::none_of(frequencies, [frequencyMHz](double existing) {
                return nearlyEqual(existing, frequencyMHz);
            })) frequencies.push_back(frequencyMHz);
    };
    for (const auto& value : result.feedpoints) addFrequency(value.frequencyMHz);
    for (const auto& value : result.currents) addFrequency(value.frequencyMHz);
    for (const auto& value : result.radiation) addFrequency(value.frequencyMHz);
    std::ranges::sort(frequencies);
    const QSignalBlocker blocker(rawOutputFrequencyControl_);
    rawOutputFrequencyControl_->clear();
    for (const auto frequencyMHz : frequencies) {
        rawOutputFrequencyControl_->addItem(
            tr("%1 MHz").arg(formatDecimal(frequencyMHz)), frequencyMHz);
    }
    rawOutputFrequencyControl_->setEnabled(!frequencies.empty());
    updateResultReference();
}

void MainWindow::jumpRawOutputToSelectedFrequency()
{
    if (rawOutputFrequencyControl_->currentIndex() < 0
        || analysisOutput_->document()->isEmpty()) return;
    const auto target = rawOutputFrequencyControl_->currentData().toDouble();
    static const QRegularExpression frequencyExpression(
        QStringLiteral("FREQUENCY\\s*:\\s*([+\\-0-9.EeDd]+)"),
        QRegularExpression::CaseInsensitiveOption);
    for (auto block = analysisOutput_->document()->begin(); block.isValid(); block = block.next()) {
        const auto match = frequencyExpression.match(block.text());
        if (!match.hasMatch()) continue;
        auto valueText = match.captured(1);
        valueText.replace(QLatin1Char('D'), QLatin1Char('E'), Qt::CaseInsensitive);
        bool valid{};
        const auto value = valueText.toDouble(&valid);
        if (!valid || !nearlyEqual(value, target)) continue;
        QTextCursor cursor(block);
        analysisOutput_->setTextCursor(cursor);
        analysisOutput_->centerCursor();
        statusBar()->showMessage(
            tr("Raw output positioned at %1 MHz.").arg(formatDecimal(target)), 3000);
        return;
    }
    statusBar()->showMessage(
        tr("No raw-output section was found for %1 MHz.").arg(formatDecimal(target)), 5000);
}

void MainWindow::findInRawOutput()
{
    const auto text = rawOutputFindControl_->text();
    if (text.isEmpty() || analysisOutput_->document()->isEmpty()) return;
    if (analysisOutput_->find(text)) return;
    auto cursor = analysisOutput_->textCursor();
    cursor.movePosition(QTextCursor::Start);
    analysisOutput_->setTextCursor(cursor);
    if (!analysisOutput_->find(text))
        statusBar()->showMessage(tr("Text not found in raw output: %1").arg(text), 4000);
}

void MainWindow::clearDisplayedResults()
{
    updateResultReference();
    const analysis::AnalysisResult empty;
    const auto context = tr("No analysis run");
    averageGainResultsView_->clear();
    convergenceWorkspace_->clearResults();
    controlPanel_->clearStudy(ControlPanel::Study::AverageGain);
    controlPanel_->clearStudy(ControlPanel::Study::Convergence);
    analysisResultsView_->setResults(empty, context);
    visualizePlotsView_->setResults(empty, context);
    currentResultsView_->setModel({});
    currentResultsView_->setResults(empty, context);
    radiationPatternView_->setResults(empty, context);
    radiation3DView_->setResults(empty, context);
    radiation3DView_->setModel({});
    radiationPerformanceView_->setResults(empty, context);
    setDisplayedResults(empty);
    controlPanel_->setResults(empty, context, false);
    displayedRunDirectory_.clear();
    resultsAvailable_ = false;
    liveResultsStale_ = false;
    setLiveResultStatus(tr("No results"));
}

void MainWindow::editWire(const model::Wire& original, const model::Wire& updated)
{
    if (!original.editable) return;
    const auto block = editor_->document()->findBlockByNumber(
        static_cast<int>(original.sourceLine) - 1);
    if (!block.isValid()) return;
    const auto sourceLine = block.text().toStdString();
    const auto scale = deckScaleForSourceLine(original.sourceLine);
    const auto number = [scale](double value) {
        std::ostringstream output;
        output << std::setprecision(15) << value / scale;
        return output.str();
    };
    std::vector<nec::NecFieldReplacement> replacements;
    const auto addReplacement = [&replacements, &sourceLine](std::size_t field,
                                    bool changed, std::string value) {
        if (!changed) return true;
        if (!nec::necCardFieldIsNumeric(sourceLine, field)) return false;
        replacements.push_back({field, std::move(value)});
        return true;
    };
    const bool editable =
        addReplacement(0, original.tag != updated.tag, std::to_string(updated.tag))
        && addReplacement(1, original.segments != updated.segments,
            std::to_string(updated.segments))
        && addReplacement(2, original.start.x != updated.start.x, number(updated.start.x))
        && addReplacement(3, original.start.y != updated.start.y, number(updated.start.y))
        && addReplacement(4, original.start.z != updated.start.z, number(updated.start.z))
        && addReplacement(5, original.end.x != updated.end.x, number(updated.end.x))
        && addReplacement(6, original.end.y != updated.end.y, number(updated.end.y))
        && addReplacement(7, original.end.z != updated.end.z, number(updated.end.z))
        && addReplacement(8, original.radius != updated.radius, number(updated.radius));
    if (!editable) {
        showSymbolicGeometryEditBlocked();
        updateWireCardEditor();
        return;
    }
    if (replacements.empty()) return;
    const auto replacement = nec::replaceNecCardFields(sourceLine, replacements);
    if (!replacement) return;
    const auto source = nec::replaceSourceLine(
        editor_->toPlainText().toStdString(), original.sourceLine, *replacement);
    if (!source) return;
    pushGeometrySourceEdit(tr("Edit wire %1").arg(original.tag),
        QString::fromStdString(*source));
    selectWireInProject(updated.tag);
}

void MainWindow::editStructuredCard(std::size_t sourceLine, const QString& cardText)
{
    const auto source = nec::replaceSourceLine(editor_->toPlainText().toStdString(),
        sourceLine, cardText.toStdString());
    if (source && QString::fromStdString(*source) != editor_->toPlainText())
        pushGeometrySourceEdit(tr("Edit structured card on line %1").arg(sourceLine),
            QString::fromStdString(*source));
}

void MainWindow::makeFieldOptimizable(std::size_t sourceLine,
    std::size_t fieldIndex, const QString& fieldLabel)
{
    const auto& cache = activeSource();
    const auto source = std::string(cache.source());
    const auto& document = cache.document();
    if (sourceLine == 0 || sourceLine > document.cards().size()) return;
    const auto& card = document.cards()[sourceLine - 1];
    if (fieldIndex >= card.fields.size()) return;
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) {
        QMessageBox::warning(this, tr("Parameter Error"),
            tr("Resolve the current symbol error before changing parameter links: %1")
                .arg(QString::fromStdString(resolution.diagnostics.front().message)));
        return;
    }
    const auto& resolvedDocument = *cache.resolvedDocument();
    if (sourceLine > resolvedDocument.cards().size()
        || fieldIndex >= resolvedDocument.cards()[sourceLine - 1].fields.size()) return;
    const auto currentExpression = QString::fromStdString(card.fields[fieldIndex]);
    const auto resolvedValue = QString::fromStdString(
        resolvedDocument.cards()[sourceLine - 1].fields[fieldIndex]);

    auto fieldName = fieldLabel.section(QLatin1Char('('), 0, 0).trimmed().toLower();
    fieldName.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    fieldName.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    auto baseName = QString::fromStdString(card.mnemonic).toLower();
    if (!fieldName.isEmpty()) baseName += QLatin1Char('_') + fieldName;
    if (baseName.isEmpty() || baseName.front().isDigit())
        baseName.prepend(QStringLiteral("parameter_"));

    const auto nameUsed = [&resolution](const QString& candidate) {
        return std::ranges::any_of(resolution.definitions, [&candidate](const auto& definition) {
            return QString::fromStdString(definition.name).compare(
                candidate, Qt::CaseInsensitive) == 0;
        });
    };
    auto suggestion = baseName;
    for (auto suffix = 2; nameUsed(suggestion); ++suffix)
        suggestion = QStringLiteral("%1_%2").arg(baseName).arg(suffix);

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("parameterizeFieldDialog"));
    dialog.setWindowTitle(tr("Parameterize Field"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* fieldValue = new QLabel(tr("%1 → %2").arg(currentExpression, resolvedValue), &dialog);
    fieldValue->setObjectName(QStringLiteral("parameterizeCurrentValue"));
    auto* mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("parameterizeMode"));
    mode->addItem(tr("Create New Parameter"), 0);
    if (!resolution.definitions.empty()) mode->addItem(tr("Use Existing Parameter"), 1);
    auto* newName = new QLineEdit(suggestion, &dialog);
    newName->setObjectName(QStringLiteral("parameterizeNewName"));
    auto* existing = new QComboBox(&dialog);
    existing->setObjectName(QStringLiteral("parameterizeExistingSymbol"));
    for (const auto& definition : resolution.definitions) {
        existing->addItem(tr("%1 = %2")
            .arg(QString::fromStdString(definition.name))
            .arg(definition.value, 0, 'g', 12),
            QString::fromStdString(definition.name));
        existing->setItemData(existing->count()-1, definition.value, Qt::UserRole+1);
    }
    const auto directSymbol = std::ranges::find_if(resolution.definitions,
        [&currentExpression](const auto& definition) {
            const auto name = QString::fromStdString(definition.name);
            return name.compare(currentExpression, Qt::CaseInsensitive) == 0
                || (QLatin1Char('-') + name).compare(
                    currentExpression, Qt::CaseInsensitive) == 0;
        });
    if (directSymbol != resolution.definitions.end()) {
        mode->setCurrentIndex(mode->findData(1));
        existing->setCurrentIndex(existing->findData(
            QString::fromStdString(directSymbol->name)));
    }
    auto* preview = new QLabel(&dialog);
    preview->setObjectName(QStringLiteral("parameterizePreview"));
    preview->setWordWrap(true);
    form->addRow(tr("Current field"), fieldValue);
    form->addRow(tr("Action"), mode);
    form->addRow(tr("New parameter name"), newName);
    form->addRow(tr("Existing parameter"), existing);
    form->addRow(tr("Preview"), preview);
    auto* newNameLabel = form->labelForField(newName);
    auto* existingLabel = form->labelForField(existing);
    layout->addLayout(form);
    auto* note = new QLabel(tr(
        "The selected field will reference the parameter. Configure optimization bounds in Optimize."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    const auto updateDialog = [mode, newName, existing, preview, resolvedValue,
                                  newNameLabel, existingLabel] {
        const auto create = mode->currentData().toInt() == 0;
        newName->setVisible(create);
        newNameLabel->setVisible(create);
        existing->setVisible(!create);
        existingLabel->setVisible(!create);
        auto ok = false;
        const auto fieldValue = resolvedValue.toDouble(&ok);
        if (create) {
            auto definition = resolvedValue;
            auto reference = newName->text().trimmed();
            if (ok && fieldValue < 0.0) {
                definition = QString::number(-fieldValue, 'g', 15);
                reference.prepend(QLatin1Char('-'));
            }
            preview->setText(QObject::tr("Create SY %1=%2 and link this field to %3.")
                .arg(newName->text().trimmed(), definition, reference));
        } else {
            const auto name = existing->currentData().toString();
            const auto symbolValue = existing->currentData(Qt::UserRole+1).toDouble();
            const auto reference = ok
                ? QString::fromStdString(nec::symbolReferencePreservingValue(
                    name.toStdString(), symbolValue, fieldValue))
                : name;
            preview->setText(QObject::tr("Link this field to %1.").arg(reference));
        }
    };
    connect(mode, &QComboBox::currentIndexChanged, &dialog, updateDialog);
    connect(newName, &QLineEdit::textChanged, &dialog, updateDialog);
    connect(existing, &QComboBox::currentIndexChanged, &dialog, updateDialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateDialog();
    if (dialog.exec() != QDialog::Accepted) return;

    const auto create = mode->currentData().toInt() == 0;
    const auto name = create ? newName->text().trimmed() : existing->currentData().toString();
    if (name.isEmpty()) return;
    const QRegularExpression validName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (create && (!validName.match(name).hasMatch() || nameUsed(name))) {
        QMessageBox::warning(this, tr("Invalid Parameter Name"),
            tr("Use a unique name beginning with a letter or underscore, followed only by letters, numbers, or underscores."));
        return;
    }

    auto sourceForParameter = std::optional<std::string>{source};
    if (create && !nec::necCardFieldIsNumeric(card.sourceText, fieldIndex)) {
        sourceForParameter = nec::replaceNecCardFieldExpression(
            source, sourceLine, fieldIndex, resolvedValue.toStdString());
    }
    auto existingReference = name;
    if (!create) {
        auto ok = false;
        const auto fieldValue = resolvedValue.toDouble(&ok);
        if (ok) {
            existingReference = QString::fromStdString(nec::symbolReferencePreservingValue(
                name.toStdString(), existing->currentData(Qt::UserRole+1).toDouble(), fieldValue));
        }
    }
    const auto updated = !sourceForParameter ? std::optional<std::string>{}
        : create
            ? nec::parameterizeNecCardField(
                *sourceForParameter, sourceLine, fieldIndex, name.toStdString())
            : nec::replaceNecCardFieldExpression(
                source, sourceLine, fieldIndex, existingReference.toStdString());
    if (!updated) {
        statusBar()->showMessage(tr("The selected field could not be parameterized."), 5000);
        return;
    }
    const auto updatedResolution = nec::NecSymbolResolver{}.resolve(*updated);
    if (!updatedResolution.ok()) {
        QMessageBox::warning(this, tr("Parameter Error"),
            tr("The parameter could not be created: %1")
                .arg(QString::fromStdString(updatedResolution.diagnostics.front().message)));
        return;
    }
    const auto updatedConversion = nec::NecModelConverter{}.convert(
        nec::NecParser{}.parse(updatedResolution.resolvedSource));
    const auto updatedSourceLine = sourceLine + (create ? 1 : 0);
    const auto geometryIssue = std::ranges::find(
        updatedConversion.issues, updatedSourceLine, &nec::ConversionIssue::lineNumber);
    if (geometryIssue != updatedConversion.issues.end()) {
        QMessageBox::warning(this, tr("Invalid Parameter Link"),
            tr("This parameter link would make the geometry invalid: %1")
                .arg(QString::fromStdString(geometryIssue->message)));
        return;
    }
    pushGeometrySourceEdit(create
            ? tr("Parameterize %1 as new %2").arg(fieldLabel, name)
            : tr("Link %1 to %2").arg(fieldLabel, existingReference),
        QString::fromStdString(*updated));
    statusBar()->showMessage(create
        ? tr("Created %1. Set its bounds in Optimize.").arg(name)
        : tr("Linked %1 to %2.").arg(fieldLabel, existingReference), 5000);
}

void MainWindow::detachFieldParameter(std::size_t sourceLine,
    std::size_t fieldIndex, const QString& fieldLabel)
{
    const auto& cache = activeSource();
    const auto source = std::string(cache.source());
    const auto& resolution = cache.resolution();
    if (!resolution.ok()) {
        QMessageBox::warning(this, tr("Parameter Error"),
            tr("Resolve the current symbol error before detaching this field."));
        return;
    }
    const auto& resolvedDocument = *cache.resolvedDocument();
    if (sourceLine == 0 || sourceLine > resolvedDocument.cards().size()
        || fieldIndex >= resolvedDocument.cards()[sourceLine - 1].fields.size()) return;
    const auto numericValue = resolvedDocument.cards()[sourceLine - 1].fields[fieldIndex];
    const auto updated = nec::replaceNecCardFieldExpression(
        source, sourceLine, fieldIndex, numericValue);
    if (!updated) return;
    pushGeometrySourceEdit(tr("Detach parameter from %1").arg(fieldLabel),
        QString::fromStdString(*updated));
    statusBar()->showMessage(tr("Replaced %1 with its current numeric value.").arg(fieldLabel),
        5000);
}

void MainWindow::addStructuredCard(const QString& cardText)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto& document = activeSource().document();
    auto lastGeometryIndex = -1;
    auto hasGeometryEnd = false;
    for (const auto& card : document.cards()) {
        if (nec::isGeometryCard(card.kind))
            lastGeometryIndex = static_cast<int>(card.lineNumber)-1;
        if (card.kind == nec::NecCardKind::GeometryEnd) hasGeometryEnd = true;
    }
    if (lastGeometryIndex >= 0 && !hasGeometryEnd) {
        const auto grounded = currentSetup_.ground
            && currentSetup_.ground->type != model::GroundType::FreeSpace;
        lines.insert(lastGeometryIndex+1,
            QString::fromStdString(nec::NecWriter{}.writeGeometryEndCard(grounded ? 1 : 0)));
        hasGeometryEnd = true;
    }
    const auto mnemonic = cardText.section(QLatin1Char(' '), 0, 0).toUpper();
    const auto mnemonicText = mnemonic.toStdString();
    const auto* cardSpec = nec::findNecCardSpec(mnemonicText);
    const auto addingGeometry = cardSpec != nullptr && nec::isGeometryCard(cardSpec->kind);
    auto insertion = lines.size();
    if (addingGeometry) {
        for (auto index = 0; index < lines.size(); ++index) {
            if (lines[index].trimmed().section(QLatin1Char(' '), 0, 0).compare(
                    QStringLiteral("GE"), Qt::CaseInsensitive) == 0) {
                insertion = index;
                break;
            }
        }
    }
    for (auto index = 0; index < lines.size(); ++index) {
        if (addingGeometry && insertion != lines.size()) break;
        const auto lineMnemonic = lines[index].trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
        if (lineMnemonic == QStringLiteral("EN")
            || (mnemonic != QStringLiteral("XQ") && lineMnemonic == QStringLiteral("XQ"))) {
            insertion = index;
            break;
        }
    }
    lines.insert(insertion, cardText);
    if (addingGeometry && !hasGeometryEnd) {
        lines.insert(insertion+1, QString::fromStdString(nec::NecWriter{}.writeGeometryEndCard(0)));
    }
    pushGeometrySourceEdit(tr("Add %1 card").arg(mnemonic), lines.join(QLatin1Char('\n')));
}

void MainWindow::deleteStructuredCard(std::size_t sourceLine)
{
    const auto& document = activeSource().document();
    const auto found = std::ranges::find(document.cards(), sourceLine, &nec::NecCard::lineNumber);
    if (found == document.cards().end()) return;
    const auto source = nec::removeSourceLine(editor_->toPlainText().toStdString(), sourceLine);
    if (source) pushGeometrySourceEdit(tr("Delete %1 card")
        .arg(QString::fromStdString(found->mnemonic)), QString::fromStdString(*source));
}

void MainWindow::pushGeometrySourceEdit(const QString& description, QString updatedSource)
{
    if (editor_->toPlainText() == updatedSource) {
        return;
    }
    auto destination = EditorDestination::Geometry;
    const auto geometryTab = workspace_->currentIndex();
    if (moduleStack_->currentIndex() == analysisModuleIndex_
        || (setupPanel_ != nullptr && setupPanel_->isWindowActive())) {
        if (analysisWorkspace_->currentIndex() == modelSourcesWorkspaceIndex_)
            destination = EditorDestination::Sources;
        else if (analysisWorkspace_->currentIndex() == loadNetworkTabIndex_)
            destination = EditorDestination::Loads;
        else if (analysisWorkspace_->currentIndex() == modelEnvironmentWorkspaceIndex_)
            destination = EditorDestination::Environment;
        else
            destination = EditorDestination::Frequency;
    } else if (moduleStack_->currentIndex() == modelModuleIndex_
        || (modelPanel_ != nullptr && modelPanel_->isWindowActive())) {
        if (modelWorkspace_->currentIndex() == modelDeckWorkspaceIndex_)
            destination = EditorDestination::RawSource;
        else if (modelWorkspace_->currentIndex() == modelParametersWorkspaceIndex_)
            destination = EditorDestination::Parameters;
    }
    const auto analysisTab = analysisWorkspace_->currentIndex();
    const auto sourceTab = sourceWorkspace_->currentIndex();
    applyGeometrySource(description, updatedSource, destination, geometryTab, analysisTab, sourceTab);
}

void MainWindow::applyGeometrySource(const QString& description, const QString& source,
    EditorDestination destination, int geometryTab, int analysisTab, int sourceTab)
{
    pendingSourceHistoryEntry_ = SourceHistoryEntry{
        description, destination, geometryTab, analysisTab, sourceTab};
    editor_->replaceTextAsSingleEdit(source);
    pendingSourceHistoryEntry_.reset();
    editor_->document()->setModified(true);
    checkModel();
    if (destination == EditorDestination::Frequency) {
        showModule(analysisModuleIndex_);
        analysisWorkspace_->setCurrentIndex(analysisTab);
    } else {
        showEditor(destination, geometryTab);
    }
    if (destination == EditorDestination::RawSource) {
        sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
    }
}

auto MainWindow::nextWireTag() const -> int
{
    auto tag = 1;
    for (const auto& wire : currentModel_.wires()) {
        tag = std::max(tag, wire.tag + 1);
    }
    return tag;
}

auto MainWindow::wireHasSymbolicGeometry(std::size_t sourceLine) const -> bool
{
    const auto block = editor_->document()->findBlockByNumber(static_cast<int>(sourceLine) - 1);
    if (!block.isValid()) return false;
    const auto source = block.text().toStdString();
    for (auto field = std::size_t{2}; field <= 8; ++field)
        if (!nec::necCardFieldIsNumeric(source, field)) return true;
    return false;
}

void MainWindow::showSymbolicGeometryEditBlocked()
{
    QMessageBox::information(this, tr("Symbolic Wire Geometry"), tr(
        "This GW card uses symbolic coordinates or radius values. Edit its SY definitions or raw "
        "NEC source instead; the graphical edit was canceled to preserve those expressions."));
}

void MainWindow::refreshGeometryViews()
{
    updateWireCardEditor();
    xyView_->updateModel(currentModel_);
    xzView_->updateModel(currentModel_);
    yzView_->updateModel(currentModel_);
    geometry3DView_->updateModel(currentModel_);
}

void MainWindow::updateWireCardEditor()
{
    std::unordered_map<std::size_t, std::unordered_set<int>> symbolicFields;
    for (const auto& wire : currentModel_.wires()) {
        const auto block = editor_->document()->findBlockByNumber(
            static_cast<int>(wire.sourceLine) - 1);
        if (!block.isValid()) continue;
        const auto source = block.text().toStdString();
        for (auto field = 0; field <= 8; ++field) {
            if (!nec::necCardFieldIsNumeric(source, static_cast<std::size_t>(field)))
                symbolicFields[wire.sourceLine].insert(field);
        }
    }
    wireCardEditor_->setSymbolicGeometryFields(std::move(symbolicFields));
    wireCardEditor_->setModel(currentModel_);
}

void MainWindow::performUndo()
{
    if (!editor_->document()->isUndoAvailable()) return;
    auto entry = sourceUndoHistory_.empty()
        ? SourceHistoryEntry{tr("Raw Source Edit"), EditorDestination::RawSource}
        : sourceUndoHistory_.back();
    if (!sourceUndoHistory_.empty()) sourceUndoHistory_.pop_back();
    suppressSourceHistoryRecording_ = true;
    editor_->undo();
    suppressSourceHistoryRecording_ = false;
    sourceRedoHistory_.push_back(entry);
    checkModel();
    navigateToSourceHistoryEntry(entry);
    statusBar()->showMessage(tr("Undid: %1").arg(entry.description), 5000);
    updateUndoActions();
}

void MainWindow::performRedo()
{
    if (!editor_->document()->isRedoAvailable()) return;
    auto entry = sourceRedoHistory_.empty()
        ? SourceHistoryEntry{tr("Raw Source Edit"), EditorDestination::RawSource}
        : sourceRedoHistory_.back();
    if (!sourceRedoHistory_.empty()) sourceRedoHistory_.pop_back();
    suppressSourceHistoryRecording_ = true;
    editor_->redo();
    suppressSourceHistoryRecording_ = false;
    sourceUndoHistory_.push_back(entry);
    checkModel();
    navigateToSourceHistoryEntry(entry);
    statusBar()->showMessage(tr("Redid: %1").arg(entry.description), 5000);
    updateUndoActions();
}

void MainWindow::recordSourceUndoCommand()
{
    if (suppressSourceHistoryRecording_) return;
    auto entry = pendingSourceHistoryEntry_.value_or(SourceHistoryEntry{
        tr("Raw Source Edit"), EditorDestination::RawSource, 0, 0, 0});
    sourceUndoHistory_.push_back(std::move(entry));
    sourceRedoHistory_.clear();
    updateUndoActions();
}

void MainWindow::resetSourceUndoHistory()
{
    pendingSourceHistoryEntry_.reset();
    sourceUndoHistory_.clear();
    sourceRedoHistory_.clear();
    updateUndoActions();
}

void MainWindow::navigateToSourceHistoryEntry(const SourceHistoryEntry& entry)
{
    showEditor(entry.destination, entry.geometryTab);
    if (entry.destination == EditorDestination::RawSource)
        sourceWorkspace_->setCurrentIndex(entry.sourceTab);
    else if (entry.destination == EditorDestination::Frequency)
        analysisWorkspace_->setCurrentIndex(entry.analysisTab);
}

void MainWindow::updateUndoActions()
{
    const auto historyAvailable = editor_ != nullptr && editor_->document() != nullptr;
    undoAction_->setEnabled(historyAvailable && editor_->document()->isUndoAvailable());
    redoAction_->setEnabled(historyAvailable && editor_->document()->isRedoAvailable());
    undoAction_->setText(historyAvailable && editor_->document()->isUndoAvailable()
            && !sourceUndoHistory_.empty()
        ? tr("&Undo %1").arg(sourceUndoHistory_.back().description)
        : tr("&Undo"));
    redoAction_->setText(historyAvailable && editor_->document()->isRedoAvailable()
            && !sourceRedoHistory_.empty()
        ? tr("&Redo %1").arg(sourceRedoHistory_.back().description)
        : tr("&Redo"));
}

auto MainWindow::activeSource() const -> const nec::NecSourceCache&
{
    activeSourceCache_.update(editor_->toPlainText().toStdString());
    return activeSourceCache_;
}

void MainWindow::updateProjectTree(const model::AntennaModel& antennaModel, const nec::NecDocument& document)
{
    projectTree_->clear();
    const auto projectName = currentFile_.isEmpty() ? tr("Untitled NEC Model") : QFileInfo(currentFile_).fileName();
    auto* root = new QTreeWidgetItem(projectTree_, {projectName});
    root->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    root->setData(0, ItemKindRole, QStringLiteral("model"));

    auto* geometry = new QTreeWidgetItem(root, {tr("Geometry")});
    geometry->setData(0, ItemKindRole, QStringLiteral("geometry"));
    auto* parameters = new QTreeWidgetItem(root, {tr("Parameters")});
    parameters->setData(0, ItemKindRole, QStringLiteral("parameters"));
    auto* environment = new QTreeWidgetItem(root, {tr("Environment")});
    auto* frequencySources = new QTreeWidgetItem(root, {tr("Frequency & Sources")});
    auto* attachments = new QTreeWidgetItem(root, {tr("Loads & Networks")});
    auto* requests = new QTreeWidgetItem(root, {tr("Requests & Execution")});
    auto* comments = new QTreeWidgetItem(root, {tr("Comments")});
    auto* other = new QTreeWidgetItem(root, {tr("Other Cards")});
    auto* allCards = new QTreeWidgetItem(root, {tr("All Cards")});
    for (auto* category : {environment, frequencySources, attachments, requests, comments, other, allCards})
        category->setData(0, ItemKindRole, QStringLiteral("category"));

    const auto categoryFor = [&](const nec::NecCard& card) -> QTreeWidgetItem* {
        const auto* spec = nec::findNecCardSpec(card.mnemonic);
        if (spec == nullptr) return other;
        switch (spec->area) {
        case nec::NecCardArea::Comments: return comments;
        case nec::NecCardArea::Parameters: return parameters;
        case nec::NecCardArea::Geometry: return geometry;
        case nec::NecCardArea::Environment: return environment;
        case nec::NecCardArea::FrequencySources: return frequencySources;
        case nec::NecCardArea::LoadsNetworks: return attachments;
        case nec::NecCardArea::RequestsExecution: return requests;
        case nec::NecCardArea::Extensions: return other;
        }
        return other;
    };
    const auto addCard = [&](QTreeWidgetItem* parent, const nec::NecCard& card, bool generic) {
        const auto mnemonic = QString::fromStdString(card.mnemonic).toUpper();
        auto label = tr("Line %1 · %2").arg(card.lineNumber).arg(
            mnemonic.isEmpty() ? tr("Blank") : QString::fromStdString(card.sourceText).trimmed());
        auto kind = QStringLiteral("card");
        auto wireTag = 0;
        if (card.kind == nec::NecCardKind::GeometryWire) {
            const auto wire = std::ranges::find(antennaModel.wires(), card.lineNumber, &model::Wire::sourceLine);
            if (wire != antennaModel.wires().end()) {
                wireTag = wire->tag;
                if (!generic) {
                    label = tr("GW · Wire %1 (%2 segments) · line %3")
                        .arg(wire->tag).arg(wire->segments).arg(card.lineNumber);
                    kind = QStringLiteral("wire");
                }
            }
        } else if (!generic && card.kind == nec::NecCardKind::Excitation) {
            const auto value = std::ranges::find(currentSetup_.excitations, card.lineNumber,
                &model::Excitation::sourceLine);
            if (value != currentSetup_.excitations.end()) {
                label = tr("EX · Wire %1, segment %2 · line %3")
                    .arg(value->wireTag).arg(value->segment).arg(card.lineNumber);
                kind = QStringLiteral("excitation");
                wireTag = value->wireTag;
            }
        } else if (!generic && card.kind == nec::NecCardKind::Load) {
            const auto value = std::ranges::find(currentSetup_.loads, card.lineNumber,
                &model::LoadDefinition::sourceLine);
            if (value != currentSetup_.loads.end()) {
                label = tr("LD · Wire %1, segments %2–%3 · line %4")
                    .arg(value->wireTag).arg(value->firstSegment).arg(value->lastSegment).arg(card.lineNumber);
                kind = QStringLiteral("load");
                wireTag = value->wireTag;
            }
        } else if (!generic && card.kind == nec::NecCardKind::TransmissionLine) {
            const auto value = std::ranges::find(currentSetup_.transmissionLines, card.lineNumber,
                &model::TransmissionLineDefinition::sourceLine);
            if (value != currentSetup_.transmissionLines.end()) {
                label = tr("TL · W%1/S%2 → W%3/S%4 · line %5")
                    .arg(value->wireTag1).arg(value->segment1).arg(value->wireTag2)
                    .arg(value->segment2).arg(card.lineNumber);
                kind = QStringLiteral("transmissionLine");
            }
        }
        auto* item = new QTreeWidgetItem(parent, {label});
        item->setData(0, ItemKindRole, kind);
        item->setData(0, SourceLineRole, static_cast<qulonglong>(card.lineNumber));
        item->setData(0, CardMnemonicRole, mnemonic);
        item->setData(0, WireTagRole, wireTag);
        item->setToolTip(0, QString::fromStdString(card.sourceText));
    };

    auto cardCount = 0;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Blank) continue;
        addCard(categoryFor(card), card, false);
        addCard(allCards, card, true);
        ++cardCount;
    }
    const auto labelCategory = [](QTreeWidgetItem* item, const QString& title) {
        item->setText(0, QStringLiteral("%1 (%2)").arg(title).arg(item->childCount()));
    };
    labelCategory(geometry, tr("Geometry"));
    labelCategory(parameters, tr("Parameters"));
    labelCategory(environment, tr("Environment"));
    labelCategory(frequencySources, tr("Frequency & Sources"));
    labelCategory(attachments, tr("Loads & Networks"));
    labelCategory(requests, tr("Requests & Execution"));
    labelCategory(comments, tr("Comments"));
    labelCategory(other, tr("Other Cards"));
    allCards->setText(0, tr("All Cards (%1)").arg(cardCount));
    projectTree_->expandItem(root);
    projectTree_->expandItem(geometry);
    projectTree_->expandItem(parameters);
    projectTree_->expandItem(environment);
    projectTree_->expandItem(frequencySources);
    projectTree_->expandItem(attachments);
    projectTree_->expandItem(requests);
}

void MainWindow::synchronizeProjectItemSelection(QTreeWidgetItem* item)
{
    if (item == nullptr) return;
    const auto kind = item->data(0, ItemKindRole).toString();
    if (kind == QStringLiteral("wire")) {
        const auto tag = item->data(0, WireTagRole).toInt();
        synchronizeGeometrySelection(tag);
        setupEditor_->selectExcitation(0);
    } else if (kind == QStringLiteral("excitation")) {
        const auto sourceLine = item->data(0, SourceLineRole).toULongLong();
        const auto found = std::ranges::find(currentSetup_.excitations, sourceLine,
            &model::Excitation::sourceLine);
        if (found != currentSetup_.excitations.end()) {
            xyView_->selectExcitation(sourceLine);
            xzView_->selectExcitation(sourceLine);
            yzView_->selectExcitation(sourceLine);
            geometry3DView_->selectExcitation(sourceLine);
            wireCardEditor_->selectWire(-1);
            setupEditor_->selectExcitation(sourceLine);
        }
    } else if (kind == QStringLiteral("load")) {
        const auto sourceLine = item->data(0, SourceLineRole).toULongLong();
        const auto found = std::ranges::find(currentSetup_.loads, sourceLine,
            &model::LoadDefinition::sourceLine);
        if (found != currentSetup_.loads.end()) {
            xyView_->selectLoad(sourceLine); xzView_->selectLoad(sourceLine);
            yzView_->selectLoad(sourceLine); geometry3DView_->selectLoad(sourceLine);
            loadNetworkEditor_->selectLoad(sourceLine);
        }
    } else if (kind == QStringLiteral("transmissionLine")) {
        const auto sourceLine = item->data(0, SourceLineRole).toULongLong();
        const auto found = std::ranges::find(currentSetup_.transmissionLines, sourceLine,
            &model::TransmissionLineDefinition::sourceLine);
        if (found != currentSetup_.transmissionLines.end()) {
            xyView_->selectTransmissionLine(sourceLine); xzView_->selectTransmissionLine(sourceLine);
            yzView_->selectTransmissionLine(sourceLine); geometry3DView_->selectTransmissionLine(sourceLine);
            loadNetworkEditor_->selectTransmissionLine(sourceLine);
        }
    }
}

void MainWindow::setCurrentFile(QString path)
{
    const auto modelChanged = currentFile_ != path;
    currentFile_ = std::move(path);
    if (modelChanged && resultReferenceController_.hasReference()) clearResultReference();
    if (!currentFile_.isEmpty()) rememberRecentFile(currentFile_);
    const auto name = currentFile_.isEmpty() ? tr("Untitled") : QFileInfo(currentFile_).fileName();
    setWindowTitle(tr("%1[*] — NEC Workbench").arg(name));
    const auto modelName = currentFile_.isEmpty() ? tr("Untitled model.nec") : name;
    modelFileStatus_->setText(tr("Model: %1").arg(modelName));
    modelFileStatus_->setToolTip(currentFile_.isEmpty() ? tr("This model has not been saved yet.") : currentFile_);
    if (controlPanel_ != nullptr)
        controlPanel_->setDocumentState(currentFile_.isEmpty() ? QString{} : name,
            editor_->document()->isModified());
    if (modelPanel_ != nullptr) modelPanel_->setStatusText(modelName);
    if (geometryPanel_ != nullptr) geometryPanel_->setStatusText(modelName);
    if (setupPanel_ != nullptr) setupPanel_->setStatusText(modelName);
    if (optimizationPanel_ != nullptr) optimizationPanel_->setStatusText(
        tr("%1 · %2").arg(modelName,
            optimizationWorkspace_->isRunning() ? tr("Running") : tr("Ready")));
    if (hasNecModel_) QTimer::singleShot(0, this, [this] { fitWindowToDashboard(); });
}

void MainWindow::restoreWorkspaceLayout()
{
    QSettings settings;
    const auto savedGeometry = settings.value(QStringLiteral("mainWindow/geometry")).toByteArray();
    if (!savedGeometry.isEmpty()) {
        restoreGeometry(savedGeometry);
    } else {
        const auto normalGeometry = settings.value(QStringLiteral("mainWindow/normalGeometry")).toRect();
        if (normalGeometry.isValid()) setGeometry(normalGeometry);
    }
    const auto savedState = settings.value(QStringLiteral("mainWindow/state")).toByteArray();
    restoreState(savedState);
    if (savedState.isEmpty()) resetWorkspaceLayout();
    const auto modelCheckGeometry = settings.value(
        QStringLiteral("modelCheckWindow/geometry")).toByteArray();
    if (!modelCheckGeometry.isEmpty()) modelCheckWindow_->restoreGeometry(modelCheckGeometry);
    const auto runMonitorGeometry = settings.value(
        QStringLiteral("runMonitorWindow/geometry")).toByteArray();
    if (!runMonitorGeometry.isEmpty()) runMonitorWindow_->restoreGeometry(runMonitorGeometry);
    auto geometrySettings = geometrySettings_;
    geometrySettings.lengthUnit = static_cast<model::LengthUnit>(settings.value(
        QStringLiteral("model/displayLengthUnit"), static_cast<int>(model::LengthUnit::Meter)).toInt());
    geometrySettings.automaticGridSpacing = settings.value(
        QStringLiteral("geometry/automaticGridSpacing"), true).toBool();
    geometrySettings.manualGridSpacingMeters = settings.value(
        QStringLiteral("geometry/manualGridSpacingMeters"), 1.0).toDouble();
    geometrySettings.minorGridDivisions = settings.value(
        QStringLiteral("geometry/minorGridDivisions"), 5).toInt();
    geometrySettings.showGrid = settings.value(QStringLiteral("geometry/showGrid"), true).toBool();
    geometrySettings.showAxes = settings.value(QStringLiteral("geometry/showAxes"), true).toBool();
    geometrySettings.showLabels = settings.value(QStringLiteral("geometry/showLabels"), true).toBool();
    geometrySettings.gridSnapping = settings.value(QStringLiteral("geometry/gridSnapping"), true).toBool();
    geometrySettings.snapSpacingMeters = settings.value(
        QStringLiteral("model/snapSpacingMeters"), 0.5).toDouble();
    const auto savedSnapUnitBehavior = settings.value(
        QStringLiteral("geometry/snapUnitBehavior"), static_cast<int>(SnapUnitBehavior::UnitFriendly)).toInt();
    geometrySettings.snapUnitBehavior = savedSnapUnitBehavior == static_cast<int>(SnapUnitBehavior::PreservePhysical)
        ? SnapUnitBehavior::PreservePhysical
        : SnapUnitBehavior::UnitFriendly;
    if (geometrySettings.snapUnitBehavior == SnapUnitBehavior::UnitFriendly) {
        const auto displayedSnap = model::fromMeters(
            geometrySettings.snapSpacingMeters, geometrySettings.lengthUnit);
        geometrySettings.snapSpacingMeters = model::toMeters(
            model::niceEngineeringStep(displayedSnap), geometrySettings.lengthUnit);
    }
    geometrySettings.endpointSnapping = settings.value(
        QStringLiteral("geometry/endpointSnapping"), true).toBool();
    geometrySettings.endpointTolerancePixels = settings.value(
        QStringLiteral("geometry/endpointTolerancePixels"), 12.0).toDouble();
    applyGeometrySettings(geometrySettings);
    solverBackendId_ = settings.value(QStringLiteral("analysis/backendId"), QStringLiteral("nec2")).toString();
    const auto legacyExecutablePath = settings.value(
        QStringLiteral("analysis/executablePath")).toString();
    for (const auto& backendId : {QStringLiteral("nec2"), QStringLiteral("opennec"),
             QStringLiteral("nec2dxs")}) {
        solverExecutablePaths_.insert(backendId, settings.value(
            QStringLiteral("analysis/executables/%1").arg(backendId)).toString());
    }
    if (solverExecutablePaths_.value(solverBackendId_).isEmpty()
        && !legacyExecutablePath.isEmpty()) {
        solverExecutablePaths_.insert(solverBackendId_, legacyExecutablePath);
    }
    solverTimeoutSeconds_ = settings.value(QStringLiteral("analysis/timeoutSeconds"), 120).toInt();
    if (solverExecutablePaths_.value(QStringLiteral("nec2")).isEmpty())
        solverExecutablePaths_.insert(QStringLiteral("nec2"),
            QStandardPaths::findExecutable(QStringLiteral("nec2c")));
    if (solverExecutablePaths_.value(QStringLiteral("opennec")).isEmpty())
        solverExecutablePaths_.insert(QStringLiteral("opennec"),
            QStandardPaths::findExecutable(QStringLiteral("onec")));
    solverExecutablePath_ = solverExecutablePaths_.value(solverBackendId_);
    analysisSetupEditor_->setSettings(
        solverBackendId_, solverExecutablePaths_, solverTimeoutSeconds_);
    updateAnalysisReadiness();
}

void MainWindow::saveWorkspaceLayout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/normalGeometry"), normalGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
    settings.setValue(QStringLiteral("modelCheckWindow/geometry"),
        modelCheckWindow_->saveGeometry());
    settings.setValue(QStringLiteral("runMonitorWindow/geometry"),
        runMonitorWindow_->saveGeometry());
    settings.setValue(QStringLiteral("model/displayLengthUnit"), static_cast<int>(geometrySettings_.lengthUnit));
    settings.setValue(QStringLiteral("model/snapSpacingMeters"), geometrySettings_.snapSpacingMeters);
    settings.setValue(QStringLiteral("geometry/automaticGridSpacing"), geometrySettings_.automaticGridSpacing);
    settings.setValue(QStringLiteral("geometry/manualGridSpacingMeters"), geometrySettings_.manualGridSpacingMeters);
    settings.setValue(QStringLiteral("geometry/minorGridDivisions"), geometrySettings_.minorGridDivisions);
    settings.setValue(QStringLiteral("geometry/showGrid"), geometrySettings_.showGrid);
    settings.setValue(QStringLiteral("geometry/showAxes"), geometrySettings_.showAxes);
    settings.setValue(QStringLiteral("geometry/showLabels"), geometrySettings_.showLabels);
    settings.setValue(QStringLiteral("geometry/gridSnapping"), geometrySettings_.gridSnapping);
    settings.setValue(QStringLiteral("geometry/snapUnitBehavior"), static_cast<int>(geometrySettings_.snapUnitBehavior));
    settings.setValue(QStringLiteral("geometry/endpointSnapping"), geometrySettings_.endpointSnapping);
    settings.setValue(QStringLiteral("geometry/endpointTolerancePixels"), geometrySettings_.endpointTolerancePixels);
    settings.setValue(QStringLiteral("analysis/backendId"), solverBackendId_);
    for (auto iterator = solverExecutablePaths_.cbegin();
         iterator != solverExecutablePaths_.cend(); ++iterator) {
        settings.setValue(QStringLiteral("analysis/executables/%1").arg(iterator.key()),
            iterator.value());
    }
    settings.remove(QStringLiteral("analysis/executablePath"));
    settings.setValue(QStringLiteral("analysis/timeoutSeconds"), solverTimeoutSeconds_);
}

}
