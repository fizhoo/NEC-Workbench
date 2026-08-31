#include "ui/MainWindow.h"

#include "analysis/SolverCommand.h"
#include "analysis/NecOutputParser.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/DeckGeometryUnits.h"
#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
#include "nec/NecSymbolResolver.h"
#include "nec/NecWriter.h"
#include "model/WireGauge.h"
#include "ui/commands/GeometrySourceCommand.h"
#include "ui/commands/MoveEndpointCommand.h"
#include "ui/commands/MoveWireCommand.h"
#include "ui/dashboard/DashboardPage.h"
#include "ui/cards/WireCardEditor.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/analysis/AnalysisSetupEditor.h"
#include "ui/analysis/AnalysisRequestEditor.h"
#include "ui/analysis/AverageGainResultsView.h"
#include "ui/analysis/ConvergenceWorkspace.h"
#include "ui/analysis/ImpedanceResultsView.h"
#include "ui/analysis/ResultsSummaryView.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/editor/NecEditor.h"
#include "ui/editor/NecHighlighter.h"
#include "ui/geometry/EngineeringSpinBox.h"
#include "ui/geometry/AutoSegmentationDialog.h"
#include "ui/geometry/Geometry3DView.h"
#include "ui/geometry/GeometryView.h"
#include "ui/geometry/GeometrySettingsDialog.h"
#include "ui/geometry/WirePropertiesDialog.h"
#include "ui/optimization/OptimizationWorkspace.h"
#include "ui/setup/SetupEditor.h"
#include "ui/setup/LoadNetworkEditor.h"
#include "ui/setup/ExcitationPropertiesDialog.h"
#include "ui/welcome/WelcomePage.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QMenuBar>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <span>
#include <string_view>
#include <utility>

namespace necwb::ui {
namespace {

constexpr auto ItemKindRole = Qt::UserRole;
constexpr auto WireTagRole = Qt::UserRole + 1;
constexpr auto SourceLineRole = Qt::UserRole + 2;

auto sameResultFrequency(double first, double second) -> bool
{
    return std::abs(first - second) <= 1.0e-9 * std::max({1.0, std::abs(first), std::abs(second)});
}
constexpr auto RunDirectoryRole = Qt::UserRole + 3;
constexpr auto RunContextRole = Qt::UserRole + 4;
constexpr auto CardMnemonicRole = Qt::UserRole + 5;
constexpr auto RunTypeRole = Qt::UserRole + 6;
constexpr auto RunIdRole = Qt::UserRole + 7;

enum RunColumn {
    RunStartedColumn,
    RunModelColumn,
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
    if (bytes >= mebibyte) return QObject::tr("%1 MB").arg(bytes / mebibyte, 0, 'f', 1);
    if (bytes >= kibibyte) return QObject::tr("%1 KB").arg(bytes / kibibyte, 0, 'f', 1);
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
    std::vector<double> frequencies;
    const auto addFrequency = [&frequencies](double frequencyMHz) {
        if (std::ranges::find_if(frequencies, [frequencyMHz](double existing) {
                return sameResultFrequency(existing, frequencyMHz);
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
    return QObject::tr("Model: %1 · Run: %2 · Backend: %3").arg(modelName, started, backend);
}

auto averageGainEnvironmentId(analysis::AverageGainEnvironment environment) -> QString
{
    return environment == analysis::AverageGainEnvironment::PerfectGround
        ? QStringLiteral("perfect-ground") : QStringLiteral("free-space");
}

auto averageGainEnvironmentFromId(const QString& id) -> analysis::AverageGainEnvironment
{
    return id == QStringLiteral("perfect-ground")
        ? analysis::AverageGainEnvironment::PerfectGround
        : analysis::AverageGainEnvironment::FreeSpace;
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

auto cardPropertyLabels(const QString& mnemonic) -> QStringList
{
    if (mnemonic == QStringLiteral("GW")) return {QObject::tr("Tag"), QObject::tr("Segments"),
        QObject::tr("X1"), QObject::tr("Y1"), QObject::tr("Z1"), QObject::tr("X2"),
        QObject::tr("Y2"), QObject::tr("Z2"), QObject::tr("Radius")};
    if (mnemonic == QStringLiteral("GS")) return {QObject::tr("I1 (unused)"),
        QObject::tr("I2 (unused)"), QObject::tr("Scale to meters")};
    if (mnemonic == QStringLiteral("EX")) return {QObject::tr("Type"), QObject::tr("Wire Tag"),
        QObject::tr("Segment"), QObject::tr("I4"), QObject::tr("Real"), QObject::tr("Imaginary"),
        QObject::tr("F7"), QObject::tr("F8"), QObject::tr("F9"), QObject::tr("F10")};
    if (mnemonic == QStringLiteral("FR")) return {QObject::tr("Mode"), QObject::tr("Count"),
        QObject::tr("I3"), QObject::tr("I4"), QObject::tr("Start MHz"), QObject::tr("Step/Ratio")};
    if (mnemonic == QStringLiteral("GN")) return {QObject::tr("Type"), QObject::tr("I2"),
        QObject::tr("I3"), QObject::tr("I4"), QObject::tr("Permittivity"), QObject::tr("Conductivity"),
        QObject::tr("F3"), QObject::tr("F4"), QObject::tr("F5"), QObject::tr("F6")};
    if (mnemonic == QStringLiteral("GE")) return {QObject::tr("Ground Flag")};
    if (mnemonic == QStringLiteral("LD")) return {QObject::tr("Type"), QObject::tr("Wire Tag"),
        QObject::tr("First Segment"), QObject::tr("Last Segment"), QObject::tr("Value 1"),
        QObject::tr("Value 2"), QObject::tr("Value 3")};
    if (mnemonic == QStringLiteral("TL")) return {QObject::tr("Wire 1"), QObject::tr("Segment 1"),
        QObject::tr("Wire 2"), QObject::tr("Segment 2"), QObject::tr("Z0"), QObject::tr("Length"),
        QObject::tr("Shunt R1"), QObject::tr("Shunt X1"), QObject::tr("Shunt R2"), QObject::tr("Shunt X2")};
    if (mnemonic == QStringLiteral("RP")) return {QObject::tr("Mode"), QObject::tr("Theta Count"),
        QObject::tr("Phi Count"), QObject::tr("Format"), QObject::tr("Theta Start"),
        QObject::tr("Phi Start"), QObject::tr("Theta Step"), QObject::tr("Phi Step"),
        QObject::tr("Distance"), QObject::tr("Normalization")};
    if (mnemonic == QStringLiteral("XQ")) return {QObject::tr("Option")};
    return {};
}

void addPropertyRow(QTableWidget* table, const QString& name, const QString& value)
{
    const auto row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(name));
    table->setItem(row, 1, new QTableWidgetItem(value));
}

void addPropertyWidgetRow(QTableWidget* table, const QString& name, QWidget* editor)
{
    const auto row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(name));
    table->setCellWidget(row, 1, editor);
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

}

MainWindow::MainWindow()
{
    setWindowTitle(tr("Untitled[*] — NEC Workbench"));
    setWindowModified(false);
    resize(1280, 820);

    undoStack_ = new QUndoStack(this);
    createActions();
    createWorkspace();
    createDocks();
    createMenusAndToolbar();

    modelFileStatus_ = new QLabel(this);
    modelFileStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(modelFileStatus_, 1);
    checkStatus_ = new QLabel(tr("No model loaded"), this);
    statusBar()->addPermanentWidget(checkStatus_);

    connect(editor_, &QPlainTextEdit::textChanged, this, [this] { clearCheckResults(); });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        showCardProperties(static_cast<std::size_t>(editor_->textCursor().blockNumber()+1));
    });
    connect(editor_->document(), &QTextDocument::modificationChanged, this, [this](bool modified) {
        saveAction_->setEnabled(modified);
        setWindowModified(modified);
        sourceWorkspace_->setTabText(0, modified ? tr("NEC Source *") : tr("NEC Source"));
        if (dashboardPage_ != nullptr)
            dashboardPage_->setDocumentState(currentFile_.isEmpty()
                    ? QString{} : QFileInfo(currentFile_).fileName(), modified);
    });
    connect(editor_->document(), &QTextDocument::undoAvailable, this, [this] { updateUndoActions(); });
    connect(editor_->document(), &QTextDocument::redoAvailable, this, [this] { updateUndoActions(); });
    connect(undoStack_, &QUndoStack::canUndoChanged, this, [this] { updateUndoActions(); });
    connect(undoStack_, &QUndoStack::canRedoChanged, this, [this] { updateUndoActions(); });
    connect(workspace_, &QTabWidget::currentChanged, this, [this] { updateUndoActions(); });
    connect(sourceWorkspace_, &QTabWidget::currentChanged, this, [this] { updateUndoActions(); });
    connect(moduleStack_, &QStackedWidget::currentChanged, this, [this] { updateUndoActions(); });

    restoreWorkspaceLayout();
    updateUndoActions();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (solverProcess_ != nullptr || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning()) {
        const auto answer = QMessageBox::question(this, tr("Solver Running"),
            tr("A solver task is still active. Cancel it and close NEC Workbench?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        if (solverProcess_ != nullptr) {
            solverProcess_->kill();
            solverProcess_->waitForFinished(2000);
        }
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
    newAction_->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    newAction_->setShortcut(QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, [this] { newModel(); });

    openAction_ = new QAction(tr("&Open NEC File…"), this);
    openAction_->setObjectName(QStringLiteral("openNecFileAction"));
    openAction_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, [this] { openFile(); });

    saveAction_ = new QAction(tr("&Save"), this);
    saveAction_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setEnabled(false);
    connect(saveAction_, &QAction::triggered, this, [this] { saveFile(); });

    saveAsAction_ = new QAction(tr("Save &As…"), this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this] { saveFileAs(); });

    checkAction_ = new QAction(tr("&Check Model"), this);
    checkAction_->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    checkAction_->setShortcut(QKeySequence(Qt::Key_F7));
    checkAction_->setStatusTip(tr("Check NEC cards and model geometry"));
    connect(checkAction_, &QAction::triggered, this, [this] { checkModel(); });

    runAction_ = new QAction(tr("&Run Analysis"), this);
    runAction_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    runAction_->setEnabled(false);
    runAction_->setStatusTip(tr("Run the checked model with the selected NEC engine"));
    connect(runAction_, &QAction::triggered, this, [this] { startAnalysis(); });

    averageGainAction_ = new QAction(tr("Run &Average Gain Test…"), this);
    averageGainAction_->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    averageGainAction_->setEnabled(false);
    averageGainAction_->setStatusTip(tr(
        "Run a single-frequency lossless Average Gain Test without modifying the source model"));
    connect(averageGainAction_, &QAction::triggered, this, [this] { startAverageGainTest(); });

    convergenceAction_ = new QAction(tr("Segmentation &Convergence…"), this);
    convergenceAction_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    convergenceAction_->setEnabled(false);
    convergenceAction_->setStatusTip(tr(
        "Open the multi-run segmentation convergence study under Results Validation"));
    connect(convergenceAction_, &QAction::triggered, this, [this] { showConvergenceStudy(); });

    fitGeometryAction_ = new QAction(tr("&Fit Geometry"), this);
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
    connect(geometrySettingsAction_, &QAction::triggered, this, [this] { showGeometrySettings(); });

    autoSegmentationAction_ = new QAction(tr("Automatic Segmentation…"), this);
    autoSegmentationAction_->setStatusTip(tr("Preview wavelength-based wire segment counts"));
    connect(autoSegmentationAction_, &QAction::triggered, this, [this] { showAutoSegmentation(); });

    undoAction_ = new QAction(tr("&Undo"), this);
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] { performUndo(); });

    redoAction_ = new QAction(tr("&Redo"), this);
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] { performRedo(); });

    auto* moduleGroup = new QActionGroup(this);
    moduleGroup->setExclusive(true);
    homeModuleAction_ = moduleGroup->addAction(tr("&Home"));
    modelModuleAction_ = moduleGroup->addAction(tr("&Geometry"));
    sourceModuleAction_ = moduleGroup->addAction(tr("NEC &Source"));
    analysisModuleAction_ = moduleGroup->addAction(tr("&Analysis"));
    visualizeModuleAction_ = moduleGroup->addAction(tr("&Results"));
    optimizeModuleAction_ = moduleGroup->addAction(tr("&Optimize"));
    for (auto* action : moduleGroup->actions()) {
        action->setCheckable(true);
    }
    homeModuleAction_->setChecked(true);
    modelModuleAction_->setEnabled(false);
    sourceModuleAction_->setEnabled(false);
    analysisModuleAction_->setEnabled(false);
    visualizeModuleAction_->setEnabled(true);
    optimizeModuleAction_->setEnabled(false);
    connect(homeModuleAction_, &QAction::triggered, this, [this] { showModule(homeModuleIndex_); });
    connect(modelModuleAction_, &QAction::triggered, this, [this] { showModule(modelModuleIndex_); });
    connect(sourceModuleAction_, &QAction::triggered, this, [this] { showModule(sourceModuleIndex_); });
    connect(analysisModuleAction_, &QAction::triggered, this, [this] { showModule(analysisModuleIndex_); });
    connect(visualizeModuleAction_, &QAction::triggered, this, [this] {
        showModule(visualizeModuleIndex_);
        if (!resultsAvailable_) resultsWorkspace_->setCurrentIndex(analysisRunsTabIndex_);
    });
    connect(optimizeModuleAction_, &QAction::triggered, this, [this] {
        if (historicalSessionViewActive_) {
            leaveHistoricalSessionViews();
            historicalReviewActive_ = false;
            historicalSessionViewActive_ = false;
        }
        showModule(optimizeModuleIndex_);
    });
}

