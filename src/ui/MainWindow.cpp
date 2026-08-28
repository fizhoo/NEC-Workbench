#include "ui/MainWindow.h"

#include "analysis/SolverCommand.h"
#include "analysis/NecOutputParser.h"
#include "nec/NecModelChecker.h"
#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
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
#include "ui/analysis/ImpedanceResultsView.h"
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
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QStringList>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
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
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <span>
#include <utility>

namespace necwb::ui {
namespace {

constexpr auto ItemKindRole = Qt::UserRole;
constexpr auto WireTagRole = Qt::UserRole + 1;
constexpr auto SourceLineRole = Qt::UserRole + 2;
constexpr auto RunDirectoryRole = Qt::UserRole + 3;
constexpr auto RunContextRole = Qt::UserRole + 4;

auto runContext(const AnalysisRunRecord& record) -> QString
{
    const auto modelName = record.sourceFile.isEmpty()
        ? QObject::tr("Archived model.nec")
        : QFileInfo(record.sourceFile).fileName();
    return QObject::tr("Model: %1 · Run: %2").arg(modelName, record.id);
}

auto cardPropertyLabels(const QString& mnemonic) -> QStringList
{
    if (mnemonic == QStringLiteral("GW")) return {QObject::tr("Tag"), QObject::tr("Segments"),
        QObject::tr("X1"), QObject::tr("Y1"), QObject::tr("Z1"), QObject::tr("X2"),
        QObject::tr("Y2"), QObject::tr("Z2"), QObject::tr("Radius")};
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

auto createPlaceholder(const QString& title, const QString& description, QWidget* parent) -> QWidget*
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);

    auto* heading = new QLabel(title, page);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 5);
    heading->setFont(font);

