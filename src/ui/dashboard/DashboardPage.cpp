#include "ui/dashboard/DashboardPage.h"

#include "analysis/ResultSummary.h"
#include "model/ModelSummary.h"
#include "nec/NecDocument.h"
#include "ui/DisplayFormat.h"

#include <QAction>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace necwb::ui {
namespace {

auto groundName(model::GroundType ground) -> QString
{
    switch (ground) {
    case model::GroundType::FreeSpace: return QObject::tr("Free space");
    case model::GroundType::Perfect: return QObject::tr("Perfect");
    case model::GroundType::SommerfeldNorton: return QObject::tr("Sommerfeld/Norton");
    case model::GroundType::ReflectionApproximation:
        return QObject::tr("Reflection approximation");
    }
    return QObject::tr("Unknown");
}

auto datasetName(analysis::RadiationDatasetKind kind) -> QString
{
    switch (kind) {
    case analysis::RadiationDatasetKind::Point: return QObject::tr("point");
    case analysis::RadiationDatasetKind::AzimuthCut: return QObject::tr("azimuth cut");
    case analysis::RadiationDatasetKind::ElevationCut: return QObject::tr("elevation cut");
    case analysis::RadiationDatasetKind::FullPattern: return QObject::tr("full pattern");
    }
    return QObject::tr("pattern");
}

auto compactButton(const QString& text, QWidget* parent) -> QToolButton*
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumHeight(25);
    return button;
}

auto countText(std::size_t count, const QString& singular, const QString& plural) -> QString
{
    return count == 0 ? QObject::tr("none")
        : count == 1 ? QObject::tr("1 %1").arg(singular)
        : QObject::tr("%1 %2").arg(static_cast<qulonglong>(count)).arg(plural);
}

}