void MainWindow::createWorkspace()
{
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    auto* moduleNavigation = new QToolBar(tr("Workbench Modules"), central);
    moduleNavigation->setObjectName(QStringLiteral("moduleNavigationToolbar"));
    moduleNavigation->setToolButtonStyle(Qt::ToolButtonTextOnly);
    moduleNavigation->addAction(homeModuleAction_);
    moduleNavigation->addSeparator();
    moduleNavigation->addAction(modelModuleAction_);
    moduleNavigation->addAction(sourceModuleAction_);
    moduleNavigation->addAction(analysisModuleAction_);
    moduleNavigation->addAction(visualizeModuleAction_);
    moduleNavigation->addAction(optimizeModuleAction_);
    moduleStack_ = new QStackedWidget(central);
    centralLayout->addWidget(moduleNavigation);
    centralLayout->addWidget(moduleStack_, 1);
    setCentralWidget(central);

    sourceWorkspace_ = new QTabWidget(moduleStack_);
    sourceWorkspace_->setDocumentMode(true);
    editor_ = new NecEditor(sourceWorkspace_);
    editor_->setObjectName(QStringLiteral("necSourceEditor"));
    new NecHighlighter(editor_->document());
    sourceWorkspace_->addTab(editor_, tr("NEC Source"));
    sourceTabIndex_ = -1;

    dashboardStack_ = new QStackedWidget(moduleStack_);
    welcomePage_ = new WelcomePage(newAction_, openAction_,
        [this](const QString& path) { openFileAtPath(path); },
        [this] { openExample(); }, [this] { clearRecentFiles(); }, dashboardStack_);
    welcomePage_->setRecentFiles(recentFiles());
    dashboardStack_->addWidget(welcomePage_);
    dashboardPage_ = new DashboardPage(editor_->document(), dashboardStack_);
    dashboardPage_->setQuickActions(modelModuleAction_, sourceModuleAction_, checkAction_,
        runAction_, visualizeModuleAction_);
    dashboardPage_->setAverageGainAction(averageGainAction_);
    dashboardPage_->setConvergenceAction(convergenceAction_);
    dashboardStack_->addWidget(dashboardPage_);
    homeModuleIndex_ = moduleStack_->addWidget(dashboardStack_);

    auto* structuredContainer = new QWidget(sourceWorkspace_);
    auto* structuredLayout = new QVBoxLayout(structuredContainer);
    structuredLayout->setContentsMargins(8, 8, 8, 8);
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
    connect(structuredCardEditor_, &StructuredCardEditor::cardSelected, this,
        [this](std::size_t sourceLine) {
            editor_->goToLine(sourceLine);
            showCardProperties(sourceLine);
        });
    connect(structuredCardEditor_, &StructuredCardEditor::cardEdited, this,
        [this](std::size_t sourceLine, const QString& cardText) {
            editStructuredCard(sourceLine, cardText);
        });
    connect(structuredCardEditor_, &StructuredCardEditor::cardAddRequested, this,
        [this](const QString& cardText) { addStructuredCard(cardText); });
    connect(structuredCardEditor_, &StructuredCardEditor::cardDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteStructuredCard(sourceLine); });

    workspace_ = new QTabWidget(moduleStack_);
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
    snapSpacingControl_->setDecimals(6);
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
    connect(xyView_, &GeometryView::wireSelected, this, [this](int tag) { selectWireInProject(tag); });
    connect(xzView_, &GeometryView::wireSelected, this, [this](int tag) { selectWireInProject(tag); });
    connect(yzView_, &GeometryView::wireSelected, this, [this](int tag) { selectWireInProject(tag); });
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
        connect(view, &GeometryView::wirePropertiesRequested, this,
            [this](int tag) { showWireProperties(tag); });
        connect(view, &GeometryView::excitationSelected, this,
            [this](std::size_t sourceLine) { selectExcitation(sourceLine); });
        connect(view, &GeometryView::addExcitationRequested, this,
            [this](int wireTag, int segment) { addExcitationAt(wireTag, segment); });
        connect(view, &GeometryView::editExcitationRequested, this,
            [this](std::size_t sourceLine) { showExcitationEditor(sourceLine); });
        connect(view, &GeometryView::openExcitationSetupRequested, this,
            [this](std::size_t sourceLine) { showExcitationInSetup(sourceLine); });
        connect(view, &GeometryView::deleteExcitationRequested, this,
            [this](std::size_t sourceLine) { deleteExcitation(sourceLine); });
        connect(view, &GeometryView::loadSelected, this,
            [this](std::size_t sourceLine) { selectLoad(sourceLine); });
        connect(view, &GeometryView::transmissionLineSelected, this,
            [this](std::size_t sourceLine) { selectTransmissionLine(sourceLine); });
        connect(view, &GeometryView::addLoadRequested, this,
            [this](int wireTag, int segment) { addLoadAt(wireTag, segment); });
        connect(view, &GeometryView::transmissionLineEndpointRequested, this,
            [this](int wireTag, int segment) { chooseTransmissionLineEndpoint(wireTag, segment); });
        connect(view, &GeometryView::cancelTransmissionLineRequested, this,
            [this] { setPendingTransmissionLineEndpoint(std::nullopt); });
        connect(view, &GeometryView::editLoadRequested, this,
            [this](std::size_t sourceLine) { showLoadInEditor(sourceLine); });
        connect(view, &GeometryView::editTransmissionLineRequested, this,
            [this](std::size_t sourceLine) { showTransmissionLineInEditor(sourceLine); });
        connect(view, &GeometryView::deleteLoadRequested, this,
            [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete load"), sourceLine); });
        connect(view, &GeometryView::deleteTransmissionLineRequested, this,
            [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete transmission line"), sourceLine); });
    };
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
    connect(geometry3DView_, &Geometry3DView::wireSelected, this,
        [this](int tag) { selectWireInProject(tag); });
    connect(geometry3DView_, &Geometry3DView::wirePropertiesRequested, this,
        [this](int tag) { showWireProperties(tag); });
    connect(geometry3DView_, &Geometry3DView::excitationSelected, this,
        [this](std::size_t sourceLine) { selectExcitation(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::addExcitationRequested, this,
        [this](int wireTag, int segment) { addExcitationAt(wireTag, segment); });
    connect(geometry3DView_, &Geometry3DView::editExcitationRequested, this,
        [this](std::size_t sourceLine) { showExcitationEditor(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::openExcitationSetupRequested, this,
        [this](std::size_t sourceLine) { showExcitationInSetup(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::deleteExcitationRequested, this,
        [this](std::size_t sourceLine) { deleteExcitation(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::loadSelected, this,
        [this](std::size_t sourceLine) { selectLoad(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::transmissionLineSelected, this,
        [this](std::size_t sourceLine) { selectTransmissionLine(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::addLoadRequested, this,
        [this](int wireTag, int segment) { addLoadAt(wireTag, segment); });
    connect(geometry3DView_, &Geometry3DView::transmissionLineEndpointRequested, this,
        [this](int wireTag, int segment) { chooseTransmissionLineEndpoint(wireTag, segment); });
    connect(geometry3DView_, &Geometry3DView::cancelTransmissionLineRequested, this,
        [this] { setPendingTransmissionLineEndpoint(std::nullopt); });
    connect(geometry3DView_, &Geometry3DView::editLoadRequested, this,
        [this](std::size_t sourceLine) { showLoadInEditor(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::editTransmissionLineRequested, this,
        [this](std::size_t sourceLine) { showTransmissionLineInEditor(sourceLine); });
    connect(geometry3DView_, &Geometry3DView::deleteLoadRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete load"), sourceLine); });
    connect(geometry3DView_, &Geometry3DView::deleteTransmissionLineRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete transmission line"), sourceLine); });
    workspace_->addTab(geometry3DPage, tr("3D Geometry"));
    modelModuleIndex_ = moduleStack_->addWidget(workspace_);
    sourceModuleIndex_ = moduleStack_->addWidget(sourceWorkspace_);

    analysisWorkspace_ = new QTabWidget(moduleStack_);
    analysisWorkspace_->setObjectName(QStringLiteral("analysisWorkspace"));
    analysisWorkspace_->setDocumentMode(true);

    modelSetupWorkspace_ = new QTabWidget(analysisWorkspace_);
    modelSetupWorkspace_->setObjectName(QStringLiteral("modelSetupWorkspace"));
    modelSetupWorkspace_->setDocumentMode(true);
    analysisWorkspace_->addTab(modelSetupWorkspace_, tr("Model Setup"));
    setupTabIndex_ = -2;

    setupEditor_ = new SetupEditor(modelSetupWorkspace_);
    connect(setupEditor_, &SetupEditor::frequencyChanged, this,
        [this](model::FrequencyDefinition frequency) { changeFrequency(frequency); });
    connect(setupEditor_, &SetupEditor::frequencyDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteFrequency(sourceLine); });
    connect(setupEditor_, &SetupEditor::groundChanged, this,
        [this](model::GroundDefinition ground) { changeGround(ground); });
    connect(setupEditor_, &SetupEditor::excitationChanged, this,
        [this](model::Excitation excitation) { changeExcitation(excitation); });
    connect(setupEditor_, &SetupEditor::excitationDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteExcitation(sourceLine); });
    connect(setupEditor_, &SetupEditor::excitationSelected, this,
        [this](std::size_t sourceLine) { selectExcitation(sourceLine); });
    modelSetupWorkspace_->addTab(setupEditor_, tr("Frequency, Ground && Sources"));

    loadNetworkEditor_ = new LoadNetworkEditor(modelSetupWorkspace_);
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadChanged, this,
        [this](model::LoadDefinition load) { changeLoad(load); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete load"), sourceLine); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineChanged, this,
        [this](model::TransmissionLineDefinition line) { changeTransmissionLine(line); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete transmission line"), sourceLine); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadSelected, this,
        [this](std::size_t sourceLine) { selectLoad(sourceLine); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineSelected, this,
        [this](std::size_t sourceLine) { selectTransmissionLine(sourceLine); });
    loadNetworkTabIndex_ = modelSetupWorkspace_->addTab(
        loadNetworkEditor_, tr("Loads && Transmission Lines"));

    analysisSetupEditor_ = new AnalysisSetupEditor(analysisWorkspace_);
    connect(analysisSetupEditor_, &AnalysisSetupEditor::settingsChanged, this,
        [this](QString backendId, QString executablePath, int timeoutSeconds) {
            solverBackendId_ = std::move(backendId);
            solverExecutablePath_ = std::move(executablePath);
            solverTimeoutSeconds_ = timeoutSeconds;
            optimizationWorkspace_->setContext(editor_->toPlainText(), currentFile_,
                solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
                modelChecked_ && modelErrorCount_ == 0);
            convergenceWorkspace_->setContext(editor_->toPlainText(), currentFile_,
                solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
                modelChecked_ && modelErrorCount_ == 0);
            dashboardPage_->setModel(currentModel_, currentSetup_, solverBackendId_, modelChecked_,
                modelErrorCount_, modelWarningCount_);
            runAction_->setStatusTip(solverExecutablePath_.isEmpty()
                    ? tr("Select a NEC engine executable in Analyze Setup")
                    : tr("Run the checked model with the selected NEC engine"));
            updateAnalysisReadiness();
        });
    analysisWorkspace_->addTab(analysisSetupEditor_, tr("Solver"));
    analysisRequestEditor_ = new AnalysisRequestEditor(analysisWorkspace_);
    connect(analysisRequestEditor_, &AnalysisRequestEditor::requestsChanged, this,
        [this](bool executionEnabled, model::ExecutionRequest execution,
            bool patternEnabled, model::RadiationPatternRequest pattern) {
            changeAnalysisRequests(executionEnabled, execution, patternEnabled, pattern);
        });
    analysisWorkspace_->addTab(analysisRequestEditor_, tr("Requests"));

    auto* runsPage = new QWidget(moduleStack_);
    auto* runsLayout = new QVBoxLayout(runsPage);
    runsLayout->setContentsMargins(16, 16, 16, 16);
    auto* runsHeading = new QLabel(tr("Runs"), runsPage);
    auto runsHeadingFont = runsHeading->font();
    runsHeadingFont.setBold(true);
    runsHeadingFont.setPointSize(runsHeadingFont.pointSize() + 3);
    runsHeading->setFont(runsHeadingFont);
    auto* runsDescription = new QLabel(tr(
        "Analysis runs appear individually. Optimization candidates are grouped under one session row. "
        "View or double-click a row to inspect its archived results without changing the active model."), runsPage);
    runsDescription->setWordWrap(true);
    analysisRuns_ = new QTableWidget(0, RunColumnCount, runsPage);
    analysisRuns_->setObjectName(QStringLiteral("analysisRunsTable"));
    analysisRuns_->setHorizontalHeaderLabels({tr("Started"), tr("Model"), tr("Type"), tr("Results"),
        tr("Output"), tr("Backend"), tr("Status"), tr("Duration"), tr("Run Folder")});
    analysisRuns_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    analysisRuns_->setSelectionBehavior(QAbstractItemView::SelectRows);
    analysisRuns_->setSelectionMode(QAbstractItemView::SingleSelection);
    analysisRuns_->setAlternatingRowColors(true);
    analysisRuns_->horizontalHeader()->setStretchLastSection(true);
    auto* runViewButtons = new QHBoxLayout;
    auto* runManagementButtons = new QHBoxLayout;
    cancelRunButton_ = new QPushButton(tr("Cancel Active Run"), runsPage);
    cancelRunButton_->setEnabled(false);
    openRunResultsButton_ = new QPushButton(tr("View Run Results"), runsPage);
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
    deleteRunButton_ = new QPushButton(tr("Delete Run…"), runsPage);
    deleteRunButton_->setObjectName(QStringLiteral("deleteRunButton"));
    deleteRunButton_->setEnabled(false);
    runViewButtons->addWidget(openRunResultsButton_);
    runViewButtons->addWidget(inspectRunInputButton_);
    runViewButtons->addWidget(openRunSnapshotButton_);
    runViewButtons->addStretch();
    runManagementButtons->addWidget(cancelRunButton_);
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
        this, [this](int, int) { loadSelectedRun(); });

    analysisModuleIndex_ = moduleStack_->addWidget(analysisWorkspace_);

    auto* resultsPage = new QWidget(moduleStack_);
    auto* resultsLayout = new QVBoxLayout(resultsPage);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    auto* resultsContextFrame = new QFrame(resultsPage);
    resultsContextFrame->setObjectName(QStringLiteral("resultsContextBanner"));
    resultsContextFrame->setFrameShape(QFrame::StyledPanel);
    auto* resultsContextLayout = new QVBoxLayout(resultsContextFrame);
    resultsContextLayout->setContentsMargins(12, 8, 12, 8);
    auto* resultsContextHeading = new QHBoxLayout;
    resultsContextTitleLabel_ = new QLabel(tr("No Results Loaded"), resultsContextFrame);
    resultsContextTitleLabel_->setObjectName(QStringLiteral("resultsContextTitleLabel"));
    auto resultsContextFont = resultsContextTitleLabel_->font();
    resultsContextFont.setBold(true);
    resultsContextTitleLabel_->setFont(resultsContextFont);
    returnToActiveResultsButton_ = new QPushButton(tr("Return to Current Work"), resultsContextFrame);
    returnToActiveResultsButton_->setObjectName(QStringLiteral("returnToActiveResultsButton"));
    returnToActiveResultsButton_->setVisible(false);
    resultsContextHeading->addWidget(resultsContextTitleLabel_, 1);
    resultsContextHeading->addWidget(returnToActiveResultsButton_);
    resultsContextDetailLabel_ = new QLabel(
        tr("Select a run from Results → Runs, or analyze the active model."), resultsContextFrame);
    resultsContextDetailLabel_->setObjectName(QStringLiteral("resultsContextDetailLabel"));
    resultsContextDetailLabel_->setWordWrap(true);
    activeModelResultsLabel_ = new QLabel(tr("Active Model: None"), resultsContextFrame);
    activeModelResultsLabel_->setObjectName(QStringLiteral("activeModelResultsLabel"));
    resultsContextLayout->addLayout(resultsContextHeading);
    resultsContextLayout->addWidget(resultsContextDetailLabel_);
    resultsContextLayout->addWidget(activeModelResultsLabel_);
    connect(returnToActiveResultsButton_, &QPushButton::clicked,
        this, [this] { returnToCurrentWork(); });
    resultsStatusLabel_ = new QLabel(tr("No analysis results are loaded."), resultsPage);
    resultsStatusLabel_->setContentsMargins(12, 7, 12, 7);
    resultsStatusLabel_->setWordWrap(true);
    auto* frequencyBar = new QHBoxLayout;
    auto* frequencyLabel = new QLabel(tr("Result Frequency:"), resultsPage);
    resultsFrequencyControl_ = new QComboBox(resultsPage);
    resultsFrequencyControl_->setObjectName(QStringLiteral("resultsFrequencyControl"));
    resultsFrequencyControl_->setMinimumContentsLength(24);
    resultsFrequencyControl_->setEnabled(false);
    resultsAvailabilityLabel_ = new QLabel(tr("No result data available."), resultsPage);
    resultsAvailabilityLabel_->setObjectName(QStringLiteral("resultsAvailabilityLabel"));
    frequencyBar->addWidget(frequencyLabel);
    frequencyBar->addWidget(resultsFrequencyControl_);
    frequencyBar->addSpacing(12);
    frequencyBar->addWidget(resultsAvailabilityLabel_, 1);
    resultsWorkspace_ = new QTabWidget(resultsPage);
    resultsWorkspace_->setDocumentMode(true);
    resultsSummaryView_ = new ResultsSummaryView(resultsWorkspace_);
    resultsSummaryTabIndex_ = resultsWorkspace_->addTab(resultsSummaryView_, tr("Summary"));
    validationWorkspace_ = new QTabWidget(resultsWorkspace_);
    validationWorkspace_->setDocumentMode(true);
    averageGainResultsView_ = new AverageGainResultsView(validationWorkspace_);
    averageGainValidationTabIndex_ = validationWorkspace_->addTab(
        averageGainResultsView_, tr("Average Gain Test"));
    convergenceWorkspace_ = new ConvergenceWorkspace(validationWorkspace_);
    convergenceValidationTabIndex_ = validationWorkspace_->addTab(
        convergenceWorkspace_, tr("Segmentation Convergence"));
    averageGainResultsTabIndex_ = resultsWorkspace_->addTab(validationWorkspace_, tr("Validation"));
    convergenceWorkspace_->setRunsChangedCallback([this] { loadRunHistory(); });
    convergenceWorkspace_->setRunningChangedCallback([this] { synchronizeRunnerState(); });
    convergenceWorkspace_->setSummaryChangedCallback([this](const QString& summary) {
        dashboardPage_->setConvergenceState(summary);
        resultsAvailable_ = true;
        resultsStatusLabel_->setText(tr("Model validation — %1").arg(summary));
    });
    convergenceWorkspace_->setReturnToCurrentWorkCallback([this] { returnToCurrentWork(); });

    auto* impedanceTabs = new QTabWidget(resultsWorkspace_);
    impedanceTabs->setDocumentMode(true);
    analysisResultsView_ = new ImpedanceResultsView(impedanceTabs);
    impedanceTabs->addTab(analysisResultsView_, tr("Table"));
    visualizePlotsView_ = new SweepPlotsView(impedanceTabs);
    impedanceTabs->addTab(visualizePlotsView_, tr("Plots"));
    resultsWorkspace_->addTab(impedanceTabs, tr("Impedance"));

    currentResultsView_ = new CurrentDistributionView(resultsWorkspace_);
    resultsWorkspace_->addTab(currentResultsView_, tr("Currents"));

    auto* radiationTabs = new QTabWidget(resultsWorkspace_);
    radiationTabs->setDocumentMode(true);
    radiationPatternView_ = new RadiationPatternView(radiationTabs);
    radiationTabs->addTab(radiationPatternView_, tr("2D Pattern"));
    radiation3DView_ = new Radiation3DView(radiationTabs);
    radiationTabs->addTab(radiation3DView_, tr("3D Pattern"));
    resultsWorkspace_->addTab(radiationTabs, tr("Radiation"));
    radiationPatternView_->setSettingsChangedCallback([this](const auto& settings) {
        radiation3DView_->setDisplaySettings(settings);
    });
    radiation3DView_->setSettingsChangedCallback([this](const auto& settings) {
        radiationPatternView_->setDisplaySettings(settings);
    });
    connect(resultsFrequencyControl_, &QComboBox::currentIndexChanged, this, [this] {
        if (resultsFrequencyControl_->currentIndex() >= 0)
            applyResultFrequency(resultsFrequencyControl_->currentData().toDouble());
    });
    auto* rawOutputPage = new QWidget(resultsWorkspace_);
    auto* rawOutputLayout = new QVBoxLayout(rawOutputPage);
    rawOutputLayout->setContentsMargins(12, 12, 12, 12);
    auto* rawOutputDescription = new QLabel(tr(
        "Complete, immutable model.out from the solver. The frequency selector filters parsed views; "
        "it does not rewrite this archived file."), rawOutputPage);
    rawOutputDescription->setWordWrap(true);
    auto* rawOutputControls = new QHBoxLayout;
    auto* jumpFrequencyButton = new QPushButton(tr("Jump to Selected Frequency"), rawOutputPage);
    rawOutputFindControl_ = new QLineEdit(rawOutputPage);
    rawOutputFindControl_->setPlaceholderText(tr("Find text in complete output"));
    auto* findOutputButton = new QPushButton(tr("Find Next"), rawOutputPage);
    rawOutputControls->addWidget(jumpFrequencyButton);
    rawOutputControls->addSpacing(12);
    rawOutputControls->addWidget(rawOutputFindControl_, 1);
    rawOutputControls->addWidget(findOutputButton);
    analysisOutput_ = new QPlainTextEdit(rawOutputPage);
    analysisOutput_->setReadOnly(true);
    analysisOutput_->setPlaceholderText(tr("Complete solver output will appear here."));
    rawOutputLayout->addWidget(rawOutputDescription);
    rawOutputLayout->addLayout(rawOutputControls);
    rawOutputLayout->addWidget(analysisOutput_, 1);
    connect(jumpFrequencyButton, &QPushButton::clicked,
        this, [this] { jumpRawOutputToSelectedFrequency(); });
    connect(findOutputButton, &QPushButton::clicked, this, [this] { findInRawOutput(); });
    connect(rawOutputFindControl_, &QLineEdit::returnPressed, this, [this] { findInRawOutput(); });
    analysisOutputTabIndex_ = resultsWorkspace_->addTab(rawOutputPage, tr("Raw Output"));
    analysisRunsTabIndex_ = resultsWorkspace_->addTab(runsPage, tr("Runs"));
    resultsLayout->addWidget(resultsContextFrame);
    resultsLayout->addWidget(resultsStatusLabel_);
    resultsLayout->addLayout(frequencyBar);
    resultsLayout->addWidget(resultsWorkspace_, 1);
    visualizeModuleIndex_ = moduleStack_->addWidget(resultsPage);

    optimizationWorkspace_ = new OptimizationWorkspace(moduleStack_);
    optimizationWorkspace_->setRunsChangedCallback([this] { loadRunHistory(); });
    optimizationWorkspace_->setRunningChangedCallback([this] { synchronizeRunnerState(); });
    optimizationWorkspace_->setReturnToCurrentWorkCallback([this] { returnToCurrentWork(); });
    optimizeModuleIndex_ = moduleStack_->addWidget(optimizationWorkspace_);
    showModule(homeModuleIndex_);
}

void MainWindow::showModule(int index)
{
    if (moduleStack_ == nullptr || index < 0 || index >= moduleStack_->count()) {
        return;
    }
    moduleStack_->setCurrentIndex(index);
    if (index != visualizeModuleIndex_ && !historicalSessionViewActive_)
        lastNonResultsModuleIndex_ = index;
    if (index == homeModuleIndex_) {
        homeModuleAction_->setChecked(true);
        dashboardStack_->setCurrentIndex(hasNecModel_ ? 1 : 0);
    } else if (index == modelModuleIndex_) {
        modelModuleAction_->setChecked(true);
    } else if (index == sourceModuleIndex_) {
        sourceModuleAction_->setChecked(true);
    } else if (index == analysisModuleIndex_) {
        analysisModuleAction_->setChecked(true);
    } else if (index == visualizeModuleIndex_) {
        visualizeModuleAction_->setChecked(true);
    } else if (index == optimizeModuleIndex_) {
        optimizeModuleAction_->setChecked(true);
    }
    updateUndoActions();
}

void MainWindow::showModelTab(int index)
{
    if (index == sourceTabIndex_) {
        showModule(sourceModuleIndex_);
        sourceWorkspace_->setCurrentIndex(0);
    } else if (index == setupTabIndex_) {
        showModule(analysisModuleIndex_);
        analysisWorkspace_->setCurrentIndex(0);
        modelSetupWorkspace_->setCurrentIndex(0);
    } else {
        showModule(modelModuleIndex_);
        workspace_->setCurrentIndex(index);
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
            : tr("GS scale ×%1 to meters").arg(info.scaleToMeters, 0, 'g', 12));
    deckUnitControl_->setToolTip(tr(
        "These are the coordinate and radius units written in NEC geometry cards. "
        "This setting is independent of Geometry display units."));
    wireCardEditor_->setDeckScale(
        info.uniform ? info.scaleToMeters : 1.0, unitLabel);
}

auto MainWindow::deckScaleForSourceLine(std::size_t sourceLine) const -> double
{
    const auto resolution = nec::NecSymbolResolver{}.resolve(editor_->toPlainText().toStdString());
    if (!resolution.ok()) return deckScaleToMeters_;
    return nec::geometryScaleForLine(
        nec::NecParser{}.parse(resolution.resolvedSource), sourceLine);
}

void MainWindow::changeDeckLengthUnit(model::LengthUnit unit)
{
    const auto targetScale = model::metersPerUnit(unit);
    if (std::abs(targetScale - deckScaleToMeters_) <= 1.0e-12) return;

    const auto source = editor_->toPlainText();
    const auto rawDocument = nec::NecParser{}.parse(source.toStdString());
    const auto resolution = nec::NecSymbolResolver{}.resolve(source.toStdString());
    if (!resolution.ok()) {
        QMessageBox::warning(this, tr("Change NEC Deck Units"),
            tr("Resolve the model's symbol expressions before changing deck geometry units."));
        checkModel();
        return;
    }
    const auto resolvedDocument = nec::NecParser{}.parse(resolution.resolvedSource);
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
    if (projectTree_ != nullptr && projectTree_->currentItem() != nullptr) {
        showProjectItemProperties(projectTree_->currentItem());
    }
}

void MainWindow::showGeometrySettings()
{
    GeometrySettingsDialog dialog(geometrySettings_, this);
    if (dialog.exec() == QDialog::Accepted) {
        applyGeometrySettings(dialog.settings());
    }
}

auto MainWindow::formatLength(double meters) const -> QString
{
    const auto symbol = model::lengthUnitSymbol(geometrySettings_.lengthUnit);
    return QStringLiteral("%1 %2")
        .arg(QString::number(model::fromMeters(meters, geometrySettings_.lengthUnit), 'g', 10))
        .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

void MainWindow::showAutoSegmentation()
{
    if (!modelChecked_ || modelErrorCount_ != 0 || currentModel_.empty()) {
        QMessageBox::information(this, tr("Automatic Segmentation"),
            tr("Run Check Model and resolve geometry errors before calculating segment counts."));
        return;
    }
    if (!currentSetup_.frequency) {
        QMessageBox::information(this, tr("Automatic Segmentation"),
            tr("Add an analysis frequency before calculating wavelength-based segment counts."));
        return;
    }
    const auto document = nec::NecParser{}.parse(editor_->toPlainText().toStdString());
    const auto unsupportedAttachment = std::ranges::find_if(document.cards(), [](const auto& card) {
        return card.kind == nec::NecCardKind::Load
            || card.kind == nec::NecCardKind::TransmissionLine
            || card.kind == nec::NecCardKind::Network;
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
            tr("This deck contains unsupported EX types or LD, TL, or NT attachment cards. Automatic segmentation is blocked until NEC Workbench can safely remap all segment references."));
        return;
    }
    const auto maximumFrequencyMHz = std::max(currentSetup_.frequency->startMHz,
        model::frequencyEndMHz(*currentSetup_.frequency));
    AutoSegmentationDialog dialog(currentModel_, currentSetup_.excitations,
        maximumFrequencyMHz, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto proposal = dialog.proposal();
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    auto changed = false;
    const nec::NecWriter writer;
    for (const auto& wireProposal : proposal.wires) {
        const auto* original = currentModel_.wireByTag(wireProposal.wireTag);
        if (original == nullptr || original->segments == wireProposal.newSegments) {
            continue;
        }
        auto updated = *original;
        updated.segments = wireProposal.newSegments;
        const auto lineIndex = static_cast<int>(updated.sourceLine) - 1;
        if (lineIndex >= 0 && lineIndex < lines.size()) {
            lines[lineIndex] = QString::fromStdString(
                writer.writeWireCard(updated, deckScaleForSourceLine(updated.sourceLine)));
            changed = true;
        }
    }
    for (std::size_t index = 0; index < currentSetup_.excitations.size(); ++index) {
        const auto& original = currentSetup_.excitations[index];
        const auto& updated = proposal.remappedExcitations[index];
        if (original.segment == updated.segment) {
            continue;
        }
        const auto lineIndex = static_cast<int>(updated.sourceLine) - 1;
        if (lineIndex >= 0 && lineIndex < lines.size()) {
            lines[lineIndex] = QString::fromStdString(writer.writeExcitationCard(updated));
            changed = true;
        }
    }
    if (changed) {
        pushGeometrySourceEdit(tr("Automatically segment wires"), lines.join(QLatin1Char('\n')));
    } else {
        statusBar()->showMessage(tr("Wire segmentation already matches this recommendation."), 5000);
    }
}

void MainWindow::createDocks()
{
    projectDock_ = new QDockWidget(tr("Project"), this);
    projectDock_->setObjectName(QStringLiteral("projectDock"));
    projectTree_ = new QTreeWidget(projectDock_);
    projectTree_->setHeaderHidden(true);
    projectTree_->setAlternatingRowColors(true);
    projectDock_->setWidget(projectTree_);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock_);

    propertiesDock_ = new QDockWidget(tr("Properties"), this);
    propertiesDock_->setObjectName(QStringLiteral("propertiesDock"));
    properties_ = new QTableWidget(propertiesDock_);
    properties_->setObjectName(QStringLiteral("propertiesTable"));
    properties_->setColumnCount(2);
    properties_->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    properties_->horizontalHeader()->setStretchLastSection(true);
    properties_->verticalHeader()->hide();
    properties_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    properties_->setSelectionMode(QAbstractItemView::NoSelection);
    propertiesDock_->setWidget(properties_);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    diagnosticsDock_ = new QDockWidget(tr("Model Adequacy"), this);
    diagnosticsDock_->setObjectName(QStringLiteral("modelDiagnosticsDock"));
    auto* validationPanel = new QWidget(diagnosticsDock_);
    auto* validationLayout = new QVBoxLayout(validationPanel);
    validationLayout->setContentsMargins(8, 6, 8, 6);
    validationLayout->setSpacing(4);
    validationSummary_ = new QLabel(tr("Run Check Model to evaluate the current source."), validationPanel);
    auto summaryFont = validationSummary_->font();
    summaryFont.setBold(true);
    validationSummary_->setFont(summaryFont);
    validationScope_ = new QLabel(tr(
        "Static NEC checks cover source validity, segmentation, wire thickness, source centering, "
        "and junction consistency. Average Gain Test runs from Home or the Model menu and appears "
        "under Results > Validation. Convergence requires a separate multi-run study."), validationPanel);
    validationScope_->setWordWrap(true);
    diagnostics_ = new QTreeWidget(validationPanel);
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
    diagnosticsDock_->setWidget(validationPanel);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticsDock_);

    solverOutputDock_ = new QDockWidget(tr("Solver Output"), this);
    solverOutputDock_->setObjectName(QStringLiteral("solverOutputDock"));
    solverOutput_ = new QPlainTextEdit(solverOutputDock_);
    solverOutput_->setReadOnly(true);
    solverOutput_->setPlaceholderText(tr("External solver command, progress, and diagnostics will appear here."));
    solverOutputDock_->setWidget(solverOutput_);
    addDockWidget(Qt::BottomDockWidgetArea, solverOutputDock_);
    tabifyDockWidget(diagnosticsDock_, solverOutputDock_);

    diagnosticsDock_->raise();

    connect(diagnostics_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) { goToDiagnostic(item); });
    connect(projectTree_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) { showProjectItemProperties(item); });
    connect(projectTree_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item) { activateProjectItem(item); });

    updateProjectTree({}, nec::NecDocument{});
}

void MainWindow::createMenusAndToolbar()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(newAction_);
    fileMenu->addAction(openAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction_);
    fileMenu->addAction(saveAsAction_);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(projectDock_->toggleViewAction());
    viewMenu->addAction(propertiesDock_->toggleViewAction());
    viewMenu->addAction(diagnosticsDock_->toggleViewAction());
    viewMenu->addAction(solverOutputDock_->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(fitGeometryAction_);

    auto* modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->addAction(checkAction_);
    modelMenu->addAction(averageGainAction_);
    modelMenu->addAction(convergenceAction_);
    modelMenu->addSeparator();
    modelMenu->addAction(autoSegmentationAction_);
    modelMenu->addAction(geometrySettingsAction_);

    auto* analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(runAction_);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->addAction(newAction_);
    toolbar->addAction(openAction_);
    toolbar->addAction(saveAction_);
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    toolbar->addSeparator();
    toolbar->addAction(checkAction_);
    toolbar->addAction(autoSegmentationAction_);
    toolbar->addAction(runAction_);
}

void MainWindow::newModel()
{
    if (!maybeSaveChanges()) {
        return;
    }
    clearDisplayedResults();
    activeModelResultsDirectory_.clear();
    activeModelResultsContext_.clear();
    undoStack_->clear();
    editor_->setPlainText(tr("CM New NEC Workbench model\nCE\nGE 0\nEN\n"));
    hasNecModel_ = true;
    modelModuleAction_->setEnabled(true);
    sourceModuleAction_->setEnabled(true);
    analysisModuleAction_->setEnabled(true);
    visualizeModuleAction_->setEnabled(true);
    dashboardStack_->setCurrentIndex(1);
    setCurrentFile({});
    editor_->document()->setModified(false);
    showModelTab(sourceTabIndex_);
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
    undoStack_->clear();
    editor_->setPlainText(QString::fromUtf8(file.readAll()));
    hasNecModel_ = true;
    modelModuleAction_->setEnabled(true);
    sourceModuleAction_->setEnabled(true);
    analysisModuleAction_->setEnabled(true);
    visualizeModuleAction_->setEnabled(true);
    dashboardStack_->setCurrentIndex(1);
    setCurrentFile(path);
    editor_->document()->setModified(false);
    showModelTab(sourceTabIndex_);
    checkModel();
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

void MainWindow::rememberRecentFile(const QString& path)
{
    if (path.isEmpty()) return;
    auto files = recentFiles();
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    files.removeAll(absolutePath);
    files.prepend(absolutePath);
    while (files.size() > 8) files.removeLast();
    QSettings{}.setValue(QStringLiteral("files/recentModels"), files);
    if (welcomePage_ != nullptr) welcomePage_->setRecentFiles(files);
}

void MainWindow::clearRecentFiles()
{
    QSettings{}.remove(QStringLiteral("files/recentModels"));
    if (welcomePage_ != nullptr) welcomePage_->setRecentFiles({});
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

void MainWindow::checkModel()
{
    const auto source = editor_->toPlainText().toStdString();
    const auto document = nec::NecParser{}.parse(source);
    const auto resolution = nec::NecSymbolResolver{}.resolve(source);
    auto deckUnitInfo = nec::inspectDeckGeometryUnits(document);
    nec::ModelCheckResult result;
    if (resolution.ok()) {
        const auto resolvedDocument = nec::NecParser{}.parse(resolution.resolvedSource);
        deckUnitInfo = nec::inspectDeckGeometryUnits(resolvedDocument);
        result = nec::NecModelChecker{}.check(resolvedDocument);
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
        auto* item = new QTreeWidgetItem(diagnostics_, {
            isError ? tr("Error") : tr("Warning"),
            QString::fromStdString(diagnostic.category),
            QString::number(diagnostic.lineNumber),
            QString::fromStdString(diagnostic.message)});
        item->setData(0, SourceLineRole, static_cast<qulonglong>(diagnostic.lineNumber));
        item->setIcon(0, style()->standardIcon(isError ? QStyle::SP_MessageBoxCritical : QStyle::SP_MessageBoxWarning));
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
    if (result.errorCount() != 0) {
        validationSummary_->setText(tr("Invalid model · %1 blocking error(s)")
            .arg(static_cast<qulonglong>(result.errorCount())));
    } else if (incomplete) {
        validationSummary_->setText(tr("Incomplete model · finish geometry and analysis setup"));
    } else if (adequacyWarnings != 0) {
        validationSummary_->setText(tr("Static adequacy: Caution · %1 finding(s) to review")
            .arg(static_cast<qulonglong>(adequacyWarnings)));
    } else {
        validationSummary_->setText(tr("Static adequacy checks passed"));
    }

    editor_->setDiagnostics(result.diagnostics);
    updateDeckUnitControls(deckUnitInfo);
    wireCardEditor_->setModel(currentModel_);
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
    setupEditor_->setData(currentModel_, currentSetup_);
    loadNetworkEditor_->setData(currentModel_, currentSetup_);
    analysisRequestEditor_->setData(currentSetup_);
    structuredCardEditor_->setDocument(document);
    dashboardPage_->setModel(currentModel_, currentSetup_, solverBackendId_, true,
        modelErrorCount_, modelWarningCount_);
    updateProjectTree(currentModel_, document);
    optimizationWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
        modelErrorCount_ == 0);
    convergenceWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
        modelErrorCount_ == 0);
    checkStatus_->setText(tr("Checked: %1 errors, %2 warnings, %3 wires, %4 cards")
        .arg(static_cast<qulonglong>(result.errorCount()))
        .arg(static_cast<qulonglong>(result.warningCount()))
        .arg(static_cast<qulonglong>(currentModel_.wireCount()))
        .arg(static_cast<qulonglong>(document.cards().size())));
    diagnosticsDock_->show();
    diagnosticsDock_->raise();
    resizeDocks({diagnosticsDock_}, {220}, Qt::Vertical);
    updateAnalysisReadiness();
}

void MainWindow::clearCheckResults()
{
    if (!updatingSourceFromGeometry_) {
        undoStack_->clear();
    }
    diagnostics_->clear();
    editor_->setDiagnostics(std::span<const nec::ModelDiagnostic>{});
    validationSummary_->setText(tr("Model changed · run Check Model again"));
    checkStatus_->setText(tr("Model changed — check required"));
    modelChecked_ = false;
    optimizationWorkspace_->setModelValid(false);
    convergenceWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_, false);
    dashboardPage_->setModel(currentModel_, currentSetup_, solverBackendId_, false,
        modelErrorCount_, modelWarningCount_);
    dashboardPage_->markAverageGainStale();
    averageGainResultsView_->markStale();
    dashboardPage_->markConvergenceStale();
    convergenceWorkspace_->markStale();
    if (resultsAvailable_ && !displayingHistoricalResults_) dashboardPage_->markResultsStale();
    if (resultsAvailable_ && !displayingHistoricalResults_)
        resultsStatusLabel_->setText(tr("STALE — the NEC model changed after these results were calculated."));
    updateAnalysisReadiness();
}

void MainWindow::goToDiagnostic(QTreeWidgetItem* item)
{
    const auto lineNumber = item->data(0, SourceLineRole).toULongLong();
    if (lineNumber == 0) {
        return;
    }
    showModelTab(sourceTabIndex_);
    editor_->goToLine(static_cast<std::size_t>(lineNumber));
}

void MainWindow::activateProjectItem(QTreeWidgetItem* item)
{
    const auto kind = item->data(0, ItemKindRole).toString();
    const auto sourceLine = item->data(0, SourceLineRole).toULongLong();
    if (kind == QStringLiteral("geometry")) {
        showModelTab(geometryTabIndex_);
        return;
    }
    if (kind == QStringLiteral("excitation")) {
        showModelTab(setupTabIndex_);
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
            showModule(sourceModuleIndex_);
            sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
            if (auto* tabs = qobject_cast<QTabWidget*>(wireCardEditor_->parentWidget()))
                tabs->setCurrentWidget(wireCardEditor_);
            wireCardEditor_->selectWire(item->data(0, WireTagRole).toInt());
        } else {
            showModelTab(sourceTabIndex_);
        }
        if (sourceLine != 0) editor_->goToLine(static_cast<std::size_t>(sourceLine));
        return;
    }
    if (kind != QStringLiteral("card") || sourceLine == 0) return;

    const auto mnemonic = item->data(0, CardMnemonicRole).toString();
    if (mnemonic == QStringLiteral("FR") || mnemonic == QStringLiteral("GN")
        || mnemonic == QStringLiteral("GE") || mnemonic == QStringLiteral("EX")) {
        showModelTab(setupTabIndex_);
        if (mnemonic == QStringLiteral("EX")) setupEditor_->selectExcitation(sourceLine);
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
        analysisWorkspace_->setCurrentWidget(analysisRequestEditor_);
        return;
    }
    if (mnemonic == QStringLiteral("GW")) {
        showModule(sourceModuleIndex_);
        sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
        if (auto* tabs = qobject_cast<QTabWidget*>(wireCardEditor_->parentWidget()))
            tabs->setCurrentWidget(wireCardEditor_);
        wireCardEditor_->selectWire(item->data(0, WireTagRole).toInt());
        return;
    }
    if (structuredCardEditor_->selectCard(sourceLine)) {
        showModule(sourceModuleIndex_);
        sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
        if (auto* tabs = qobject_cast<QTabWidget*>(structuredCardEditor_->parentWidget()))
            tabs->setCurrentWidget(structuredCardEditor_);
        return;
    }
    showModelTab(sourceTabIndex_);
    editor_->goToLine(sourceLine);
}

void MainWindow::selectWireInProject(int tag)
{
    synchronizeGeometrySelection(tag);
    wireCardEditor_->selectWire(tag);
    setupEditor_->selectExcitation(0);
    if (tag < 0) {
        projectTree_->clearSelection();
        properties_->setRowCount(0);
        return;
    }

    if (auto* item = findProjectItem(projectTree_, QStringLiteral("wire"),
            WireTagRole, tag)) {
        projectTree_->setCurrentItem(item);
        showProjectItemProperties(item);
        propertiesDock_->show();
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
        showProjectItemProperties(item);
        propertiesDock_->show();
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
    const auto found = std::ranges::find(currentSetup_.loads, sourceLine,
        &model::LoadDefinition::sourceLine);
    if (found == currentSetup_.loads.end()) return;
    properties_->setRowCount(0);
    populateLoadProperties(*found);
    properties_->resizeColumnToContents(0);
    propertiesDock_->show();
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
    const auto found = std::ranges::find(currentSetup_.transmissionLines, sourceLine,
        &model::TransmissionLineDefinition::sourceLine);
    if (found == currentSetup_.transmissionLines.end()) return;
    properties_->setRowCount(0);
    populateTransmissionLineProperties(*found);
    properties_->resizeColumnToContents(0);
    propertiesDock_->show();
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
        applyEndpointMove(tag, endpoint, original);
        return;
    }
    const auto endpointName = endpoint == model::WireEndpoint::Start ? tr("start") : tr("end");
    undoStack_->push(new MoveEndpointCommand(
        tr("Move wire %1 %2 endpoint").arg(tag).arg(endpointName), original, updated,
        [this, tag, endpoint](const model::Point3D& position) {
            applyEndpointMove(tag, endpoint, position);
        }));
}

void MainWindow::applyEndpointMove(int tag, model::WireEndpoint endpoint, const model::Point3D& position)
{
    auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr) {
        return;
    }
    (endpoint == model::WireEndpoint::Start ? wire->start : wire->end) = position;
    replaceWireSourceLine(*wire);
    refreshGeometryViews();
    selectWireInProject(tag);
    checkStatus_->setText(tr("Wire %1 geometry updated").arg(tag));
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
    undoStack_->push(new MoveWireCommand(tr("Move wire %1").arg(tag), originalStart, originalEnd,
        updatedStart, updatedEnd,
        [this, tag](const model::Point3D& start, const model::Point3D& end) {
            applyWireMove(tag, start, end);
        }));
}

void MainWindow::applyWireMove(int tag, const model::Point3D& start, const model::Point3D& end)
{
    auto* wire = currentModel_.wireByTag(tag);
    if (wire == nullptr) {
        return;
    }
    wire->start = start;
    wire->end = end;
    replaceWireSourceLine(*wire);
    refreshGeometryViews();
    selectWireInProject(tag);
    checkStatus_->setText(tr("Wire %1 moved").arg(tag));
}

void MainWindow::addWire(const model::Point3D& start, const model::Point3D& end)
{
    const auto originalSource = editor_->toPlainText();
    auto lines = originalSource.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto document = nec::NecParser{}.parse(originalSource.toStdString());
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
    if (wire == nullptr || position == wire->start || position == wire->end) {
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
    if (wire == nullptr) {
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
    if (wire == nullptr) {
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
    if (selectedWire == nullptr) {
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
    upsertSetupCard(frequency.sourceLine == 0 ? tr("Add frequency definition") : tr("Edit frequency definition"),
        frequency.sourceLine, QString::fromStdString(nec::NecWriter{}.writeFrequencyCard(frequency)), true);
}

void MainWindow::deleteFrequency(std::size_t sourceLine)
{
    deleteSetupCard(tr("Delete frequency definition"), sourceLine);
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
        const auto document = nec::NecParser{}.parse(editor_->toPlainText().toStdString());
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
    showModelTab(setupTabIndex_);
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
    showModule(analysisModuleIndex_);
    analysisWorkspace_->setCurrentIndex(0);
    modelSetupWorkspace_->setCurrentIndex(loadNetworkTabIndex_);
    selectLoad(sourceLine);
}

void MainWindow::showTransmissionLineInEditor(std::size_t sourceLine)
{
    showModule(analysisModuleIndex_);
    analysisWorkspace_->setCurrentIndex(0);
    modelSetupWorkspace_->setCurrentIndex(loadNetworkTabIndex_);
    selectTransmissionLine(sourceLine);
}

void MainWindow::upsertSetupCard(const QString& description, std::size_t sourceLine,
    const QString& cardText, bool frequencyCard)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (sourceLine != 0) {
        const auto lineIndex = static_cast<int>(sourceLine) - 1;
        if (lineIndex < 0 || lineIndex >= lines.size()) {
            return;
        }
        lines[lineIndex] = cardText;
    } else {
        const auto document = nec::NecParser{}.parse(editor_->toPlainText().toStdString());
        auto insertionIndex = lines.size();
        for (const auto& card : document.cards()) {
            const bool insertionBoundary = card.kind == nec::NecCardKind::End
                || card.kind == nec::NecCardKind::RadiationPattern
                || (!frequencyCard && card.kind == nec::NecCardKind::Frequency);
            if (insertionBoundary) {
                insertionIndex = static_cast<int>(card.lineNumber) - 1;
                break;
            }
        }
        lines.insert(insertionIndex, cardText);
    }
    pushGeometrySourceEdit(description, lines.join(QLatin1Char('\n')));
}

void MainWindow::deleteSetupCard(const QString& description, std::size_t sourceLine)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(sourceLine) - 1;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        return;
    }
    lines.removeAt(lineIndex);
    pushGeometrySourceEdit(description, lines.join(QLatin1Char('\n')));
}

void MainWindow::changeAnalysisRequests(bool executionEnabled,
    const model::ExecutionRequest& execution, bool patternEnabled,
    const model::RadiationPatternRequest& pattern)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const nec::NecWriter writer;
    std::vector<int> removals;
    if (currentSetup_.executionRequest) {
        const auto index = static_cast<int>(currentSetup_.executionRequest->sourceLine) - 1;
        if (executionEnabled) {
            lines[index] = QString::fromStdString(writer.writeExecutionCard(execution));
        } else {
            removals.push_back(index);
        }
    }
    if (currentSetup_.radiationPattern) {
        const auto index = static_cast<int>(currentSetup_.radiationPattern->sourceLine) - 1;
        if (patternEnabled) {
            lines[index] = QString::fromStdString(writer.writeRadiationPatternCard(pattern));
        } else {
            removals.push_back(index);
        }
    }
    std::ranges::sort(removals, std::greater{});
    for (const auto index : removals) {
        if (index >= 0 && index < lines.size()) {
            lines.removeAt(index);
        }
    }

    QStringList additions;
    if (executionEnabled && !currentSetup_.executionRequest) {
        additions.append(QString::fromStdString(writer.writeExecutionCard(execution)));
    }
    if (patternEnabled && !currentSetup_.radiationPattern) {
        additions.append(QString::fromStdString(writer.writeRadiationPatternCard(pattern)));
    }
    if (!additions.empty()) {
        const auto updatedDocument = nec::NecParser{}.parse(lines.join(QLatin1Char('\n')).toStdString());
        auto insertionIndex = lines.size();
        for (const auto& card : updatedDocument.cards()) {
            if (card.kind == nec::NecCardKind::End) {
                insertionIndex = static_cast<int>(card.lineNumber) - 1;
                break;
            }
        }
        for (const auto& addition : additions) {
            lines.insert(insertionIndex++, addition);
        }
    }
    pushGeometrySourceEdit(tr("Change requested analysis results"), lines.join(QLatin1Char('\n')));
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
        blockers.append(tr("No valid wire geometry."));
    }
    if (modelErrorCount_ != 0) {
        blockers.append(tr("Model check reports %1 error(s).").arg(static_cast<qulonglong>(modelErrorCount_)));
    }
    if (!currentSetup_.frequency) {
        blockers.append(tr("No supported FR frequency definition."));
    }
    if (currentSetup_.excitations.empty()) {
        blockers.append(tr("No supported EX voltage source."));
    }
    auto averageGainBlockers = blockers;
    if (!currentSetup_.executionRequest && !currentSetup_.radiationPattern) {
        blockers.append(tr("No XQ or RP result request."));
    }
    if (!analysis::isBackendRunnable(solverBackendId_.toStdString())) {
        blockers.append(tr("The selected backend does not have a process adapter yet."));
    }
    const QFileInfo executable(solverExecutablePath_);
    if (solverExecutablePath_.isEmpty()) {
        blockers.append(tr("No solver executable selected."));
    } else if (!executable.exists() || !executable.isFile() || !executable.isExecutable()) {
        blockers.append(tr("Solver executable path is not runnable."));
    }
    if (optimizationWorkspace_->isRunning()) {
        blockers.append(tr("An optimization sweep is running."));
        averageGainBlockers.append(tr("An optimization sweep is running."));
    }
    if (convergenceWorkspace_->isRunning()) {
        blockers.append(tr("A segmentation convergence study is running."));
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
    runAction_->setEnabled(blockers.empty() && solverProcess_ == nullptr);
    averageGainAction_->setEnabled(averageGainBlockers.empty() && solverProcess_ == nullptr);
    convergenceAction_->setEnabled(modelChecked_ && modelErrorCount_ == 0
        && solverProcess_ == nullptr && !optimizationWorkspace_->isRunning()
        && !convergenceWorkspace_->isRunning());
    optimizeModuleAction_->setEnabled(modelChecked_ && modelErrorCount_ == 0);
}

void MainWindow::synchronizeRunnerState()
{
    optimizationWorkspace_->setExternalRunActive(
        solverProcess_ != nullptr || convergenceWorkspace_->isRunning());
    convergenceWorkspace_->setExternalRunActive(
        solverProcess_ != nullptr || optimizationWorkspace_->isRunning());
    updateAnalysisReadiness();
}

void MainWindow::showConvergenceStudy()
{
    if (historicalSessionViewActive_) {
        leaveHistoricalSessionViews();
        historicalReviewActive_ = false;
        historicalSessionViewActive_ = false;
    }
    showModule(visualizeModuleIndex_);
    resultsWorkspace_->setCurrentIndex(averageGainResultsTabIndex_);
    validationWorkspace_->setCurrentIndex(convergenceValidationTabIndex_);
}

void MainWindow::startAnalysis()
{
    if (solverProcess_ != nullptr || !runAction_->isEnabled()) {
        return;
    }

    checkModel();
    if (modelErrorCount_ != 0 || !runAction_->isEnabled()) {
        statusBar()->showMessage(tr("Solve canceled: model validation found blocking errors."), 5000);
        return;
    }

    currentRunPurpose_ = SolverRunPurpose::Analysis;
    currentRunRecord_ = runStore_.create(solverBackendId_, currentFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : currentFile_);
    currentRunDirectory_ = currentRunRecord_->directory;
    if (!QFileInfo::exists(currentRunDirectory_)) {
        QMessageBox::critical(this, tr("Run Failed"), tr("Could not create the solver run folder."));
        currentRunRecord_.reset();
        return;
    }

    const auto inputPath = QDir(currentRunDirectory_).filePath(QStringLiteral("model.nec"));
    const auto sourcePath = QDir(currentRunDirectory_).filePath(QStringLiteral("model.source.nec"));
    currentRunOutputPath_ = QDir(currentRunDirectory_).filePath(QStringLiteral("model.out"));
    const auto source = editor_->toPlainText().toStdString();
    const auto resolution = nec::NecSymbolResolver{}.resolve(source);
    if (!resolution.ok()) {
        QMessageBox::critical(this, tr("Run Failed"),
            tr("The parameterized source could not be resolved."));
        currentRunRecord_.reset();
        return;
    }
    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Run Failed"), sourceFile.errorString());
        currentRunRecord_.reset();
        return;
    }
    sourceFile.write(QByteArray::fromStdString(source));
    sourceFile.close();

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Run Failed"), inputFile.errorString());
        currentRunRecord_.reset();
        return;
    }
    const auto generatedDocument = nec::NecParser{}.parse(resolution.generatedDeck);
    const auto generatedSetup = nec::NecSetupConverter{}.convert(generatedDocument);
    const auto solverInput = analysis::prepareSolverInput(
        resolution.generatedDeck, generatedSetup,
        analysisRequestEditor_->radiationSweepMode());
    inputFile.write(QByteArray::fromStdString(solverInput));
    inputFile.close();

    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(solverBackendId_.toStdString(),
            solverExecutablePath_.toStdString(), "model.nec", "model.out");
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Run Failed"), QString::fromLocal8Bit(error.what()));
        return;
    }

    startSolverProcess(command, tr("NEC analysis"));
}

void MainWindow::startAverageGainTest()
{
    if (solverProcess_ != nullptr || !averageGainAction_->isEnabled()) return;
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
    frequency->setDecimals(6);
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
    const auto source = editor_->toPlainText().toStdString();
    const auto resolution = nec::NecSymbolResolver{}.resolve(source);
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
    inputFile.write(QByteArray::fromStdString(analysis::prepareAverageGainTestInput(
        resolution.generatedDeck, currentAverageGainFrequencyMHz_, currentAverageGainEnvironment_)));
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
    currentRunRow_ = 0;
    analysisRuns_->selectRow(0);

    solverOutput_->clear();
    analysisOutput_->clear();
    const auto program = QString::fromStdString(command.executable);
    QStringList arguments;
    for (const auto& argument : command.arguments) {
        arguments.append(QString::fromStdString(argument));
    }
    appendSolverOutput(tr("%1\nRun folder: %2\nCommand: %3 %4\n\n")
        .arg(runContext(*currentRunRecord_), currentRunDirectory_, program,
            arguments.join(QLatin1Char(' '))));

    currentRunCanceled_ = false;
    currentRunTimedOut_ = false;
    solverProcess_ = new QProcess(this);
    synchronizeRunnerState();
    solverProcess_->setWorkingDirectory(currentRunDirectory_);
    solverProcess_->setProgram(program);
    solverProcess_->setArguments(arguments);
    connect(solverProcess_, &QProcess::started, this, [this] {
        setCurrentRunStatus(tr("Running"));
        appendSolverOutput(tr("Solver started.\n"));
    });
    connect(solverProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        appendSolverOutput(QString::fromLocal8Bit(solverProcess_->readAllStandardOutput()));
    });
    connect(solverProcess_, &QProcess::readyReadStandardError, this, [this] {
        appendSolverOutput(QString::fromLocal8Bit(solverProcess_->readAllStandardError()));
    });
    connect(solverProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            failAnalysis(solverProcess_->errorString());
        }
    });
    connect(solverProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) { finishAnalysis(exitCode, exitStatus); });

    solverTimeout_ = new QTimer(this);
    solverTimeout_->setSingleShot(true);
    connect(solverTimeout_, &QTimer::timeout, this, [this] {
        if (solverProcess_ == nullptr) {
            return;
        }
        currentRunTimedOut_ = true;
        appendSolverOutput(tr("\nRun exceeded the %1-second timeout; stopping solver.\n")
            .arg(solverTimeoutSeconds_));
        solverProcess_->terminate();
        QTimer::singleShot(2000, this, [this] {
            if (solverProcess_ != nullptr) {
                solverProcess_->kill();
            }
        });
    });
    solverTimeout_->start(solverTimeoutSeconds_ * 1000);
    solverElapsed_.start();
    cancelRunButton_->setEnabled(true);
    openRunFolderButton_->setEnabled(true);
    runAction_->setEnabled(false);
    showModule(visualizeModuleIndex_);
    if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
        averageGainResultsView_->setRunning(currentAverageGainFrequencyMHz_,
            currentAverageGainEnvironment_, runContext(*currentRunRecord_));
        dashboardPage_->setAverageGainRunning(currentAverageGainFrequencyMHz_);
        resultsWorkspace_->setCurrentIndex(averageGainResultsTabIndex_);
        validationWorkspace_->setCurrentIndex(averageGainValidationTabIndex_);
    } else {
        resultsWorkspace_->setCurrentIndex(analysisRunsTabIndex_);
    }
    updateRunSelectionActions();
    solverOutputDock_->show();
    solverOutputDock_->raise();
    statusBar()->showMessage(tr("Running %1…").arg(activity));
    solverProcess_->start();
}

void MainWindow::cancelAnalysis()
{
    if (solverProcess_ == nullptr) {
        return;
    }
    currentRunCanceled_ = true;
    appendSolverOutput(tr("\nCancellation requested; stopping solver.\n"));
    solverProcess_->terminate();
    QTimer::singleShot(2000, this, [this] {
        if (solverProcess_ != nullptr) {
            solverProcess_->kill();
        }
    });
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

void MainWindow::finishAnalysis(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (solverProcess_ == nullptr) {
        return;
    }
    if (solverTimeout_ != nullptr) {
        solverTimeout_->stop();
        solverTimeout_->deleteLater();
        solverTimeout_ = nullptr;
    }
    appendSolverOutput(solverProcess_->readAllStandardOutput());
    appendSolverOutput(solverProcess_->readAllStandardError());

    auto averageGainParsed = false;
    QFile outputFile(currentRunOutputPath_);
    if (outputFile.open(QIODevice::ReadOnly)) {
        const auto outputBytes = outputFile.readAll();
        analysisOutput_->setPlainText(QString::fromLocal8Bit(outputBytes));
        const auto result = analysis::NecOutputParser{}.parse(std::string_view(
            outputBytes.constData(), static_cast<std::size_t>(outputBytes.size())));
        if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
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
                    .arg(assessment.normalizedGain, 0, 'f', 4)
                    .arg(averageGainClassificationName(assessment.classification));
                averageGainResultsView_->setResult(assessment, currentAverageGainFrequencyMHz_,
                    currentAverageGainEnvironment_, result.averagingSolidAnglePi, context);
                dashboardPage_->setAverageGainResult(assessment, currentAverageGainFrequencyMHz_);
                writeAverageGainMetadata(currentRunDirectory_, currentAverageGainFrequencyMHz_,
                    currentAverageGainEnvironment_, assessment, result.averagingSolidAnglePi);
                resultsAvailable_ = true;
                displayedRunDirectory_ = currentRunDirectory_;
                resultsStatusLabel_->setText(tr("Average Gain Test — %1").arg(context));
                showActiveResultsContext(context);
                showModule(visualizeModuleIndex_);
                resultsWorkspace_->setCurrentIndex(averageGainResultsTabIndex_);
                validationWorkspace_->setCurrentIndex(averageGainValidationTabIndex_);
                const auto expectedSolidAngle = currentAverageGainEnvironment_
                        == analysis::AverageGainEnvironment::PerfectGround
                    ? 2.0 : 4.0;
                if (result.averagingSolidAnglePi
                    && std::abs(*result.averagingSolidAnglePi - expectedSolidAngle) > 0.01) {
                    appendSolverOutput(tr("\nWarning: solver reported a %1π averaging solid angle; expected %2π.\n")
                        .arg(*result.averagingSolidAnglePi, 0, 'g', 8)
                        .arg(expectedSolidAngle, 0, 'g', 8));
                }
            } else {
                averageGainResultsView_->setFailure(
                    tr("The solver output contains no AVERAGE POWER GAIN value."), context);
                dashboardPage_->setAverageGainFailure(tr("average power gain was not reported"));
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
            resultsSummaryView_->setResults(result, context);
            analysisResultsView_->setResults(result, context);
            visualizePlotsView_->setResults(result, context);
            currentResultsView_->setResults(result, context);
            radiationPatternView_->setResults(result, context);
            radiation3DView_->setModel(currentModel_);
            radiation3DView_->setResults(result, context);
            setDisplayedResults(result);
            displayedRunDirectory_ = currentRunDirectory_;
            activeModelResultsDirectory_ = currentRunDirectory_;
            activeModelResultsContext_ = context;
            dashboardPage_->setResults(result, context, false);
            dashboardPage_->setModel(currentModel_, currentSetup_, solverBackendId_, true,
                modelErrorCount_, modelWarningCount_);
            resultsAvailable_ = true;
            resultsStatusLabel_->setText(tr("Current results — %1").arg(context));
            showActiveResultsContext(context);
            if (!result.feedpoints.empty() || !result.currents.empty() || !result.radiation.empty()) {
                showModule(visualizeModuleIndex_);
                resultsWorkspace_->setCurrentIndex(resultsSummaryTabIndex_);
            }
        }
    } else if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
        const auto message = tr("The solver did not produce a readable model.out file.");
        averageGainResultsView_->setFailure(message, runContext(*currentRunRecord_));
        dashboardPage_->setAverageGainFailure(message);
    }

    QString status;
    if (currentRunTimedOut_) {
        status = tr("Timed out");
    } else if (currentRunCanceled_) {
        status = tr("Canceled");
    } else if (exitStatus == QProcess::NormalExit && exitCode == 0
        && (currentRunPurpose_ != SolverRunPurpose::AverageGainTest || averageGainParsed)) {
        status = tr("Completed");
    } else if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest
        && exitStatus == QProcess::NormalExit && exitCode == 0) {
        status = tr("Failed (AGT result missing)");
    } else {
        status = tr("Failed (exit %1)").arg(exitCode);
    }
    setCurrentRunStatus(status);
    if (currentRunRow_ >= 0) {
        const auto duration = solverElapsed_.elapsed() / 1000.0;
        analysisRuns_->setItem(currentRunRow_, RunDurationColumn,
            new QTableWidgetItem(tr("%1 s").arg(duration, 0, 'f', 2)));
        if (currentRunRecord_) {
            currentRunRecord_->durationSeconds = duration;
            runStore_.save(*currentRunRecord_);
        }
    }
    appendSolverOutput(tr("\n\nRun status: %1\nElapsed: %2 seconds\n")
        .arg(status).arg(solverElapsed_.elapsed() / 1000.0, 0, 'f', 2));
    const auto activity = currentRunPurpose_ == SolverRunPurpose::AverageGainTest
        ? tr("Average Gain Test") : tr("Analysis");
    statusBar()->showMessage(tr("%1 %2. Artifacts: %3")
        .arg(activity, status.toLower(), currentRunDirectory_), 10000);
    cancelRunButton_->setEnabled(false);
    solverProcess_->deleteLater();
    solverProcess_ = nullptr;
    synchronizeRunnerState();
    updateRunSelectionActions();
    updateAnalysisReadiness();
}

void MainWindow::failAnalysis(const QString& message)
{
    if (solverProcess_ == nullptr) {
        return;
    }
    appendSolverOutput(tr("\nFailed to start solver: %1\n").arg(message));
    if (currentRunPurpose_ == SolverRunPurpose::AverageGainTest) {
        const auto context = currentRunRecord_ ? runContext(*currentRunRecord_) : tr("Current model");
        averageGainResultsView_->setFailure(message, context);
        dashboardPage_->setAverageGainFailure(message);
    }
    setCurrentRunStatus(tr("Failed to start"));
    if (currentRunRow_ >= 0) {
        const auto duration = solverElapsed_.elapsed() / 1000.0;
        analysisRuns_->setItem(currentRunRow_, RunDurationColumn,
            new QTableWidgetItem(tr("%1 s").arg(duration, 0, 'f', 2)));
        if (currentRunRecord_) {
            currentRunRecord_->durationSeconds = duration;
            runStore_.save(*currentRunRecord_);
        }
    }
    if (solverTimeout_ != nullptr) {
        solverTimeout_->stop();
        solverTimeout_->deleteLater();
        solverTimeout_ = nullptr;
    }
    statusBar()->showMessage(tr("Solver failed to start."), 10000);
    cancelRunButton_->setEnabled(false);
    solverProcess_->deleteLater();
    solverProcess_ = nullptr;
    synchronizeRunnerState();
    updateRunSelectionActions();
    updateAnalysisReadiness();
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
    analysisRuns_->clearSelection();
    updateRunSelectionActions();
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
        : record.runType == QStringLiteral("average-gain-test") ? tr("AGT") : tr("Analysis");
    const QStringList values{
        record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        record.sourceFile.isEmpty() ? tr("Archived model.nec") : QFileInfo(record.sourceFile).fileName(),
        type,
        runResultsText(displayRecord),
        formatByteSize(outputBytes),
        record.backend,
        record.status,
        record.durationSeconds > 0.0 ? tr("%1 s").arg(record.durationSeconds, 0, 'f', 2) : tr("—"),
        record.directory,
    };
    for (auto column = 0; column < values.size(); ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        item->setData(RunDirectoryRole, record.directory);
        item->setData(RunContextRole, runContext(record));
        item->setData(RunTypeRole, record.runType);
        item->setData(RunIdRole, record.id);
        analysisRuns_->setItem(row, column, item);
    }
    if (prepend) analysisRuns_->resizeColumnsToContents();
}

void MainWindow::loadSelectedRun()
{
    if (solverProcess_ != nullptr || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning()
        || analysisRuns_->currentRow() < 0) {
        return;
    }
    const auto row = analysisRuns_->currentRow();
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item != nullptr) {
        captureHistoricalReturnContext();
        if (item->data(RunTypeRole).toString() == QStringLiteral("optimization-session")) {
            historicalSessionViewActive_ = true;
            if (optimizationWorkspace_->loadSession(item->data(RunIdRole).toString()))
                showModule(optimizeModuleIndex_);
            else historicalSessionViewActive_ = false;
            return;
        }
        if (item->data(RunTypeRole).toString() == QStringLiteral("convergence-session")) {
            historicalSessionViewActive_ = true;
            if (convergenceWorkspace_->loadSession(item->data(RunIdRole).toString())) {
                showModule(visualizeModuleIndex_);
                resultsWorkspace_->setCurrentIndex(averageGainResultsTabIndex_);
                validationWorkspace_->setCurrentIndex(convergenceValidationTabIndex_);
            } else historicalSessionViewActive_ = false;
            return;
        }
        const auto directory = item->data(RunDirectoryRole).toString();
        const auto modelName = analysisRuns_->item(row, RunModelColumn)->text();
        const auto started = analysisRuns_->item(row, RunStartedColumn)->text();
        const auto backend = analysisRuns_->item(row, RunBackendColumn)->text();
        const auto context = item->data(RunContextRole).toString();
        if (item->data(RunTypeRole).toString() == QStringLiteral("average-gain-test")) {
            displayAverageGainTestArtifacts(directory, context);
            showHistoricalResultsContext(modelName, started, backend);
            return;
        }
        displayRunArtifacts(directory, context);
        showHistoricalResultsContext(modelName, started, backend);
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
    if (solverProcess_ != nullptr || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning()) return;
    const auto row = analysisRuns_->currentRow();
    if (row < 0) return;
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item == nullptr) return;
    const auto directory = item->data(RunDirectoryRole).toString();
    const auto modelName = analysisRuns_->item(row, RunModelColumn)->text();
    const auto context = item->data(RunContextRole).toString();
    if (!loadRunModel(directory, modelName, context)) return;
    historicalReviewActive_ = false;
    historicalSessionViewActive_ = false;
    activeModelResultsDirectory_ = directory;
    activeModelResultsContext_ = context;
    displayRunArtifacts(directory, context, false);
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
    undoStack_->clear();
    editor_->setPlainText(QString::fromUtf8(inputFile.readAll()));
    hasNecModel_ = true;
    modelModuleAction_->setEnabled(true);
    sourceModuleAction_->setEnabled(true);
    analysisModuleAction_->setEnabled(true);
    visualizeModuleAction_->setEnabled(true);
    dashboardStack_->setCurrentIndex(1);
    setCurrentFile({});
    const auto displayName = modelName.isEmpty() ? tr("Archived model.nec") : modelName;
    setWindowTitle(tr("%1 (snapshot)[*] — NEC Workbench").arg(displayName));
    modelFileStatus_->setText(tr("Open snapshot as new model: %1").arg(displayName));
    modelFileStatus_->setToolTip(tr("%1\nArchived input: %2\nUse Save As to name this editable copy.")
        .arg(runContext, inputFile.fileName()));
    editor_->document()->setModified(false);
    checkModel();
    dashboardPage_->setDocumentState(displayName, false);
    updateActiveModelResultsLabel();
    return true;
}

void MainWindow::updateRunSelectionActions()
{
    const auto row = analysisRuns_->currentRow();
    const auto hasSelection = row >= 0 && analysisRuns_->item(row, RunStartedColumn) != nullptr;
    auto activeSelection = false;
    if (hasSelection && solverProcess_ != nullptr) {
        activeSelection = analysisRuns_->item(row, RunStartedColumn)->data(RunDirectoryRole).toString()
            == currentRunDirectory_;
    }
    const auto solverBusy = solverProcess_ != nullptr || optimizationWorkspace_->isRunning()
        || convergenceWorkspace_->isRunning();
    openRunResultsButton_->setEnabled(hasSelection && !activeSelection && !solverBusy);
    const auto selectedType = hasSelection
        ? analysisRuns_->item(row, RunStartedColumn)->data(RunTypeRole).toString() : QString{};
    openRunResultsButton_->setText(selectedType == QStringLiteral("optimization-session")
        ? tr("View Optimization")
        : selectedType == QStringLiteral("convergence-session") ? tr("View Convergence")
        : selectedType == QStringLiteral("average-gain-test") ? tr("View Validation") : tr("View Run Results"));
    const auto hasInputSnapshot = hasSelection
        && QFileInfo::exists(QDir(analysisRuns_->item(row, RunStartedColumn)
            ->data(RunDirectoryRole).toString()).filePath(QStringLiteral("model.nec")));
    const auto isSession = selectedType == QStringLiteral("optimization-session")
        || selectedType == QStringLiteral("convergence-session");
    inspectRunInputButton_->setEnabled(hasInputSnapshot && !isSession);
    openRunSnapshotButton_->setEnabled(hasInputSnapshot && !isSession && !activeSelection && !solverBusy);
    openRunFolderButton_->setEnabled(hasSelection);
    deleteRunButton_->setEnabled(hasSelection && !activeSelection && !solverBusy);
}

void MainWindow::deleteSelectedRun()
{
    if (optimizationWorkspace_->isRunning() || convergenceWorkspace_->isRunning()) return;
    const auto row = analysisRuns_->currentRow();
    if (row < 0) return;
    const auto* item = analysisRuns_->item(row, RunStartedColumn);
    if (item == nullptr) return;
    const auto directory = item->data(RunDirectoryRole).toString();
    if (solverProcess_ != nullptr && directory == currentRunDirectory_) {
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
    analysisRuns_->removeRow(row);
    if (activeModelResultsDirectory_ == directory) {
        activeModelResultsDirectory_.clear();
        activeModelResultsContext_.clear();
        if (displayingHistoricalResults_) returnToActiveResultsButton_->setVisible(false);
    }
    if (displayedRunDirectory_ == directory) clearDisplayedResults();
    updateRunSelectionActions();
}

void MainWindow::displayRunArtifacts(const QString& directory, const QString& context, bool historical)
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
        resultsStatusLabel_->setText(message);
        analysisOutput_->setPlainText(tr("Solver output unavailable: %1").arg(outputFile.fileName()));
        showModule(visualizeModuleIndex_);
        resultsWorkspace_->setCurrentIndex(analysisRunsTabIndex_);
        statusBar()->showMessage(message, 7000);
        return;
    }
    const auto outputBytes = outputFile.readAll();
    analysisOutput_->setPlainText(QString::fromLocal8Bit(outputBytes));
    const auto result = analysis::NecOutputParser{}.parse(std::string_view(
        outputBytes.constData(), static_cast<std::size_t>(outputBytes.size())));
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
        resultsStatusLabel_->setText(message);
        showModule(visualizeModuleIndex_);
        resultsWorkspace_->setCurrentIndex(analysisOutputTabIndex_);
        statusBar()->showMessage(message, 7000);
        return;
    }
    resultsSummaryView_->setResults(result, context);
    analysisResultsView_->setResults(result, context);
    visualizePlotsView_->setResults(result, context);
    currentResultsView_->setResults(result, context);
    radiationPatternView_->setResults(result, context);
    radiation3DView_->setModel({});
    auto archivedModelAvailable = false;
    QFile inputFile(QDir(directory).filePath(QStringLiteral("model.nec")));
    if (inputFile.open(QIODevice::ReadOnly)) {
        const auto runDocument = nec::NecParser{}.parse(
            QString::fromUtf8(inputFile.readAll()).toStdString());
        const auto conversion = nec::NecModelConverter{}.convert(runDocument);
        if (!conversion.model.empty()) {
            radiation3DView_->setModel(conversion.model);
            archivedModelAvailable = true;
        }
    }
    radiation3DView_->setResults(result, context);
    setDisplayedResults(result);
    displayedRunDirectory_ = directory;
    resultsAvailable_ = true;
    resultsStatusLabel_->setText(archivedModelAvailable
        ? (historical ? tr("Archived input loaded for result geometry — %1").arg(context)
                      : tr("Active model results — %1").arg(context))
        : tr("Results loaded — archived model.nec is missing or invalid · %1").arg(context));
    if (!historical) showActiveResultsContext(context);
    showModule(visualizeModuleIndex_);
    resultsWorkspace_->setCurrentIndex(resultsSummaryTabIndex_);
    statusBar()->showMessage(historical
        ? tr("Displaying historical run: %1").arg(directory)
        : tr("Displaying active model results: %1").arg(directory), 5000);
}