    auto* body = new QLabel(description, page);
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(heading);
    layout->addWidget(body);
    layout->addStretch();
    return page;
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
    if (solverProcess_ != nullptr) {
        const auto answer = QMessageBox::question(this, tr("Analysis Running"),
            tr("A solver run is still active. Cancel it and close NEC Workbench?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        solverProcess_->kill();
        solverProcess_->waitForFinished(2000);
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
    homeModuleAction_ = moduleGroup->addAction(tr("&Dashboard"));
    modelModuleAction_ = moduleGroup->addAction(tr("&Geometry"));
    sourceModuleAction_ = moduleGroup->addAction(tr("NEC &Source"));
    analysisModuleAction_ = moduleGroup->addAction(tr("&Analysis"));
    visualizeModuleAction_ = moduleGroup->addAction(tr("&Results"));
    optimizeModuleAction_ = moduleGroup->addAction(tr("&Optimize"));
    for (auto* action : moduleGroup->actions()) {
        action->setCheckable(true);
    }
    homeModuleAction_->setChecked(true);
    optimizeModuleAction_->setEnabled(false);
    connect(homeModuleAction_, &QAction::triggered, this, [this] { showModule(homeModuleIndex_); });
    connect(modelModuleAction_, &QAction::triggered, this, [this] { showModule(modelModuleIndex_); });
    connect(sourceModuleAction_, &QAction::triggered, this, [this] { showModule(sourceModuleIndex_); });
    connect(analysisModuleAction_, &QAction::triggered, this, [this] { showModule(analysisModuleIndex_); });
    connect(visualizeModuleAction_, &QAction::triggered, this, [this] { showModule(visualizeModuleIndex_); });
    connect(optimizeModuleAction_, &QAction::triggered, this, [this] { showModule(optimizeModuleIndex_); });
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
    dashboardStack_->addWidget(new WelcomePage(newAction_, openAction_, modelModuleAction_,
        sourceModuleAction_, analysisModuleAction_, visualizeModuleAction_, optimizeModuleAction_, dashboardStack_));
    dashboardPage_ = new DashboardPage(editor_->document(), dashboardStack_);
    dashboardStack_->addWidget(dashboardPage_);
    homeModuleIndex_ = moduleStack_->addWidget(dashboardStack_);

    auto* structuredPage = new QTabWidget(sourceWorkspace_);
    structuredPage->setDocumentMode(true);
    wireCardEditor_ = new WireCardEditor(structuredPage);
    structuredPage->addTab(wireCardEditor_, tr("Wires (GW)"));
    structuredCardEditor_ = new StructuredCardEditor(structuredPage);
    structuredPage->addTab(structuredCardEditor_, tr("Other Supported Cards"));
    sourceWorkspace_->addTab(structuredPage, tr("Structured Cards"));
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
    workspace_->addTab(geometry3DPage, tr("3D Geometry"));
    modelModuleIndex_ = moduleStack_->addWidget(workspace_);
    sourceModuleIndex_ = moduleStack_->addWidget(sourceWorkspace_);

    analysisWorkspace_ = new QTabWidget(moduleStack_);
    analysisWorkspace_->setDocumentMode(true);

    setupEditor_ = new SetupEditor(analysisWorkspace_);
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
    analysisWorkspace_->addTab(setupEditor_, tr("Model Setup"));
    setupTabIndex_ = -2;

    loadNetworkEditor_ = new LoadNetworkEditor(analysisWorkspace_);
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadChanged, this,
        [this](model::LoadDefinition load) { changeLoad(load); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::loadDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete load"), sourceLine); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineChanged, this,
        [this](model::TransmissionLineDefinition line) { changeTransmissionLine(line); });
    connect(loadNetworkEditor_, &LoadNetworkEditor::transmissionLineDeleteRequested, this,
        [this](std::size_t sourceLine) { deleteSetupCard(tr("Delete transmission line"), sourceLine); });
    analysisWorkspace_->addTab(loadNetworkEditor_, tr("Loads && Lines"));

    analysisSetupEditor_ = new AnalysisSetupEditor(analysisWorkspace_);
    connect(analysisSetupEditor_, &AnalysisSetupEditor::settingsChanged, this,
        [this](QString backendId, QString executablePath, int timeoutSeconds) {
            solverBackendId_ = std::move(backendId);
            solverExecutablePath_ = std::move(executablePath);
            solverTimeoutSeconds_ = timeoutSeconds;
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

    auto* runsPage = new QWidget(analysisWorkspace_);
    auto* runsLayout = new QVBoxLayout(runsPage);
    runsLayout->setContentsMargins(16, 16, 16, 16);
    auto* runsHeading = new QLabel(tr("Analysis Runs"), runsPage);
    auto runsHeadingFont = runsHeading->font();
    runsHeadingFont.setBold(true);
    runsHeadingFont.setPointSize(runsHeadingFont.pointSize() + 3);
    runsHeading->setFont(runsHeadingFont);
    analysisRuns_ = new QTableWidget(0, 6, runsPage);
    analysisRuns_->setHorizontalHeaderLabels({tr("Started"), tr("Model"), tr("Backend"),
        tr("Status"), tr("Duration"), tr("Run Folder")});
    analysisRuns_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    analysisRuns_->setSelectionBehavior(QAbstractItemView::SelectRows);
    analysisRuns_->setAlternatingRowColors(true);
    analysisRuns_->horizontalHeader()->setStretchLastSection(true);
    auto* runButtons = new QHBoxLayout;
    cancelRunButton_ = new QPushButton(tr("Cancel Active Run"), runsPage);
    cancelRunButton_->setEnabled(false);
    openRunFolderButton_ = new QPushButton(tr("Open Run Folder"), runsPage);
    openRunFolderButton_->setEnabled(false);
    runButtons->addWidget(cancelRunButton_);
    runButtons->addWidget(openRunFolderButton_);
    runButtons->addStretch();
    runsLayout->addWidget(runsHeading);
    runsLayout->addWidget(analysisRuns_, 1);
    runsLayout->addLayout(runButtons);
    connect(cancelRunButton_, &QPushButton::clicked, this, [this] { cancelAnalysis(); });
    connect(openRunFolderButton_, &QPushButton::clicked, this, [this] {
        const auto selectedItems = analysisRuns_->selectedItems();
        const auto directory = selectedItems.empty()
            ? currentRunDirectory_ : selectedItems.front()->data(RunDirectoryRole).toString();
        if (!directory.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
        }
    });
    connect(analysisRuns_, &QTableWidget::itemSelectionChanged, this, [this] {
        openRunFolderButton_->setEnabled(analysisRuns_->currentRow() >= 0);
        loadSelectedRun();
    });
    analysisRunsTabIndex_ = analysisWorkspace_->addTab(runsPage, tr("Runs"));

    analysisModuleIndex_ = moduleStack_->addWidget(analysisWorkspace_);

    auto* resultsPage = new QWidget(moduleStack_);
    auto* resultsLayout = new QVBoxLayout(resultsPage);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsStatusLabel_ = new QLabel(tr("No analysis results are loaded."), resultsPage);
    resultsStatusLabel_->setContentsMargins(12, 7, 12, 7);
    resultsStatusLabel_->setWordWrap(true);
    resultsWorkspace_ = new QTabWidget(resultsPage);
    resultsWorkspace_->setDocumentMode(true);
    analysisResultsView_ = new ImpedanceResultsView(resultsWorkspace_);
    visualizeResultsView_ = analysisResultsView_;
    analysisResultsTabIndex_ = resultsWorkspace_->addTab(analysisResultsView_, tr("Numerical Results"));
    visualizePlotsView_ = new SweepPlotsView(resultsWorkspace_);
    resultsWorkspace_->addTab(visualizePlotsView_, tr("Sweep Plots"));
    currentResultsView_ = new CurrentDistributionView(resultsWorkspace_);
    resultsWorkspace_->addTab(currentResultsView_, tr("Currents"));
    radiationPatternView_ = new RadiationPatternView(resultsWorkspace_);
    resultsWorkspace_->addTab(radiationPatternView_, tr("Radiation 2D"));
    radiation3DView_ = new Radiation3DView(resultsWorkspace_);
    resultsWorkspace_->addTab(radiation3DView_, tr("3D Results"));
    analysisOutput_ = new QPlainTextEdit(resultsWorkspace_);
    analysisOutput_->setReadOnly(true);
    analysisOutput_->setPlaceholderText(tr("Solver command, progress, and NEC output will appear here."));
    analysisOutputTabIndex_ = resultsWorkspace_->addTab(analysisOutput_, tr("Raw NEC Output"));
    resultsLayout->addWidget(resultsStatusLabel_);
    resultsLayout->addWidget(resultsWorkspace_, 1);
    visualizeModuleIndex_ = moduleStack_->addWidget(resultsPage);

    auto* optimizeWorkspace = new QTabWidget(moduleStack_);
    optimizeWorkspace->setDocumentMode(true);
    optimizeWorkspace->addTab(createPlaceholder(tr("Optimization Variables"),
        tr("Geometry and electrical variables will be defined here."), optimizeWorkspace), tr("Variables"));
    optimizeWorkspace->addTab(createPlaceholder(tr("Objectives and Constraints"),
        tr("Define engineering goals, limits, and frequency ranges."), optimizeWorkspace), tr("Objectives"));
    optimizeWorkspace->addTab(createPlaceholder(tr("Optimization Runs"),
        tr("Parameter sweeps, optimizers, history, and comparisons will appear here."), optimizeWorkspace), tr("Runs"));
    optimizeModuleIndex_ = moduleStack_->addWidget(optimizeWorkspace);
    showModule(homeModuleIndex_);
}

void MainWindow::showModule(int index)
{
    if (moduleStack_ == nullptr || index < 0 || index >= moduleStack_->count()) {
        return;
    }
    moduleStack_->setCurrentIndex(index);
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
    wireCardEditor_->setLengthUnit(geometrySettings_.lengthUnit);
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
            lines[lineIndex] = QString::fromStdString(writer.writeWireCard(updated));
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

    diagnosticsDock_ = new QDockWidget(tr("Validation"), this);
    diagnosticsDock_->setObjectName(QStringLiteral("modelDiagnosticsDock"));
    diagnostics_ = new QTreeWidget(diagnosticsDock_);
    diagnostics_->setHeaderLabels({tr("Severity"), tr("Line"), tr("Message")});
    diagnostics_->setRootIsDecorated(false);
    diagnostics_->setAlternatingRowColors(true);
    diagnostics_->header()->setStretchLastSection(true);
    diagnostics_->setColumnWidth(0, 90);
    diagnostics_->setColumnWidth(1, 60);
    diagnosticsDock_->setWidget(diagnostics_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticsDock_);

    solverOutputDock_ = new QDockWidget(tr("Solver Output"), this);
    solverOutputDock_->setObjectName(QStringLiteral("solverOutputDock"));
    solverOutput_ = new QPlainTextEdit(solverOutputDock_);
    solverOutput_->setReadOnly(true);
    solverOutput_->setPlaceholderText(tr("External solver command, progress, and diagnostics will appear here."));
    solverOutputDock_->setWidget(solverOutput_);
    addDockWidget(Qt::BottomDockWidgetArea, solverOutputDock_);
    tabifyDockWidget(diagnosticsDock_, solverOutputDock_);

    messagesDock_ = new QDockWidget(tr("Messages"), this);
    messagesDock_->setObjectName(QStringLiteral("messagesDock"));
    auto* messages = new QPlainTextEdit(messagesDock_);
    messages->setReadOnly(true);
    messages->setPlainText(tr("NEC Workbench messages and workflow notifications will appear here."));
    messagesDock_->setWidget(messages);
    addDockWidget(Qt::BottomDockWidgetArea, messagesDock_);
    tabifyDockWidget(diagnosticsDock_, messagesDock_);
    diagnosticsDock_->raise();

    connect(diagnostics_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) { goToDiagnostic(item); });
    connect(projectTree_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) { showProjectItemProperties(item); });
    connect(projectTree_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item) { activateProjectItem(item); });

    updateProjectTree({}, 0);
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
    viewMenu->addAction(messagesDock_->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(fitGeometryAction_);

    auto* modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->addAction(checkAction_);
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
    undoStack_->clear();
    editor_->setPlainText(tr("CM New NEC Workbench model\nCE\nGE 0\nEN\n"));
    hasNecModel_ = true;
    dashboardStack_->setCurrentIndex(1);
    setCurrentFile({});
    editor_->document()->setModified(false);
    showModelTab(sourceTabIndex_);
    checkModel();
}

void MainWindow::openFile()
{
    if (!maybeSaveChanges()) {
        return;
    }
    const auto path = QFileDialog::getOpenFileName(this, tr("Open NEC File"), {}, tr("NEC files (*.nec);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Open Failed"), file.errorString());
        return;
    }
    undoStack_->clear();
    editor_->setPlainText(QString::fromUtf8(file.readAll()));
    hasNecModel_ = true;
    dashboardStack_->setCurrentIndex(1);
    setCurrentFile(path);
    editor_->document()->setModified(false);
    showModelTab(sourceTabIndex_);
    checkModel();
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
    const auto document = nec::NecParser{}.parse(editor_->toPlainText().toStdString());
    const auto result = nec::NecModelChecker{}.check(document);
    currentModel_ = result.model;
    currentSetup_ = nec::NecSetupConverter{}.convert(document);
    modelErrorCount_ = result.errorCount();
    modelWarningCount_ = result.warningCount();
    modelChecked_ = true;

    diagnostics_->clear();
    for (const auto& diagnostic : result.diagnostics) {
        const bool isError = diagnostic.severity == nec::DiagnosticSeverity::Error;
        auto* item = new QTreeWidgetItem(diagnostics_, {
            isError ? tr("Error") : tr("Warning"),
            QString::number(diagnostic.lineNumber),
            QString::fromStdString(diagnostic.message)});
        item->setData(0, SourceLineRole, static_cast<qulonglong>(diagnostic.lineNumber));
        item->setIcon(0, style()->standardIcon(isError ? QStyle::SP_MessageBoxCritical : QStyle::SP_MessageBoxWarning));
    }
    if (result.diagnostics.empty()) {
        auto* item = new QTreeWidgetItem(diagnostics_, {tr("OK"), {}, tr("No issues found")});
        item->setIcon(0, style()->standardIcon(QStyle::SP_DialogApplyButton));
    }

    editor_->setDiagnostics(result.diagnostics);
    wireCardEditor_->setModel(currentModel_);
    xyView_->setModel(currentModel_);
    xzView_->setModel(currentModel_);
    yzView_->setModel(currentModel_);
    geometry3DView_->setModel(currentModel_);
    xyView_->setExcitations(currentSetup_.excitations);
    xzView_->setExcitations(currentSetup_.excitations);
    yzView_->setExcitations(currentSetup_.excitations);
    geometry3DView_->setExcitations(currentSetup_.excitations);
    setupEditor_->setData(currentModel_, currentSetup_);
    loadNetworkEditor_->setData(currentModel_, currentSetup_);
    analysisRequestEditor_->setData(currentSetup_);
    structuredCardEditor_->setDocument(document);
    dashboardPage_->setModel(currentModel_, currentSetup_, solverBackendId_, true,
        modelErrorCount_, modelWarningCount_);
    updateProjectTree(currentModel_, document.cards().size());
    checkStatus_->setText(tr("Checked: %1 errors, %2 warnings, %3 wires, %4 cards")
        .arg(static_cast<qulonglong>(result.errorCount()))
        .arg(static_cast<qulonglong>(result.warningCount()))
        .arg(static_cast<qulonglong>(currentModel_.wireCount()))
        .arg(static_cast<qulonglong>(document.cards().size())));
    if (!result.diagnostics.empty()) {
        diagnosticsDock_->show();
        diagnosticsDock_->raise();
    }
    updateAnalysisReadiness();
}

void MainWindow::clearCheckResults()
{
    if (!updatingSourceFromGeometry_) {
        undoStack_->clear();
    }
    diagnostics_->clear();
    editor_->setDiagnostics(std::span<const nec::ModelDiagnostic>{});
    checkStatus_->setText(tr("Model changed — check required"));
    modelChecked_ = false;
    dashboardPage_->setModel(currentModel_, currentSetup_, solverBackendId_, false,
        modelErrorCount_, modelWarningCount_);
    if (resultsAvailable_) dashboardPage_->markResultsStale();
    if (resultsAvailable_) resultsStatusLabel_->setText(tr("STALE — the NEC model changed after these results were calculated."));
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
    if (kind == QStringLiteral("geometry")) {
        showModelTab(geometryTabIndex_);
        return;
    }
    if (kind == QStringLiteral("excitation")) {
        showModelTab(setupTabIndex_);
        setupEditor_->selectExcitation(item->data(0, SourceLineRole).toULongLong());
        return;
    }
    if (kind == QStringLiteral("source") || kind == QStringLiteral("wire")) {
        showModelTab(sourceTabIndex_);
        const auto lineNumber = item->data(0, SourceLineRole).toULongLong();
        if (lineNumber != 0) {
            editor_->goToLine(static_cast<std::size_t>(lineNumber));
        }
    }
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

    QTreeWidgetItemIterator iterator(projectTree_);
    while (*iterator != nullptr) {
        auto* item = *iterator;
        if (item->data(0, ItemKindRole).toString() == QStringLiteral("wire")
            && item->data(0, WireTagRole).toInt() == tag) {
            projectTree_->setCurrentItem(item);
            showProjectItemProperties(item);
            propertiesDock_->show();
            return;
        }
        ++iterator;
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

    QTreeWidgetItemIterator iterator(projectTree_);
    while (*iterator != nullptr) {
        auto* item = *iterator;
        if (item->data(0, ItemKindRole).toString() == QStringLiteral("excitation")
            && item->data(0, SourceLineRole).toULongLong() == sourceLine) {
            projectTree_->setCurrentItem(item);
            showProjectItemProperties(item);
            propertiesDock_->show();
            return;
        }
        ++iterator;
    }
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
        if (card.kind == nec::NecCardKind::GeometryEnd) {
            insertionIndex = static_cast<int>(card.lineNumber) - 1;
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
    lines.insert(insertionIndex, QString::fromStdString(nec::NecWriter{}.writeWireCard(wire)));
    if (!hasGeometryEnd) {
        const auto grounded = currentSetup_.ground
            && currentSetup_.ground->type != model::GroundType::FreeSpace;
        lines.insert(insertionIndex+1,
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
    lines[lineIndex] = QString::fromStdString(writer.writeWireCard(first));
    lines.insert(lineIndex + 1, QString::fromStdString(writer.writeWireCard(second)));
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
    lines.insert(lineIndex + 1, QString::fromStdString(nec::NecWriter{}.writeWireCard(duplicate)));
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
    analysisRequestEditor_->setReadiness(blockers);
    runAction_->setEnabled(blockers.empty() && solverProcess_ == nullptr);
    optimizeModuleAction_->setEnabled(modelChecked_ && modelErrorCount_ == 0);
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

    currentRunRecord_ = runStore_.create(solverBackendId_, currentFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : currentFile_);
    currentRunDirectory_ = currentRunRecord_->directory;
    if (!QFileInfo::exists(currentRunDirectory_)) {
        QMessageBox::critical(this, tr("Run Failed"), tr("Could not create the solver run folder."));
        currentRunRecord_.reset();
        return;
    }

    const auto inputPath = QDir(currentRunDirectory_).filePath(QStringLiteral("model.nec"));
    currentRunOutputPath_ = QDir(currentRunDirectory_).filePath(QStringLiteral("model.out"));
    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Run Failed"), inputFile.errorString());
        return;
    }
    inputFile.write(editor_->toPlainText().toUtf8());
    inputFile.close();

    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(solverBackendId_.toStdString(),
            solverExecutablePath_.toStdString(), "model.nec", "model.out");
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Run Failed"), QString::fromLocal8Bit(error.what()));
        return;
    }

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
    showModule(analysisModuleIndex_);
    analysisWorkspace_->setCurrentIndex(analysisRunsTabIndex_);
    solverOutputDock_->show();
    solverOutputDock_->raise();
    statusBar()->showMessage(tr("Running NEC analysis…"));
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
    appendToEditor(analysisOutput_);
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

    QFile outputFile(currentRunOutputPath_);
    if (outputFile.open(QIODevice::ReadOnly)) {
        const auto outputBytes = outputFile.readAll();
        appendSolverOutput(tr("\n\n===== NEC OUTPUT: model.out =====\n"));
        appendSolverOutput(QString::fromLocal8Bit(outputBytes));
        const auto result = analysis::NecOutputParser{}.parse(std::string_view(
            outputBytes.constData(), static_cast<std::size_t>(outputBytes.size())));
        const auto context = runContext(*currentRunRecord_);
        analysisResultsView_->setResults(result, context);
        visualizePlotsView_->setResults(result, context);
        currentResultsView_->setResults(result, context);
        radiationPatternView_->setResults(result, context);
        radiation3DView_->setModel(currentModel_);
        radiation3DView_->setResults(result, context);
        dashboardPage_->setResults(result, context, false);
        dashboardPage_->setModel(currentModel_, currentSetup_, solverBackendId_, true,
            modelErrorCount_, modelWarningCount_);
        resultsAvailable_ = true;
        resultsStatusLabel_->setText(tr("Current results — %1").arg(context));
        if (!result.feedpoints.empty()) {
            showModule(visualizeModuleIndex_);
            resultsWorkspace_->setCurrentIndex(analysisResultsTabIndex_);
        }
    }

    QString status;
    if (currentRunTimedOut_) {
        status = tr("Timed out");
    } else if (currentRunCanceled_) {
        status = tr("Canceled");
    } else if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        status = tr("Completed");
    } else {
        status = tr("Failed (exit %1)").arg(exitCode);
    }
    setCurrentRunStatus(status);
    if (currentRunRow_ >= 0) {
        const auto duration = solverElapsed_.elapsed() / 1000.0;
        analysisRuns_->setItem(currentRunRow_, 4,
            new QTableWidgetItem(tr("%1 s").arg(duration, 0, 'f', 2)));
        if (currentRunRecord_) {
            currentRunRecord_->durationSeconds = duration;
            runStore_.save(*currentRunRecord_);
        }
    }
    appendSolverOutput(tr("\n\nRun status: %1\nElapsed: %2 seconds\n")
        .arg(status).arg(solverElapsed_.elapsed() / 1000.0, 0, 'f', 2));
    statusBar()->showMessage(tr("Analysis %1. Artifacts: %2").arg(status.toLower(), currentRunDirectory_), 10000);
    cancelRunButton_->setEnabled(false);
    solverProcess_->deleteLater();
    solverProcess_ = nullptr;
    updateAnalysisReadiness();
}

void MainWindow::failAnalysis(const QString& message)
{
    if (solverProcess_ == nullptr) {
        return;
    }
    appendSolverOutput(tr("\nFailed to start solver: %1\n").arg(message));
    setCurrentRunStatus(tr("Failed to start"));
    if (currentRunRow_ >= 0) {
        const auto duration = solverElapsed_.elapsed() / 1000.0;
        analysisRuns_->setItem(currentRunRow_, 4,
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
    updateAnalysisReadiness();
}

void MainWindow::setCurrentRunStatus(const QString& status)
{
    if (currentRunRow_ >= 0) {
        analysisRuns_->setItem(currentRunRow_, 3, new QTableWidgetItem(status));
    }
    if (currentRunRecord_) {
        currentRunRecord_->status = status;
        runStore_.save(*currentRunRecord_);
    }
}

void MainWindow::loadRunHistory()
{
    analysisRuns_->setRowCount(0);
    for (const auto& record : runStore_.load()) {
        addRunRecord(record, false);
    }
    if (analysisRuns_->rowCount() > 0) {
        analysisRuns_->selectRow(0);
    }
}

void MainWindow::addRunRecord(const AnalysisRunRecord& record, bool prepend)
{
    const auto row = prepend ? 0 : analysisRuns_->rowCount();
    analysisRuns_->insertRow(row);
    const QStringList values{
        record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        record.sourceFile.isEmpty() ? tr("Archived model.nec") : QFileInfo(record.sourceFile).fileName(),
        record.backend,
        record.status,
        record.durationSeconds > 0.0 ? tr("%1 s").arg(record.durationSeconds, 0, 'f', 2) : tr("—"),
        record.directory,
    };
    for (auto column = 0; column < values.size(); ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        item->setData(RunDirectoryRole, record.directory);
        item->setData(RunContextRole, runContext(record));
        analysisRuns_->setItem(row, column, item);
    }
    analysisRuns_->resizeColumnsToContents();
}

void MainWindow::loadSelectedRun()
{
    if (solverProcess_ != nullptr || analysisRuns_->currentRow() < 0) {
        return;
    }
    const auto* item = analysisRuns_->item(analysisRuns_->currentRow(), 0);
    if (item != nullptr) {
        displayRunArtifacts(item->data(RunDirectoryRole).toString(),
            item->data(RunContextRole).toString());
    }
}

void MainWindow::displayRunArtifacts(const QString& directory, const QString& context)
{
    QFile logFile(QDir(directory).filePath(QStringLiteral("run.log")));
    const auto logText = logFile.open(QIODevice::ReadOnly)
        ? QString::fromLocal8Bit(logFile.readAll()) : tr("No run log is available.");
    solverOutput_->setPlainText(logText);
    analysisOutput_->setPlainText(logText);

    QFile outputFile(QDir(directory).filePath(QStringLiteral("model.out")));
    if (!outputFile.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(tr("Selected run has no solver output: %1").arg(directory), 5000);
        return;
    }
    const auto outputBytes = outputFile.readAll();
    const auto result = analysis::NecOutputParser{}.parse(std::string_view(
        outputBytes.constData(), static_cast<std::size_t>(outputBytes.size())));
    analysisResultsView_->setResults(result, context);
    visualizePlotsView_->setResults(result, context);
    currentResultsView_->setResults(result, context);
    radiationPatternView_->setResults(result, context);
    QFile inputFile(QDir(directory).filePath(QStringLiteral("model.nec")));
    if (inputFile.open(QIODevice::ReadOnly)) {
        const auto runDocument = nec::NecParser{}.parse(
            QString::fromUtf8(inputFile.readAll()).toStdString());
        radiation3DView_->setModel(nec::NecModelConverter{}.convert(runDocument).model);
    }
    radiation3DView_->setResults(result, context);
    resultsStatusLabel_->setText(tr("Historical results — %1").arg(context));
    statusBar()->showMessage(tr("Displaying historical run: %1").arg(directory), 5000);
}

void MainWindow::editWire(const model::Wire& original, const model::Wire& updated)
{
    auto lines = editor_->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const auto lineIndex = static_cast<int>(original.sourceLine) - 1;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        return;
    }
    lines[lineIndex] = QString::fromStdString(nec::NecWriter{}.writeWireCard(updated));
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
    for (auto index = 0; index < lines.size(); ++index) {
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
    lineCursor.insertText(QString::fromStdString(nec::NecWriter{}.writeWireCard(wire)));
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

void MainWindow::updateProjectTree(const model::AntennaModel& model, std::size_t cardCount)
{
    projectTree_->clear();
    const auto projectName = currentFile_.isEmpty() ? tr("Untitled NEC Model") : QFileInfo(currentFile_).fileName();
    auto* root = new QTreeWidgetItem(projectTree_, {projectName});
    root->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    root->setData(0, ItemKindRole, QStringLiteral("model"));

    auto* source = new QTreeWidgetItem(root, {tr("Source Deck (%1 cards)").arg(static_cast<qulonglong>(cardCount))});
    source->setData(0, ItemKindRole, QStringLiteral("source"));

    auto* geometry = new QTreeWidgetItem(root, {tr("Geometry (%1 wires)").arg(static_cast<qulonglong>(model.wireCount()))});
    geometry->setData(0, ItemKindRole, QStringLiteral("geometry"));
    for (const auto& wire : model.wires()) {
        auto* item = new QTreeWidgetItem(geometry, {tr("Wire %1 (%2 segments)").arg(wire.tag).arg(wire.segments)});
        item->setData(0, ItemKindRole, QStringLiteral("wire"));
        item->setData(0, WireTagRole, wire.tag);
        item->setData(0, SourceLineRole, static_cast<qulonglong>(wire.sourceLine));
    }
    auto* sources = new QTreeWidgetItem(root, {
        tr("Sources (%1)").arg(static_cast<qulonglong>(currentSetup_.excitations.size()))});
    sources->setData(0, ItemKindRole, QStringLiteral("sources"));
    for (const auto& excitation : currentSetup_.excitations) {
        auto* item = new QTreeWidgetItem(sources, {
            tr("EX: Wire %1, segment %2").arg(excitation.wireTag).arg(excitation.segment)});
        item->setData(0, ItemKindRole, QStringLiteral("excitation"));
        item->setData(0, WireTagRole, excitation.wireTag);
        item->setData(0, SourceLineRole, static_cast<qulonglong>(excitation.sourceLine));
    }
    projectTree_->expandItem(root);
    projectTree_->expandItem(geometry);
    projectTree_->expandItem(sources);
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

void MainWindow::setCurrentFile(QString path)
{
    currentFile_ = std::move(path);
    const auto name = currentFile_.isEmpty() ? tr("Untitled") : QFileInfo(currentFile_).fileName();
    setWindowTitle(tr("%1[*] — NEC Workbench").arg(name));
    const auto modelName = currentFile_.isEmpty() ? tr("Untitled model.nec") : name;
    modelFileStatus_->setText(tr("Open model: %1").arg(modelName));
    modelFileStatus_->setToolTip(currentFile_.isEmpty() ? tr("This model has not been saved yet.") : currentFile_);
}

void MainWindow::restoreWorkspaceLayout()
{
    QSettings settings;
    const auto normalGeometry = settings.value(QStringLiteral("mainWindow/normalGeometry")).toRect();
    if (normalGeometry.isValid()) {
        setGeometry(normalGeometry);
    } else {
        restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
        const auto restoredNormalGeometry = this->normalGeometry();
        setWindowState(Qt::WindowNoState);
        if (restoredNormalGeometry.isValid()) {
            setGeometry(restoredNormalGeometry);
        }
    }
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
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