DashboardPage::DashboardPage(QWidget* parent) : QWidget(parent)
{
    fileName_ = tr("Untitled model.nec");
    modelStatus_ = tr("Model not checked");
    resultStatus_ = tr("Results none");
    lastRunText_ = tr("No completed run");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 7, 8, 6);
    outer->setSpacing(6);

    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("controlPanel"));
    panel->setMinimumWidth(560);
    panel->setMaximumWidth(600);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(6);

    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    modelName_ = new QLabel(panel);
    modelName_->setObjectName(QStringLiteral("controlPanelModelName"));
    auto modelFont = modelName_->font();
    modelFont.setBold(true);
    modelName_->setFont(modelFont);
    backendState_ = new QToolButton(panel);
    backendState_->setObjectName(QStringLiteral("controlPanelBackend"));
    backendState_->setAutoRaise(true);
    backendState_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    modelState_ = new QLabel(panel);
    modelState_->setObjectName(QStringLiteral("controlPanelModelState"));
    resultState_ = new QLabel(panel);
    resultState_->setObjectName(QStringLiteral("controlPanelResultState"));
    header->addWidget(modelName_, 1);
    header->addWidget(backendState_);
    header->addWidget(modelState_);
    header->addWidget(resultState_);
    connect(backendState_, &QToolButton::clicked, this, [this] {
        if (openWorkspace_) openWorkspace_(Workspace::Setup);
    });

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    auto* commands = new QVBoxLayout;
    commands->setSpacing(4);
    auto* open = compactButton(tr("Open…"), panel);
    open->setObjectName(QStringLiteral("controlPanelOpen"));
    auto* save = compactButton(tr("Save"), panel);
    save->setObjectName(QStringLiteral("controlPanelSave"));
    auto* run = compactButton(tr("▶ Run (F5)"), panel);
    run->setObjectName(QStringLiteral("controlPanelRun"));
    run->setMinimumHeight(30);
    auto runFont = run->font();
    runFont.setBold(true);
    run->setFont(runFont);
    auto* sweep = compactButton(tr("Frequency Sweep…"), panel);
    sweep->setObjectName(QStringLiteral("controlPanelFrequencySweep"));
    auto* stop = compactButton(tr("Stop"), panel);
    stop->setObjectName(QStringLiteral("controlPanelStop"));
    stop->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    auto* check = compactButton(tr("Check model"), panel);
    check->setObjectName(QStringLiteral("controlPanelCheck"));
    commands->addWidget(open);
    commands->addWidget(save);
    commands->addSpacing(3);
    auto* runRow = new QHBoxLayout;
    runRow->setSpacing(4);
    runRow->addWidget(run, 1);
    runRow->addWidget(stop);
    commands->addLayout(runRow);
    commands->addWidget(sweep);
    commands->addWidget(check);
    auto* resultsHeading = new QLabel(tr("RESULTS  (live windows)"), panel);
    resultsHeading->setObjectName(QStringLiteral("controlPanelResultsHeading"));
    auto resultsFont = resultsHeading->font();
    resultsFont.setPointSize(std::max(7, resultsFont.pointSize() - 1));
    resultsHeading->setFont(resultsFont);
    commands->addSpacing(4);
    commands->addWidget(resultsHeading);
    impedanceButton_ = compactButton(tr("Impedance / SWR"), panel);
    currentsButton_ = compactButton(tr("Currents"), panel);
    radiationButton_ = compactButton(tr("Radiation"), panel);
    impedanceButton_->setObjectName(QStringLiteral("controlPanelImpedance"));
    currentsButton_->setObjectName(QStringLiteral("controlPanelCurrents"));
    radiationButton_->setObjectName(QStringLiteral("controlPanelRadiation"));
    for (auto* button : {impedanceButton_, currentsButton_, radiationButton_})
        commands->addWidget(button);
    commands->addSpacing(5);
    auto* runs = compactButton(tr("Runs…"), panel);
    runs->setObjectName(QStringLiteral("controlPanelRuns"));
    auto* optimize = compactButton(tr("Optimize…"), panel);
    optimize->setObjectName(QStringLiteral("controlPanelOptimize"));
    commands->addWidget(runs);
    commands->addWidget(optimize);
    commands->addStretch();
    connect(impedanceButton_, &QToolButton::clicked, this, [this] {
        if (openWorkspace_) openWorkspace_(Workspace::Impedance);
    });
    connect(currentsButton_, &QToolButton::clicked, this, [this] {
        if (openWorkspace_) openWorkspace_(Workspace::Currents);
    });
    connect(radiationButton_, &QToolButton::clicked, this, [this] {
        if (openWorkspace_) openWorkspace_(Workspace::Radiation);
    });
    connect(runs, &QToolButton::clicked, this, [this] {
        if (openRunHistory_) openRunHistory_();
        else if (openWorkspace_) openWorkspace_(Workspace::Runs);
    });
    connect(optimize, &QToolButton::clicked, this, [this] {
        if (openWorkspace_) openWorkspace_(Workspace::Optimize);
    });

    auto* modelArea = new QVBoxLayout;
    modelArea->setSpacing(3);
    auto* modelHeading = new QLabel(tr("MODEL  (click a row to edit)"), panel);
    modelHeading->setObjectName(QStringLiteral("controlPanelModelHeading"));
    modelHeading->setFont(resultsFont);
    modelRows_ = new QTableWidget(9, 3, panel);
    modelRows_->setObjectName(QStringLiteral("controlPanelModelRows"));
    modelRows_->horizontalHeader()->hide();
    modelRows_->verticalHeader()->hide();
    modelRows_->setShowGrid(false);
    modelRows_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    modelRows_->setSelectionMode(QAbstractItemView::NoSelection);
    modelRows_->setFocusPolicy(Qt::NoFocus);
    modelRows_->verticalHeader()->setDefaultSectionSize(24);
    modelRows_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    modelRows_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    modelRows_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    modelRows_->setFixedHeight(9 * 24 + 2);
    modelRows_->setMinimumWidth(365);
    const std::array labels{
        tr("Frequency"), tr("Wires"), tr("Sources"), tr("Loads"),
        tr("Trans. lines"), tr("Ground"), tr("Variables"), tr("Requests"), tr("Deck")};
    for (auto row = 0; row < static_cast<int>(labels.size()); ++row) {
        auto* label = new QTableWidgetItem(labels[static_cast<std::size_t>(row)]);
        auto font = label->font();
        font.setBold(true);
        label->setFont(font);
        modelRows_->setItem(row, 0, label);
        modelRows_->setItem(row, 1, new QTableWidgetItem(tr("—")));
        modelRows_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("›")));
    }
    geometryViewButton_ = compactButton(tr("Geometry View"), panel);
    geometryViewButton_->setObjectName(QStringLiteral("controlPanelGeometryView"));
    geometryViewButton_->setAutoRaise(true);
    geometryViewButton_->setToolTip(tr("Open or raise the reusable 2D/3D Geometry View"));
    connect(geometryViewButton_, &QToolButton::clicked, this, [this] {
        if (openWorkspace_) openWorkspace_(Workspace::GeometryView);
    });
    connect(modelRows_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (openEditor_) openEditor_(static_cast<Editor>(row));
    });
    auto* latestHeading = new QLabel(tr("LATEST RESULT"), panel);
    latestHeading->setFont(resultsFont);
    latestResult_ = new QLabel(tr("No analysis results"), panel);
    latestResult_->setObjectName(QStringLiteral("controlPanelLatestResult"));
    latestResult_->setWordWrap(true);
    latestResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* modelHeadingRow = new QHBoxLayout;
    modelHeadingRow->setSpacing(4);
    modelHeadingRow->addWidget(modelHeading, 1);
    modelHeadingRow->addWidget(geometryViewButton_);
    modelArea->addLayout(modelHeadingRow);
    modelArea->addWidget(modelRows_);
    modelArea->addSpacing(3);
    modelArea->addWidget(latestHeading);
    modelArea->addWidget(latestResult_);
    modelArea->addStretch();

    body->addLayout(commands, 0);
    body->addLayout(modelArea, 1);

    footer_ = new QLabel(panel);
    footer_->setObjectName(QStringLiteral("controlPanelFooter"));
    footer_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    panelLayout->addLayout(header);
    panelLayout->addLayout(body, 1);
    panelLayout->addWidget(footer_);
    outer->addWidget(panel, 0, Qt::AlignTop | Qt::AlignLeft);
    outer->addStretch();

    refreshHeader();
    refreshFooter();
    for (const auto workspace : {Workspace::Impedance, Workspace::Currents,
             Workspace::Radiation})
        refreshResultButton(workspace);
    refreshGeometryViewButton();
}