void MainWindow::captureHistoricalReturnContext()
{
    if (historicalReviewActive_) return;
    historicalReviewActive_ = true;
    historicalReturnResultsTabIndex_ = resultsWorkspace_->currentIndex();
    if (moduleStack_->currentIndex() == visualizeModuleIndex_
        && !activeModelResultsDirectory_.isEmpty()
        && displayedRunDirectory_ == activeModelResultsDirectory_) {
        historicalReturnModuleIndex_ = visualizeModuleIndex_;
    } else {
        historicalReturnModuleIndex_ = lastNonResultsModuleIndex_;
    }
}

void MainWindow::leaveHistoricalSessionViews()
{
    optimizationWorkspace_->leaveHistoricalSession();
    convergenceWorkspace_->leaveHistoricalSession();
    optimizationWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
        modelChecked_ && modelErrorCount_ == 0);
    convergenceWorkspace_->setContext(editor_->toPlainText(), currentFile_,
        solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_,
        modelChecked_ && modelErrorCount_ == 0);
}

void MainWindow::returnToCurrentWork()
{
    leaveHistoricalSessionViews();
    historicalSessionViewActive_ = false;
    historicalReviewActive_ = false;
    returnToActiveResultsButton_->setVisible(false);
    if (historicalReturnModuleIndex_ == visualizeModuleIndex_
        && !activeModelResultsDirectory_.isEmpty()) {
        if (QFileInfo::exists(activeModelResultsDirectory_)) {
            displayRunArtifacts(activeModelResultsDirectory_, activeModelResultsContext_, false);
            resultsWorkspace_->setCurrentIndex(historicalReturnResultsTabIndex_ == analysisRunsTabIndex_
                ? resultsSummaryTabIndex_ : historicalReturnResultsTabIndex_);
            return;
        }
        activeModelResultsDirectory_.clear();
        activeModelResultsContext_.clear();
    }
    const auto target = historicalReturnModuleIndex_ >= 0
        ? historicalReturnModuleIndex_ : homeModuleIndex_;
    showModule(target);
}

void MainWindow::showHistoricalResultsContext(const QString& modelName, const QString& started,
    const QString& backend)
{
    displayingHistoricalResults_ = true;
    resultsContextTitleLabel_->setText(tr("Historical Run — %1 — %2 — %3")
        .arg(modelName, started, backend.isEmpty() ? tr("Unknown backend") : backend));
    resultsContextDetailLabel_->setText(tr(
        "This is archived output and does not represent or modify the active editor."));
    updateActiveModelResultsLabel();
    returnToActiveResultsButton_->setVisible(true);
}

void MainWindow::showActiveResultsContext(const QString& context)
{
    displayingHistoricalResults_ = false;
    historicalReviewActive_ = false;
    historicalSessionViewActive_ = false;
    resultsContextTitleLabel_->setText(tr("Active Model Results"));
    resultsContextDetailLabel_->setText(tr("Latest solver output for the active editor — %1").arg(context));
    updateActiveModelResultsLabel();
    returnToActiveResultsButton_->setVisible(false);
}

void MainWindow::updateActiveModelResultsLabel()
{
    if (!hasNecModel_) {
        activeModelResultsLabel_->setText(tr("Active Model: None"));
        return;
    }
    const auto name = currentFile_.isEmpty() ? tr("Untitled model.nec") : QFileInfo(currentFile_).fileName();
    activeModelResultsLabel_->setText(tr("Active Model: %1").arg(name));
}