void DashboardPage::setFileActions(QAction* open, QAction* save)
{
    if (auto* button = findChild<QToolButton*>(QStringLiteral("controlPanelOpen")))
        button->setDefaultAction(open);
    if (auto* button = findChild<QToolButton*>(QStringLiteral("controlPanelSave")))
        button->setDefaultAction(save);
}

void DashboardPage::setQuickActions(
    QAction* check, QAction* run, QAction* sweep, QAction* stop)
{
    if (auto* button = findChild<QToolButton*>(QStringLiteral("controlPanelCheck")))
        button->setDefaultAction(check);
    if (auto* button = findChild<QToolButton*>(QStringLiteral("controlPanelRun")))
        button->setDefaultAction(run);
    if (auto* button = findChild<QToolButton*>(
            QStringLiteral("controlPanelFrequencySweep")))
        button->setDefaultAction(sweep);
    if (auto* button = findChild<QToolButton*>(QStringLiteral("controlPanelStop")))
        button->setDefaultAction(stop);
}

void DashboardPage::setDocumentState(const QString& fileName, bool modified)
{
    fileName_ = fileName.isEmpty() ? tr("Untitled model.nec") : fileName;
    modified_ = modified;
    refreshHeader();
}

void DashboardPage::setModel(const model::AntennaModel& model,
    const model::ModelSetup& setup, const nec::NecDocument& document,
    const QString& solver, bool checked, std::size_t errors, std::size_t warnings)
{
    const auto summary = model::summarizeModel(model, setup, document);
    backend_ = solver;
    if (!checked) modelStatus_ = tr("Check required");
    else if (errors != 0) modelStatus_ = tr("%1 error(s)").arg(errors);
    else if (warnings != 0) modelStatus_ = tr("%1 warning(s)").arg(warnings);
    else modelStatus_ = tr("Model ready");
    refreshHeader();

    setModelRow(Editor::Frequency, summary.frequency
        ? summary.frequency->pointCount > 1
            ? tr("%1–%2 MHz · %3 pts")
                  .arg(formatDecimal(summary.frequency->startMHz),
                      formatDecimal(summary.frequency->endMHz))
                  .arg(summary.frequency->pointCount)
            : tr("%1 MHz").arg(formatDecimal(summary.frequency->startMHz))
        : tr("not configured"));
    setModelRow(Editor::Wires, tr("%1 · %2")
        .arg(countText(summary.wireCount, tr("wire"), tr("wires")),
            countText(summary.segmentCount, tr("segment"), tr("segments"))));
    setModelRow(Editor::Sources, summary.sources.empty() ? tr("none")
        : summary.sources.size() == 1
            ? tr("1 source · wire %1, seg %2")
                  .arg(summary.sources[0].wireTag).arg(summary.sources[0].segment)
            : countText(summary.sources.size(), tr("source"), tr("sources")));
    setModelRow(Editor::Loads, countText(summary.loadCount, tr("load"), tr("loads")));
    setModelRow(Editor::TransmissionLines, countText(summary.transmissionLineCount,
        tr("line"), tr("lines")));
    setModelRow(Editor::Ground, groundName(summary.groundType));
    setModelRow(Editor::Variables, summary.symbolCount == 0 ? tr("none (SY)")
        : countText(summary.symbolCount, tr("variable (SY)"), tr("variables (SY)")));
    setModelRow(Editor::Requests, summary.radiationRequests.empty()
        ? summary.executionRequestCount == 0 ? tr("none") : tr("execution only")
        : summary.radiationRequests.size() == 1
            ? tr("pattern %1×%2").arg(summary.radiationRequests[0].thetaCount)
                  .arg(summary.radiationRequests[0].phiCount)
            : countText(summary.radiationRequests.size(), tr("pattern"), tr("patterns")));
    setModelRow(Editor::Deck, countText(summary.cardCount, tr("card"), tr("cards")));
    modelRows_->setEnabled(summary.cardCount != 0);
    geometryViewButton_->setEnabled(!model.empty());
    refreshGeometryViewButton();
}