void MainWindow::displayAverageGainTestArtifacts(const QString& directory, const QString& context)
{
    clearDisplayedResults();
    QFile logFile(QDir(directory).filePath(QStringLiteral("run.log")));
    solverOutput_->setPlainText(logFile.open(QIODevice::ReadOnly)
        ? QString::fromLocal8Bit(logFile.readAll()) : tr("No run log is available."));

    QFile metadataFile(QDir(directory).filePath(QStringLiteral("agt.json")));
    auto frequencyMHz = 0.0;
    auto environment = analysis::AverageGainEnvironment::FreeSpace;
    if (metadataFile.open(QIODevice::ReadOnly)) {
        const auto metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
        frequencyMHz = metadata.value(QStringLiteral("frequencyMHz")).toDouble();
        environment = averageGainEnvironmentFromId(
            metadata.value(QStringLiteral("environment")).toString());
    }

    QFile outputFile(QDir(directory).filePath(QStringLiteral("model.out")));
    if (!outputFile.open(QIODevice::ReadOnly)) {
        const auto message = tr("Historical AGT is incomplete — model.out is missing or unreadable.");
        averageGainResultsView_->setFailure(message, context);
        resultsStatusLabel_->setText(tr("%1 %2").arg(message, context));
        showModule(visualizeModuleIndex_);
        resultsWorkspace_->setCurrentIndex(averageGainResultsTabIndex_);
        validationWorkspace_->setCurrentIndex(averageGainValidationTabIndex_);
        return;
    }
    const auto outputBytes = outputFile.readAll();
    analysisOutput_->setPlainText(QString::fromLocal8Bit(outputBytes));
    const auto result = analysis::NecOutputParser{}.parse(std::string_view(
        outputBytes.constData(), static_cast<std::size_t>(outputBytes.size())));
    if (!result.averagePowerGain) {
        const auto message = tr("The archived solver output contains no AVERAGE POWER GAIN value.");
        averageGainResultsView_->setFailure(message, context);
        resultsStatusLabel_->setText(tr("Historical AGT failed — %1").arg(context));
    } else {
        const auto expected = environment == analysis::AverageGainEnvironment::PerfectGround
            ? 2.0 : 1.0;
        const auto assessment = analysis::assessAverageGain(*result.averagePowerGain, expected);
        averageGainResultsView_->setResult(assessment, frequencyMHz, environment,
            result.averagingSolidAnglePi, context);
        resultsStatusLabel_->setText(tr("Historical Average Gain Test — %1").arg(context));
        resultsAvailable_ = true;
        displayedRunDirectory_ = directory;
        const auto selectedRow = analysisRuns_->currentRow();
        if (selectedRow >= 0) {
            AnalysisRunRecord summary;
            summary.runType = QStringLiteral("average-gain-test");
            summary.summary = tr("AGT %1 · %2").arg(assessment.normalizedGain, 0, 'f', 4)
                .arg(averageGainClassificationName(assessment.classification));
            analysisRuns_->setItem(selectedRow, RunResultsColumn,
                new QTableWidgetItem(runResultsText(summary)));
            analysisRuns_->setItem(selectedRow, RunOutputSizeColumn,
                new QTableWidgetItem(formatByteSize(outputBytes.size())));
        }
    }
    showModule(visualizeModuleIndex_);
    resultsWorkspace_->setCurrentIndex(averageGainResultsTabIndex_);
    validationWorkspace_->setCurrentIndex(averageGainValidationTabIndex_);
    statusBar()->showMessage(tr("Displaying historical AGT: %1").arg(directory), 5000);
}

void MainWindow::setDisplayedResults(const analysis::AnalysisResult& result)
{
    const auto previousFrequency = resultsFrequencyControl_->currentIndex() >= 0
        ? std::optional{resultsFrequencyControl_->currentData().toDouble()} : std::nullopt;
    displayedResults_ = result;
    std::vector<double> frequencies;
    const auto addFrequency = [&frequencies](double frequencyMHz) {
        if (std::ranges::find_if(frequencies, [frequencyMHz](double value) {
                return sameResultFrequency(value, frequencyMHz);
            }) == frequencies.end()) frequencies.push_back(frequencyMHz);
    };
    for (const auto& value : result.feedpoints) addFrequency(value.frequencyMHz);
    for (const auto& value : result.currents) addFrequency(value.frequencyMHz);
    for (const auto& value : result.radiation) addFrequency(value.frequencyMHz);
    std::ranges::sort(frequencies);

    const QSignalBlocker blocker(resultsFrequencyControl_);
    resultsFrequencyControl_->clear();
    for (const auto frequencyMHz : frequencies) {
        const auto hasImpedance = std::ranges::any_of(result.feedpoints, [frequencyMHz](const auto& value) {
            return sameResultFrequency(value.frequencyMHz, frequencyMHz);
        });
        const auto hasCurrents = std::ranges::any_of(result.currents, [frequencyMHz](const auto& value) {
            return sameResultFrequency(value.frequencyMHz, frequencyMHz);
        });
        const auto hasRadiation = std::ranges::any_of(result.radiation, [frequencyMHz](const auto& value) {
            return sameResultFrequency(value.frequencyMHz, frequencyMHz);
        });
        QStringList available;
        if (hasImpedance) available.append(tr("Z"));
        if (hasCurrents) available.append(tr("I"));
        if (hasRadiation) available.append(tr("RP"));
        resultsFrequencyControl_->addItem(tr("%1 MHz — %2")
            .arg(frequencyMHz, 0, 'g', 10).arg(available.join(QStringLiteral(" · "))), frequencyMHz);
    }
    resultsFrequencyControl_->setEnabled(!frequencies.empty());
    auto selectedIndex = 0;
    if (previousFrequency) {
        for (auto index = 0; index < resultsFrequencyControl_->count(); ++index) {
            if (sameResultFrequency(resultsFrequencyControl_->itemData(index).toDouble(), *previousFrequency)) {
                selectedIndex = index;
                break;
            }
        }
    }
    if (!frequencies.empty()) {
        resultsFrequencyControl_->setCurrentIndex(selectedIndex);
        applyResultFrequency(resultsFrequencyControl_->currentData().toDouble());
    } else {
        resultsAvailabilityLabel_->setText(tr("No parsed frequency results are available."));
    }
}