void DashboardPage::setResults(const analysis::AnalysisResult& result,
    const QString& context, bool stale)
{
    resultContext_ = context;
    resultsStale_ = stale;
    impedanceAvailable_ = !result.feedpoints.empty();
    currentsAvailable_ = !result.currents.empty();
    radiationAvailable_ = !result.radiation.empty();
    resultStatus_ = !impedanceAvailable_ && !currentsAvailable_ && !radiationAvailable_
        ? tr("Results none") : stale ? tr("Results stale") : tr("Results current");
    const auto selectedFrequency = !result.feedpoints.empty() ? result.feedpoints.front().frequencyMHz
        : !result.radiation.empty() ? result.radiation.front().frequencyMHz : 0.0;
    const auto summary = analysis::summarizeResults(result, selectedFrequency);
    QStringList lines;
    if (summary.selectedFeedpoint) {
        const auto& feedpoint = *summary.selectedFeedpoint;
        lines.append(tr("%1 MHz · W%2:S%3 · Z = %4 %5 j%6 Ω · SWR %7")
            .arg(formatDecimal(feedpoint.frequencyMHz))
            .arg(feedpoint.source.wireTag).arg(feedpoint.source.segment)
            .arg(formatDecimal(feedpoint.impedance.real()))
            .arg(feedpoint.impedance.imag() < 0.0 ? QStringLiteral("−") : QStringLiteral("+"))
            .arg(formatDecimal(std::abs(feedpoint.impedance.imag())))
            .arg(std::isfinite(feedpoint.swr) ? formatDecimal(feedpoint.swr) : tr("∞")));
    }
    QStringList extrema;
    if (summary.minimumSwr) {
        extrema.append(tr("min SWR %1 @ %2 MHz")
            .arg(formatDecimal(summary.minimumSwr->swr),
                formatDecimal(summary.minimumSwr->frequencyMHz)));
    }
    if (summary.peakGain) {
        extrema.append(tr("peak gain %1 dBi @ %2 MHz (%3)")
            .arg(formatDecimal(summary.peakGain->gainDb),
                formatDecimal(summary.peakGain->frequencyMHz),
                datasetName(summary.peakGain->datasetKind)));
    }
    if (!extrema.empty()) lines.append(extrema.join(QStringLiteral(" · ")));
    latestResult_->setText(lines.empty() ? tr("No supported analysis results")
                                         : lines.join(QLatin1Char('\n')));
    latestResult_->setToolTip(context);
    refreshHeader();
    refreshFooter();
    refreshResultButton(Workspace::Impedance);
    refreshResultButton(Workspace::Currents);
    refreshResultButton(Workspace::Radiation);
}