void MainWindow::applyResultFrequency(double frequencyMHz)
{
    const auto hasImpedance = std::ranges::any_of(displayedResults_.feedpoints, [frequencyMHz](const auto& value) {
        return sameResultFrequency(value.frequencyMHz, frequencyMHz);
    });
    const auto hasCurrents = std::ranges::any_of(displayedResults_.currents, [frequencyMHz](const auto& value) {
        return sameResultFrequency(value.frequencyMHz, frequencyMHz);
    });
    const auto hasRadiation = std::ranges::any_of(displayedResults_.radiation, [frequencyMHz](const auto& value) {
        return sameResultFrequency(value.frequencyMHz, frequencyMHz);
    });
    QStringList available;
    QStringList missing;
    const auto addStatus = [&available, &missing](bool present, const QString& name) {
        (present ? available : missing).append(name);
    };
    addStatus(hasImpedance, tr("impedance"));
    addStatus(hasCurrents, tr("currents"));
    addStatus(hasRadiation, tr("radiation"));
    auto text = tr("Available: %1").arg(available.empty() ? tr("none") : available.join(QStringLiteral(" · ")));
    if (!missing.empty()) text += tr("  |  Missing: %1").arg(missing.join(QStringLiteral(" · ")));
    resultsAvailabilityLabel_->setText(text);

    resultsSummaryView_->setSelectedFrequency(frequencyMHz);
    analysisResultsView_->setSelectedFrequency(frequencyMHz);
    visualizePlotsView_->setSelectedFrequency(frequencyMHz);
    currentResultsView_->setSelectedFrequency(frequencyMHz);
    radiationPatternView_->setSelectedFrequency(frequencyMHz);
    radiation3DView_->setSelectedFrequency(frequencyMHz);
}

void MainWindow::jumpRawOutputToSelectedFrequency()
{
    if (resultsFrequencyControl_->currentIndex() < 0 || analysisOutput_->document()->isEmpty()) return;
    const auto target = resultsFrequencyControl_->currentData().toDouble();
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
        if (!valid || !sameResultFrequency(value, target)) continue;
        QTextCursor cursor(block);
        analysisOutput_->setTextCursor(cursor);
        analysisOutput_->centerCursor();
        resultsWorkspace_->setCurrentIndex(analysisOutputTabIndex_);
        statusBar()->showMessage(tr("Raw output positioned at %1 MHz.").arg(target, 0, 'g', 10), 3000);
        return;
    }
    statusBar()->showMessage(tr("No raw-output section was found for %1 MHz.").arg(target, 0, 'g', 10), 5000);
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
    const analysis::AnalysisResult empty;
    const auto context = tr("No analysis run");
    resultsSummaryView_->clear();
    averageGainResultsView_->clear();
    dashboardPage_->clearAverageGain();
    convergenceWorkspace_->clearResults();
    dashboardPage_->clearConvergence();
    analysisResultsView_->setResults(empty, context);
    visualizePlotsView_->setResults(empty, context);
    currentResultsView_->setResults(empty, context);
    radiationPatternView_->setResults(empty, context);
    radiation3DView_->setResults(empty, context);
    radiation3DView_->setModel({});
    setDisplayedResults(empty);
    displayedRunDirectory_.clear();
    resultsAvailable_ = false;
    displayingHistoricalResults_ = false;
    resultsContextTitleLabel_->setText(tr("No Results Loaded"));
    resultsContextDetailLabel_->setText(
        tr("Select a run from Results → Runs, or analyze the active model."));
    updateActiveModelResultsLabel();
    returnToActiveResultsButton_->setVisible(false);
    resultsStatusLabel_->setText(tr("No analysis results are loaded."));
}

void MainWindow::editWire(const model::Wire& original, const model::Wire& updated)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(original.sourceLine) - 1;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        return;
    }
    lines[lineIndex] = QString::fromStdString(
        nec::NecWriter{}.writeWireCard(updated, deckScaleForSourceLine(original.sourceLine)));
    pushGeometrySourceEdit(tr("Edit wire %1").arg(original.tag), lines.join(QLatin1Char('\n')));
    selectWireInProject(updated.tag);
}

void MainWindow::editStructuredCard(std::size_t sourceLine, const QString& cardText)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(sourceLine) - 1;
    if (lineIndex < 0 || lineIndex >= lines.size() || lines[lineIndex] == cardText) return;
    lines[lineIndex] = cardText;
    pushGeometrySourceEdit(tr("Edit structured card on line %1").arg(sourceLine),
        lines.join(QLatin1Char('\n')));
}

void MainWindow::addStructuredCard(const QString& cardText)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto document = nec::NecParser{}.parse(editor_->toPlainText().toStdString());
    auto lastGeometryIndex = -1;
    auto hasGeometryEnd = false;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::GeometryWire)
            lastGeometryIndex = static_cast<int>(card.lineNumber)-1;
        if (card.kind == nec::NecCardKind::GeometryEnd) hasGeometryEnd = true;
    }
    if (lastGeometryIndex >= 0 && !hasGeometryEnd) {
        const auto grounded = currentSetup_.ground
            && currentSetup_.ground->type != model::GroundType::FreeSpace;
        lines.insert(lastGeometryIndex+1,
            QString::fromStdString(nec::NecWriter{}.writeGeometryEndCard(grounded ? 1 : 0)));
    }
    const auto mnemonic = cardText.section(QLatin1Char(' '), 0, 0).toUpper();
    auto insertion = lines.size();
    if (mnemonic == QStringLiteral("GS")) {
        for (auto index = 0; index < lines.size(); ++index) {
            if (lines[index].trimmed().section(QLatin1Char(' '), 0, 0).compare(
                    QStringLiteral("GE"), Qt::CaseInsensitive) == 0) {
                insertion = index;
                break;
            }
        }
    }
    for (auto index = 0; index < lines.size(); ++index) {
        if (mnemonic == QStringLiteral("GS") && insertion != lines.size()) break;
        const auto lineMnemonic = lines[index].trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
        if (lineMnemonic == QStringLiteral("EN")
            || (mnemonic != QStringLiteral("XQ") && lineMnemonic == QStringLiteral("XQ"))) {
            insertion = index;
            break;
        }
    }
    lines.insert(insertion, cardText);
    pushGeometrySourceEdit(tr("Add %1 card").arg(mnemonic), lines.join(QLatin1Char('\n')));
}

void MainWindow::deleteStructuredCard(std::size_t sourceLine)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(sourceLine)-1;
    if (lineIndex < 0 || lineIndex >= lines.size()) return;
    const auto mnemonic = lines[lineIndex].trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
    lines.removeAt(lineIndex);
    pushGeometrySourceEdit(tr("Delete %1 card").arg(mnemonic), lines.join(QLatin1Char('\n')));
}

void MainWindow::pushGeometrySourceEdit(const QString& description, QString updatedSource)
{
    const auto originalSource = editor_->toPlainText();
    if (originalSource == updatedSource) {
        return;
    }
    const auto targetTabIndex = moduleStack_->currentIndex() == analysisModuleIndex_
        ? setupTabIndex_
        : moduleStack_->currentIndex() == sourceModuleIndex_
            ? sourceTabIndex_ : workspace_->currentIndex();
    undoStack_->push(new GeometrySourceCommand(description, originalSource, std::move(updatedSource),
        [this, targetTabIndex](const QString& source) { applyGeometrySource(source, targetTabIndex); }));
}

void MainWindow::applyGeometrySource(const QString& source, int targetTabIndex)
{
    updatingSourceFromGeometry_ = true;
    editor_->setPlainText(source);
    updatingSourceFromGeometry_ = false;
    editor_->document()->setUndoRedoEnabled(false);
    editor_->document()->setUndoRedoEnabled(true);
    editor_->document()->setModified(true);
    checkModel();
    if (targetTabIndex == sourceTabIndex_) {
        showModule(sourceModuleIndex_);
        sourceWorkspace_->setCurrentIndex(structuredSourceTabIndex_);
    } else {
        showModelTab(targetTabIndex);
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

void MainWindow::replaceWireSourceLine(const model::Wire& wire)
{
    auto block = editor_->document()->findBlockByNumber(static_cast<int>(wire.sourceLine) - 1);
    if (!block.isValid()) {
        return;
    }

    const auto previousCursor = editor_->textCursor();
    QTextCursor lineCursor(block);
    lineCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    updatingSourceFromGeometry_ = true;
    lineCursor.insertText(QString::fromStdString(
        nec::NecWriter{}.writeWireCard(wire, deckScaleForSourceLine(wire.sourceLine))));
    updatingSourceFromGeometry_ = false;
    editor_->setTextCursor(previousCursor);
    editor_->document()->setUndoRedoEnabled(false);
    editor_->document()->setUndoRedoEnabled(true);
    editor_->document()->setModified(true);
    updateUndoActions();
}

void MainWindow::refreshGeometryViews()
{
    wireCardEditor_->setModel(currentModel_);
    xyView_->updateModel(currentModel_);
    xzView_->updateModel(currentModel_);
    yzView_->updateModel(currentModel_);
    geometry3DView_->updateModel(currentModel_);
}

void MainWindow::performUndo()
{
    if (moduleStack_->currentIndex() == modelModuleIndex_
        || moduleStack_->currentIndex() == analysisModuleIndex_
        || (moduleStack_->currentIndex() == sourceModuleIndex_
            && sourceWorkspace_->currentIndex() == structuredSourceTabIndex_)) {
        undoStack_->undo();
    } else if (moduleStack_->currentIndex() == sourceModuleIndex_
        || moduleStack_->currentIndex() == homeModuleIndex_) {
        editor_->undo();
    }
}

void MainWindow::performRedo()
{
    if (moduleStack_->currentIndex() == modelModuleIndex_
        || moduleStack_->currentIndex() == analysisModuleIndex_
        || (moduleStack_->currentIndex() == sourceModuleIndex_
            && sourceWorkspace_->currentIndex() == structuredSourceTabIndex_)) {
        undoStack_->redo();
    } else if (moduleStack_->currentIndex() == sourceModuleIndex_
        || moduleStack_->currentIndex() == homeModuleIndex_) {
        editor_->redo();
    }
}

void MainWindow::updateUndoActions()
{
    const bool geometryActive = moduleStack_ != nullptr
        && (moduleStack_->currentIndex() == modelModuleIndex_
            || moduleStack_->currentIndex() == analysisModuleIndex_
            || (moduleStack_->currentIndex() == sourceModuleIndex_
                && sourceWorkspace_->currentIndex() == structuredSourceTabIndex_));
    const bool sourceActive = moduleStack_ != nullptr
        && ((moduleStack_->currentIndex() == sourceModuleIndex_
                && sourceWorkspace_->currentIndex() == 0)
            || moduleStack_->currentIndex() == homeModuleIndex_);
    undoAction_->setEnabled(geometryActive ? undoStack_->canUndo()
                                           : sourceActive && editor_->document()->isUndoAvailable());
    redoAction_->setEnabled(geometryActive ? undoStack_->canRedo()
                                           : sourceActive && editor_->document()->isRedoAvailable());
    undoAction_->setText(geometryActive && undoStack_->canUndo()
            ? tr("&Undo %1").arg(undoStack_->undoText())
            : tr("&Undo"));
    redoAction_->setText(geometryActive && undoStack_->canRedo()
            ? tr("&Redo %1").arg(undoStack_->redoText())
            : tr("&Redo"));
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
    auto* environment = new QTreeWidgetItem(root, {tr("Environment")});
    auto* frequencySources = new QTreeWidgetItem(root, {tr("Frequency && Sources")});
    auto* attachments = new QTreeWidgetItem(root, {tr("Loads && Networks")});
    auto* requests = new QTreeWidgetItem(root, {tr("Requests && Execution")});
    auto* comments = new QTreeWidgetItem(root, {tr("Comments")});
    auto* other = new QTreeWidgetItem(root, {tr("Other Cards")});
    auto* allCards = new QTreeWidgetItem(root, {tr("All Cards")});
    for (auto* category : {parameters, environment, frequencySources, attachments, requests, comments, other, allCards})
        category->setData(0, ItemKindRole, QStringLiteral("category"));

    const auto categoryFor = [&](const nec::NecCard& card) -> QTreeWidgetItem* {
        const auto mnemonic = QString::fromStdString(card.mnemonic).toUpper();
        if (card.kind == nec::NecCardKind::Comment) return comments;
        if (card.kind == nec::NecCardKind::Symbol) return parameters;
        if (card.kind == nec::NecCardKind::GeometryWire
            || QStringList{QStringLiteral("GA"), QStringLiteral("GH"), QStringLiteral("GM"),
                   QStringLiteral("GR"), QStringLiteral("GS"), QStringLiteral("GX"),
                   QStringLiteral("SP"), QStringLiteral("SM"), QStringLiteral("SC"),
                   QStringLiteral("GF")}.contains(mnemonic)) return geometry;
        if (card.kind == nec::NecCardKind::GeometryEnd || card.kind == nec::NecCardKind::Ground
            || QStringList{QStringLiteral("EK"), QStringLiteral("GD"), QStringLiteral("KH")}
                   .contains(mnemonic)) return environment;
        if (card.kind == nec::NecCardKind::Frequency || card.kind == nec::NecCardKind::Excitation)
            return frequencySources;
        if (card.kind == nec::NecCardKind::Load || card.kind == nec::NecCardKind::TransmissionLine
            || card.kind == nec::NecCardKind::Network) return attachments;
        if (card.kind == nec::NecCardKind::RadiationPattern || card.kind == nec::NecCardKind::Execute
            || card.kind == nec::NecCardKind::End
            || QStringList{QStringLiteral("NE"), QStringLiteral("NH"), QStringLiteral("PQ"),
                   QStringLiteral("PT"), QStringLiteral("CP"), QStringLiteral("WG"),
                   QStringLiteral("NX")}.contains(mnemonic)) return requests;
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
    labelCategory(frequencySources, tr("Frequency && Sources"));
    labelCategory(attachments, tr("Loads && Networks"));
    labelCategory(requests, tr("Requests && Execution"));
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

void MainWindow::showProjectItemProperties(QTreeWidgetItem* item)
{
    properties_->setRowCount(0);
    const auto kind = item->data(0, ItemKindRole).toString();
    if (kind == QStringLiteral("wire")) {
        const auto tag = item->data(0, WireTagRole).toInt();
        synchronizeGeometrySelection(tag);
        setupEditor_->selectExcitation(0);
        if (const auto* wire = currentModel_.wireByTag(tag)) populateWireProperties(*wire);
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
            addPropertyRow(properties_, tr("Object"), tr("Voltage Source (EX 0)"));
            addPropertyRow(properties_, tr("Wire Tag"), QString::number(found->wireTag));
            addPropertyRow(properties_, tr("Segment"), QString::number(found->segment));
            addPropertyRow(properties_, tr("Magnitude"), QString::number(found->magnitude, 'g', 10));
            addPropertyRow(properties_, tr("Phase"), tr("%1°").arg(found->phaseDegrees, 0, 'g', 10));
            addPropertyRow(properties_, tr("Source Line"), QString::number(found->sourceLine));
        }
    } else if (kind == QStringLiteral("load")) {
        const auto sourceLine = item->data(0, SourceLineRole).toULongLong();
        const auto found = std::ranges::find(currentSetup_.loads, sourceLine,
            &model::LoadDefinition::sourceLine);
        if (found != currentSetup_.loads.end()) {
            xyView_->selectLoad(sourceLine); xzView_->selectLoad(sourceLine);
            yzView_->selectLoad(sourceLine); geometry3DView_->selectLoad(sourceLine);
            loadNetworkEditor_->selectLoad(sourceLine);
            populateLoadProperties(*found);
        }
    } else if (kind == QStringLiteral("transmissionLine")) {
        const auto sourceLine = item->data(0, SourceLineRole).toULongLong();
        const auto found = std::ranges::find(currentSetup_.transmissionLines, sourceLine,
            &model::TransmissionLineDefinition::sourceLine);
        if (found != currentSetup_.transmissionLines.end()) {
            xyView_->selectTransmissionLine(sourceLine); xzView_->selectTransmissionLine(sourceLine);
            yzView_->selectTransmissionLine(sourceLine); geometry3DView_->selectTransmissionLine(sourceLine);
            loadNetworkEditor_->selectTransmissionLine(sourceLine);
            populateTransmissionLineProperties(*found);
        }
    } else if (kind == QStringLiteral("card")) {
        showCardProperties(item->data(0, SourceLineRole).toULongLong());
    } else {
        addPropertyRow(properties_, tr("Selection"), item->text(0));
    }
    properties_->resizeColumnToContents(0);
}

void MainWindow::showCardProperties(std::size_t sourceLine)
{
    if (properties_ == nullptr || editor_ == nullptr || sourceLine == 0) return;
    const auto document = nec::NecParser{}.parse(editor_->toPlainText().toStdString());
    const auto found = std::ranges::find(document.cards(), sourceLine, &nec::NecCard::lineNumber);
    properties_->setRowCount(0);
    if (found == document.cards().end()) {
        addPropertyRow(properties_, tr("Source Line"), QString::number(sourceLine));
        properties_->resizeColumnToContents(0);
        return;
    }
    if (found->kind == nec::NecCardKind::GeometryWire) {
        const auto wire = std::ranges::find(currentModel_.wires(), sourceLine, &model::Wire::sourceLine);
        if (wire != currentModel_.wires().end()) {
            populateWireProperties(*wire);
            properties_->resizeColumnToContents(0);
            return;
        }
    }
    const auto mnemonic = QString::fromStdString(found->mnemonic);
    addPropertyRow(properties_, tr("Object"), mnemonic.isEmpty() ? tr("Blank Line") : tr("NEC Card %1").arg(mnemonic));
    addPropertyRow(properties_, tr("Source Line"), QString::number(found->lineNumber));
    addPropertyRow(properties_, tr("Raw Card"), QString::fromStdString(found->sourceText));
    const auto labels = cardPropertyLabels(mnemonic);
    for (std::size_t index = 0; index < found->fields.size(); ++index) {
        const auto label = static_cast<int>(index) < labels.size()
            ? labels[static_cast<int>(index)] : tr("Field %1").arg(index+1);
        addPropertyRow(properties_, label, QString::fromStdString(found->fields[index]));
    }
    properties_->resizeColumnToContents(0);
}

void MainWindow::populateWireProperties(const model::Wire& wire)
{
    addPropertyRow(properties_, tr("Object"), tr("Wire (GW)"));
    addPropertyRow(properties_, tr("Tag"), QString::number(wire.tag));
    addPropertyRow(properties_, tr("Segments"), QString::number(wire.segments));
    const auto symbol = model::lengthUnitSymbol(geometrySettings_.lengthUnit);
    const auto unit = QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size()));
    addPropertyRow(properties_, tr("Start"), tr("%1, %2, %3 %4")
        .arg(model::fromMeters(wire.start.x, geometrySettings_.lengthUnit))
        .arg(model::fromMeters(wire.start.y, geometrySettings_.lengthUnit))
        .arg(model::fromMeters(wire.start.z, geometrySettings_.lengthUnit)).arg(unit));
    addPropertyRow(properties_, tr("End"), tr("%1, %2, %3 %4")
        .arg(model::fromMeters(wire.end.x, geometrySettings_.lengthUnit))
        .arg(model::fromMeters(wire.end.y, geometrySettings_.lengthUnit))
        .arg(model::fromMeters(wire.end.z, geometrySettings_.lengthUnit)).arg(unit));

    const auto original = wire;
    auto* radiusControl = new QDoubleSpinBox(properties_);
    radiusControl->setObjectName(QStringLiteral("wireRadiusPropertyEditor"));
    radiusControl->setDecimals(9);
    radiusControl->setRange(model::fromMeters(1.0e-9, geometrySettings_.lengthUnit),
        model::fromMeters(1.0e12, geometrySettings_.lengthUnit));
    radiusControl->setSuffix(QStringLiteral(" %1").arg(unit));
    radiusControl->setKeyboardTracking(false);
    radiusControl->setValue(model::fromMeters(wire.radius, geometrySettings_.lengthUnit));
    connect(radiusControl, &QDoubleSpinBox::editingFinished, this, [this, radiusControl, original] {
        const auto radius = model::toMeters(radiusControl->value(), geometrySettings_.lengthUnit);
        if (radius == original.radius) return;
        QTimer::singleShot(0, this, [this, original, radius] {
            auto updated = original;
            updated.radius = radius;
            editWire(original, updated);
        });
    });
    addPropertyWidgetRow(properties_, tr("Radius"), radiusControl);

    auto* gaugeControl = new QComboBox(properties_);
    gaugeControl->setObjectName(QStringLiteral("wireGaugePropertyEditor"));
    gaugeControl->addItem(tr("Custom radius"));
    for (auto gauge = -3; gauge <= 40; ++gauge)
        gaugeControl->addItem(QString::fromStdString(model::awgLabel(gauge)), gauge);
    if (const auto gauge = model::matchingAwg(wire.radius))
        gaugeControl->setCurrentIndex(gaugeControl->findData(*gauge));
    connect(gaugeControl, &QComboBox::currentIndexChanged, this, [this, gaugeControl, original] {
        if (!gaugeControl->currentData().isValid()) return;
        const auto gauge = gaugeControl->currentData().toInt();
        QTimer::singleShot(0, this, [this, original, gauge] {
            auto updated = original;
            updated.radius = model::awgRadiusMeters(gauge);
            editWire(original, updated);
        });
    });
    gaugeControl->setToolTip(tr("Nominal bare-conductor American Wire Gauge; selection updates radius"));
    addPropertyWidgetRow(properties_, tr("Wire Gauge"), gaugeControl);
    addPropertyRow(properties_, tr("Source Line"), QString::number(wire.sourceLine));
}

void MainWindow::populateLoadProperties(const model::LoadDefinition& load)
{
    static const QStringList typeNames{
        tr("Series RLC"), tr("Parallel RLC"), tr("Distributed series RLC"),
        tr("Distributed parallel RLC"), tr("Fixed complex impedance"),
        tr("Wire conductivity")};
    addPropertyRow(properties_, tr("Object"), tr("Load (LD)"));
    addPropertyRow(properties_, tr("Type"), load.type >= 0 && load.type < typeNames.size()
        ? tr("%1 — %2").arg(load.type).arg(typeNames[load.type]) : QString::number(load.type));
    addPropertyRow(properties_, tr("Wire Tag"), QString::number(load.wireTag));
    addPropertyRow(properties_, tr("Segment Range"), tr("%1–%2").arg(load.firstSegment).arg(load.lastSegment));
    addPropertyRow(properties_, tr("Value 1"), QString::number(load.value1, 'g', 10));
    addPropertyRow(properties_, tr("Value 2"), QString::number(load.value2, 'g', 10));
    addPropertyRow(properties_, tr("Value 3"), QString::number(load.value3, 'g', 10));
    addPropertyRow(properties_, tr("Source Line"), QString::number(load.sourceLine));
}

void MainWindow::populateTransmissionLineProperties(
    const model::TransmissionLineDefinition& line)
{
    addPropertyRow(properties_, tr("Object"), tr("Transmission Line (TL)"));
    addPropertyRow(properties_, tr("Endpoint 1"), tr("Wire %1, segment %2")
        .arg(line.wireTag1).arg(line.segment1));
    addPropertyRow(properties_, tr("Endpoint 2"), tr("Wire %1, segment %2")
        .arg(line.wireTag2).arg(line.segment2));
    addPropertyRow(properties_, tr("Characteristic Z0"), tr("%1 Ω")
        .arg(line.characteristicImpedance, 0, 'g', 10));
    addPropertyRow(properties_, tr("Length"), formatLength(line.lengthMeters));
    addPropertyRow(properties_, tr("Shunt 1"), tr("%1 + j%2")
        .arg(line.shuntReal1, 0, 'g', 10).arg(line.shuntImaginary1, 0, 'g', 10));
    addPropertyRow(properties_, tr("Shunt 2"), tr("%1 + j%2")
        .arg(line.shuntReal2, 0, 'g', 10).arg(line.shuntImaginary2, 0, 'g', 10));
    addPropertyRow(properties_, tr("Source Line"), QString::number(line.sourceLine));
}

void MainWindow::setCurrentFile(QString path)
{
    currentFile_ = std::move(path);
    if (!currentFile_.isEmpty()) rememberRecentFile(currentFile_);
    const auto name = currentFile_.isEmpty() ? tr("Untitled") : QFileInfo(currentFile_).fileName();
    setWindowTitle(tr("%1[*] — NEC Workbench").arg(name));
    const auto modelName = currentFile_.isEmpty() ? tr("Untitled model.nec") : name;
    modelFileStatus_->setText(tr("Open model: %1").arg(modelName));
    modelFileStatus_->setToolTip(currentFile_.isEmpty() ? tr("This model has not been saved yet.") : currentFile_);
    if (dashboardPage_ != nullptr)
        dashboardPage_->setDocumentState(currentFile_.isEmpty() ? QString{} : name,
            editor_->document()->isModified());
    if (activeModelResultsLabel_ != nullptr) updateActiveModelResultsLabel();
}

void MainWindow::restoreWorkspaceLayout()
{
    QSettings settings;
    const auto normalGeometry = settings.value(QStringLiteral("mainWindow/normalGeometry")).toRect();
    setWindowState(Qt::WindowNoState);
    if (normalGeometry.isValid()) {
        setGeometry(normalGeometry);
    }
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
    if (settings.value(QStringLiteral("mainWindow/maximized"), false).toBool()) {
        QTimer::singleShot(0, this, [this] { showMaximized(); });
    }
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
    solverExecutablePath_ = settings.value(QStringLiteral("analysis/executablePath")).toString();
    solverTimeoutSeconds_ = settings.value(QStringLiteral("analysis/timeoutSeconds"), 120).toInt();
    if (solverBackendId_ == QStringLiteral("nec2") && solverExecutablePath_.isEmpty()) {
        solverExecutablePath_ = QStandardPaths::findExecutable(QStringLiteral("nec2c"));
    }
    analysisSetupEditor_->setSettings(solverBackendId_, solverExecutablePath_, solverTimeoutSeconds_);
    loadRunHistory();
    updateAnalysisReadiness();
}

void MainWindow::saveWorkspaceLayout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/normalGeometry"), normalGeometry());
    settings.setValue(QStringLiteral("mainWindow/maximized"), isMaximized());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
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
    settings.setValue(QStringLiteral("analysis/executablePath"), solverExecutablePath_);
    settings.setValue(QStringLiteral("analysis/timeoutSeconds"), solverTimeoutSeconds_);
}

}