void DashboardPage::markResultsStale()
{
    if (!impedanceAvailable_ && !currentsAvailable_ && !radiationAvailable_) return;
    resultsStale_ = true;
    resultStatus_ = tr("Results stale");
    refreshHeader();
    refreshFooter();
}

void DashboardPage::setRecentRuns(const std::vector<AnalysisRunRecord>& runs)
{
    const auto found = std::ranges::find_if(runs, [](const auto& run) {
        return run.status == QStringLiteral("Completed")
            && run.runType != QStringLiteral("optimization-candidate")
            && run.runType != QStringLiteral("convergence-step");
    });
    if (found != runs.end()) {
        lastRunText_ = tr("Last run %1 · %2 s")
            .arg(found->started.toString(QStringLiteral("HH:mm")))
            .arg(formatDecimal(found->durationSeconds));
    }
    refreshFooter();
}

void DashboardPage::setResultWindowVisible(Workspace workspace, bool visible)
{
    switch (workspace) {
    case Workspace::Impedance: impedanceVisible_ = visible; break;
    case Workspace::Currents: currentsVisible_ = visible; break;
    case Workspace::Radiation: radiationVisible_ = visible; break;
    case Workspace::GeometryView: geometryViewVisible_ = visible; break;
    default: return;
    }
    if (workspace == Workspace::GeometryView) refreshGeometryViewButton();
    else refreshResultButton(workspace);
}

void DashboardPage::setOpenRunHistoryCallback(std::function<void()> callback)
{
    openRunHistory_ = std::move(callback);
}

void DashboardPage::setOpenWorkspaceCallback(std::function<void(Workspace)> callback)
{
    openWorkspace_ = std::move(callback);
}

void DashboardPage::setOpenEditorCallback(std::function<void(Editor)> callback)
{
    openEditor_ = std::move(callback);
}

void DashboardPage::refreshHeader()
{
    modelName_->setText(modified_ ? tr("%1 *").arg(fileName_) : fileName_);
    backendState_->setText(backend_.isEmpty() ? tr("Setup solver") : backend_);
    modelState_->setText(modelStatus_);
    resultState_->setText(resultStatus_);
}

void DashboardPage::refreshFooter()
{
    const auto state = resultsStale_ ? tr("model changed · results stale")
        : impedanceAvailable_ || currentsAvailable_ || radiationAvailable_
            ? tr("model unchanged since run") : tr("no current results");
    footer_->setText(tr("%1 · %2").arg(lastRunText_, state));
    footer_->setToolTip(resultContext_);
}

void DashboardPage::refreshResultButton(Workspace workspace)
{
    auto* button = resultButton(workspace);
    if (button == nullptr) return;
    auto available = true;
    auto visible = false;
    QString label;
    switch (workspace) {
    case Workspace::Impedance:
        available = impedanceAvailable_; visible = impedanceVisible_; label = tr("Impedance / SWR"); break;
    case Workspace::Currents:
        available = currentsAvailable_; visible = currentsVisible_; label = tr("Currents"); break;
    case Workspace::Radiation:
        available = radiationAvailable_; visible = radiationVisible_; label = tr("Radiation"); break;
    default: return;
    }
    button->setEnabled(available);
    button->setText(visible ? tr("%1  [open]").arg(label) : label);
    button->setToolTip(available
        ? visible ? tr("This live result window is open; click to raise it")
                  : tr("Open this live result window")
        : tr("Run an analysis that produces these results first"));
}

void DashboardPage::refreshGeometryViewButton()
{
    geometryViewButton_->setText(geometryViewVisible_ ? tr("View [open]") : tr("View"));
}

void DashboardPage::setModelRow(Editor editor, const QString& value)
{
    auto* item = modelRows_->item(static_cast<int>(editor), 1);
    item->setText(value);
    item->setToolTip(value);
}

auto DashboardPage::resultButton(Workspace workspace) const -> QToolButton*
{
    switch (workspace) {
    case Workspace::Impedance: return impedanceButton_;
    case Workspace::Currents: return currentsButton_;
    case Workspace::Radiation: return radiationButton_;
    default: return nullptr;
    }
}

}
