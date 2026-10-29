#include "ui/controlpanel/ControlPanel.h"

#include "analysis/ResultSummary.h"
#include "analysis/FrequencyComparison.h"
#include "model/ModelSummary.h"
#include "nec/NecDocument.h"
#include "ui/DisplayFormat.h"
#include "ui/WorkbenchIcon.h"

#include <QAction>
#include <QFont>
#include <QComboBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
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

auto compactButton(const QString& text, QWidget* parent,
    const QString& objectName = {}, const char* iconName = nullptr) -> QToolButton*
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setObjectName(objectName);
    if (iconName != nullptr) button->setIcon(workbenchIcon(iconName));
    button->setToolButtonStyle(iconName == nullptr
        ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumHeight(25);
    button->setIconSize({16, 16});
    return button;
}

auto countText(std::size_t count, const QString& singular, const QString& plural) -> QString
{
    return count == 0 ? QObject::tr("none")
        : count == 1 ? QObject::tr("1 %1").arg(singular)
        : QObject::tr("%1 %2").arg(static_cast<qulonglong>(count)).arg(plural);
}

}

ControlPanel::ControlPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 7, 8, 6);
    outer->setSpacing(6);

    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("controlPanel"));
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
    openButton_ = compactButton(tr("Open…"), panel, QStringLiteral("controlPanelOpen"));
    saveButton_ = compactButton(tr("Save"), panel, QStringLiteral("controlPanelSave"));
    runButton_ = compactButton(tr("▶ Run (F5)"), panel, QStringLiteral("controlPanelRun"));
    runButton_->setMinimumHeight(30);
    auto runFont = runButton_->font();
    runFont.setBold(true);
    runButton_->setFont(runFont);
    sweepButton_ = compactButton(tr("Frequency Sweep…"), panel,
        QStringLiteral("controlPanelFrequencySweep"));
    stopButton_ = compactButton(tr("Stop"), panel, QStringLiteral("controlPanelStop"));
    stopButton_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    checkButton_ = compactButton(tr("Check model"), panel,
        QStringLiteral("controlPanelCheck"));
    commands->addWidget(openButton_);
    commands->addWidget(saveButton_);
    commands->addSpacing(3);
    auto* runRow = new QHBoxLayout;
    runRow->setSpacing(4);
    runRow->addWidget(runButton_, 1);
    runRow->addWidget(stopButton_);
    commands->addLayout(runRow);
    commands->addWidget(sweepButton_);
    commands->addWidget(checkButton_);
    auto* resultsHeading = new QLabel(tr("RESULTS  (live windows)"), panel);
    resultsHeading->setObjectName(QStringLiteral("controlPanelResultsHeading"));
    auto resultsFont = resultsHeading->font();
    resultsFont.setPointSize(std::max(7, resultsFont.pointSize() - 1));
    resultsHeading->setFont(resultsFont);
    commands->addSpacing(4);
    commands->addWidget(resultsHeading);
    auto* impedanceButton = compactButton(tr("Impedance / SWR"), panel,
        QStringLiteral("controlPanelImpedance"), "results");
    auto* currentsButton = compactButton(tr("Currents"), panel,
        QStringLiteral("controlPanelCurrents"), "currents");
    auto* radiationButton = compactButton(tr("Radiation"), panel,
        QStringLiteral("controlPanelRadiation"), "radiation-pattern");
    auto* geometryButton = compactButton(tr("Antenna View"), panel,
        QStringLiteral("controlPanelGeometryView"), "3d-view");
    resultLaunchers_ = {{{Workspace::Impedance, impedanceButton, tr("Impedance / SWR")},
        {Workspace::Currents, currentsButton, tr("Currents")},
        {Workspace::Radiation, radiationButton, tr("Radiation")},
        {Workspace::GeometryView, geometryButton, tr("Antenna View")}}};
    for (auto& launcher : resultLaunchers_) {
        commands->addWidget(launcher.button);
        connect(launcher.button, &QToolButton::clicked, this,
            [this, workspace = launcher.workspace] {
                if (openWorkspace_) openWorkspace_(workspace);
            });
    }
    commands->addSpacing(5);
    auto* runs = compactButton(tr("Runs…"), panel,
        QStringLiteral("controlPanelRuns"), "runs");
    auto* optimize = compactButton(tr("Optimize…"), panel,
        QStringLiteral("controlPanelOptimize"), "optimize");
    commands->addWidget(runs);
    commands->addWidget(optimize);
    commands->addStretch();
    connect(runs, &QToolButton::clicked, this, [this] {
        if (openWorkspace_) openWorkspace_(Workspace::Runs);
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
    const std::array labels{
        tr("Frequency"), tr("Wires"), tr("Sources"), tr("Loads"),
        tr("Trans. lines"), tr("Ground"), tr("Variables"), tr("Requests"), tr("Deck")};
    const std::array icons{
        "frequency-sweep", "geometry", "sources", "loads", "transmission-lines",
        "ground", "variables", "requests", "nec-deck"};
    for (auto row = 0; row < static_cast<int>(labels.size()); ++row) {
        auto* label = new QTableWidgetItem(labels[static_cast<std::size_t>(row)]);
        auto font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setIcon(workbenchIcon(icons[static_cast<std::size_t>(row)]));
        modelRows_->setItem(row, 0, label);
        modelRows_->setItem(row, 1, new QTableWidgetItem(tr("—")));
        modelRows_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("›")));
    }
    connect(modelRows_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (openEditor_) openEditor_(static_cast<Editor>(row));
    });
    auto* latestHeading = new QLabel(tr("QUICK RESULTS"), panel);
    latestHeading->setFont(resultsFont);
    auto* frequencyRow = new QHBoxLayout;
    frequencyRow->addWidget(new QLabel(tr("At:"), panel));
    resultFrequency_ = new QComboBox(panel);
    resultFrequency_->setObjectName(QStringLiteral("controlPanelResultFrequency"));
    frequencyRow->addWidget(resultFrequency_, 1);
    resultStale_ = new QLabel(tr("STALE"), panel);
    resultStale_->setObjectName(QStringLiteral("controlPanelResultStale"));
    resultStale_->setStyleSheet(QStringLiteral("color: palette(mid); font-weight: bold;"));
    resultStale_->hide();
    frequencyRow->addWidget(resultStale_);
    quickResults_ = new QWidget(panel);
    quickResults_->setObjectName(QStringLiteral("controlPanelQuickResults"));
    auto* quickGrid = new QGridLayout(quickResults_);
    quickGrid->setContentsMargins(0, 0, 0, 0);
    quickGrid->setHorizontalSpacing(3);
    quickGrid->setVerticalSpacing(1);
    const auto addMetric = [&](int row, int pair, const QString& name,
                               QToolButton*& value, bool radiation,
                               QLabel** labelOutput = nullptr) {
        auto* label = new QLabel(name, quickResults_);
        if (labelOutput != nullptr) *labelOutput = label;
        quickGrid->addWidget(label, row, pair * 2);
        value = compactButton(tr("—"), quickResults_);
        value->setAutoRaise(true);
        value->setStyleSheet(QStringLiteral(
            "QToolButton { border: 1px solid transparent; padding: 1px 3px; text-align: left; }"
            "QToolButton:hover { border-color: palette(mid); background: palette(alternate-base); }"));
        value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        quickGrid->addWidget(value, row, pair * 2 + 1);
        connect(value, &QToolButton::clicked, this, [this, radiation] {
            if (openWorkspace_) openWorkspace_(radiation
                ? Workspace::Radiation : Workspace::Impedance);
        });
    };
    addMetric(0, 0, tr("Rin"), resistanceResult_, false);
    addMetric(0, 1, tr("Xin"), reactanceResult_, false);
    addMetric(1, 0, tr("SWR"), swrResult_, false);
    addMetric(1, 1, tr("Gain"), gainResult_, true, &gainResultLabel_);
    addMetric(2, 0, tr("F/B"), frontToBackResult_, true,
        &frontToBackResultLabel_);
    resultExtrema_ = new QLabel(panel);
    resultExtrema_->setObjectName(QStringLiteral("controlPanelResultExtrema"));
    resultExtrema_->setWordWrap(true);
    auto* qualityHeading = new QLabel(tr("MODEL QUALITY"), panel);
    qualityHeading->setFont(resultsFont);
    auto* qualityGrid = new QGridLayout;
    qualityGrid->setContentsMargins(0, 0, 0, 0);
    qualityGrid->setHorizontalSpacing(4);
    qualityGrid->setVerticalSpacing(2);
    const std::array qualityNames{tr("Avg gain test:"), tr("Convergence:")};
    const std::array stateNames{QStringLiteral("controlPanelAverageGainState"),
        QStringLiteral("controlPanelConvergenceState")};
    const std::array buttonNames{QStringLiteral("controlPanelAverageGainRun"),
        QStringLiteral("controlPanelConvergenceRun")};
    for (auto row = 0; row < 2; ++row) {
        qualityGrid->addWidget(new QLabel(qualityNames[row], panel), row, 0);
        studyStateLabels_[row] = new QLabel(tr("not run"), panel);
        studyStateLabels_[row]->setObjectName(stateNames[row]);
        studyButtons_[row] = compactButton(tr("Run"), panel);
        studyButtons_[row]->setObjectName(buttonNames[row]);
        studyButtons_[row]->setAutoRaise(true);
        qualityGrid->addWidget(studyStateLabels_[row], row, 1);
        qualityGrid->addWidget(studyButtons_[row], row, 2);
    }
    modelArea->addWidget(modelHeading);
    modelArea->addWidget(modelRows_);
    modelArea->addSpacing(3);
    modelArea->addWidget(latestHeading);
    modelArea->addLayout(frequencyRow);
    modelArea->addWidget(quickResults_);
    modelArea->addWidget(resultExtrema_);
    modelArea->addSpacing(2);
    modelArea->addWidget(qualityHeading);
    modelArea->addLayout(qualityGrid);
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
    for (const auto& launcher : resultLaunchers_)
        refreshResultLauncher(launcher.workspace);
    connect(resultFrequency_, &QComboBox::currentIndexChanged,
        this, [this] { refreshQuickResults(); });
}

void ControlPanel::setFileActions(QAction* open, QAction* save)
{
    openButton_->setDefaultAction(open);
    saveButton_->setDefaultAction(save);
    openButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    saveButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void ControlPanel::setQuickActions(
    QAction* check, QAction* run, QAction* sweep, QAction* stop)
{
    const std::array bindings{std::pair{checkButton_, check}, std::pair{runButton_, run},
        std::pair{sweepButton_, sweep}, std::pair{stopButton_, stop}};
    for (const auto& [button, action] : bindings) {
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
}

void ControlPanel::setAverageGainAction(QAction* action)
{
    setStudyAction(Study::AverageGain, action);
}

void ControlPanel::setConvergenceAction(QAction* action)
{
    setStudyAction(Study::Convergence, action);
}

void ControlPanel::setStudyAction(Study study, QAction* action)
{
    auto* button = studyButtons_[static_cast<std::size_t>(study)];
    button->setIcon(action->icon());
    button->setToolTip(action->toolTip());
    button->setEnabled(action->isEnabled());
    connect(button, &QToolButton::clicked, action, &QAction::trigger);
    connect(action, &QAction::changed, button, [button, action] {
        button->setEnabled(action->isEnabled());
    });
    button->setText(tr("Run"));
}

void ControlPanel::setDocumentState(const QString& fileName, bool modified)
{
    fileName_ = fileName.isEmpty() ? tr("Untitled model.nec") : fileName;
    modified_ = modified;
    refreshHeader();
}

void ControlPanel::setModel(const model::AntennaModel& model,
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
    auto* geometry = resultLauncher(Workspace::GeometryView);
    geometry->available = !model.empty();
    refreshResultLauncher(Workspace::GeometryView);
}

void ControlPanel::setResults(const analysis::AnalysisResult& result,
    const QString& context, bool stale)
{
    resultContext_ = context;
    resultsStale_ = stale;
    resultLauncher(Workspace::Impedance)->available = !result.feedpoints.empty();
    resultLauncher(Workspace::Currents)->available = !result.currents.empty();
    resultLauncher(Workspace::Radiation)->available = !result.radiation.empty();
    const auto hasResults = std::ranges::any_of(resultLaunchers_, [](const auto& launcher) {
        return launcher.workspace != Workspace::GeometryView && launcher.available;
    });
    resultStatus_ = !hasResults
        ? tr("Results none") : stale ? tr("Results stale") : tr("Results current");
    std::vector<double> frequencies;
    for (const auto& value : result.feedpoints) frequencies.push_back(value.frequencyMHz);
    for (const auto& value : result.radiation) frequencies.push_back(value.frequencyMHz);
    std::ranges::sort(frequencies);
    frequencies.erase(std::unique(frequencies.begin(), frequencies.end(), [](double left, double right) {
        return nearlyEqual(left, right);
    }), frequencies.end());
    const QSignalBlocker blocker(resultFrequency_);
    resultFrequency_->clear();
    resultSummaries_.clear();
    for (const auto frequency : frequencies) {
        resultFrequency_->addItem(tr("%1 MHz").arg(formatDecimal(frequency)), frequency);
        resultSummaries_.push_back(analysis::summarizeResults(result, frequency));
    }
    if (!resultSummaries_.empty() && resultSummaries_.front().minimumSwr) {
        const auto index = resultFrequency_->findData(
            resultSummaries_.front().minimumSwr->frequencyMHz);
        if (index >= 0) resultFrequency_->setCurrentIndex(index);
    }
    refreshQuickResults();
    refreshHeader();
    refreshFooter();
    for (const auto workspace : {Workspace::Impedance, Workspace::Currents,
             Workspace::Radiation})
        refreshResultLauncher(workspace);
}

void ControlPanel::refreshQuickResults()
{
    const auto index = resultFrequency_->currentIndex();
    const auto summary = index >= 0 && index < static_cast<int>(resultSummaries_.size())
        ? resultSummaries_[static_cast<std::size_t>(index)] : analysis::ResultSummary{};
    const auto set = [](QToolButton* button, const std::optional<double>& value,
                        const QString& suffix = {}) {
        button->setText(value ? formatDecimal(*value) + suffix : QString::fromUtf8("—"));
    };
    if (summary.selectedFeedpoint) {
        set(resistanceResult_, summary.selectedFeedpoint->impedance.real(), tr(" Ω"));
        set(reactanceResult_, summary.selectedFeedpoint->impedance.imag(), tr(" Ω"));
        set(swrResult_, summary.selectedFeedpoint->swr);
    } else {
        set(resistanceResult_, std::nullopt); set(reactanceResult_, std::nullopt);
        set(swrResult_, std::nullopt);
    }
    set(gainResult_, summary.selectedGain
        ? std::optional<double>{summary.selectedGain->gainDb} : std::nullopt, tr(" dBi"));
    set(frontToBackResult_, summary.selectedFrontToBackDb, tr(" dB"));
    const std::array radiationRows{
        std::tuple{gainResultLabel_, gainResult_, summary.selectedGain.has_value()},
        std::tuple{frontToBackResultLabel_, frontToBackResult_,
            summary.selectedFrontToBackDb.has_value()}};
    for (const auto& [label, value, visible] : radiationRows) {
        label->setVisible(visible);
        value->setVisible(visible);
    }
    QStringList extrema;
    if (summary.minimumSwr) extrema.append(tr("Min SWR %1 @ %2 MHz")
        .arg(formatDecimal(summary.minimumSwr->swr), formatDecimal(summary.minimumSwr->frequencyMHz)));
    if (summary.peakGain) extrema.append(tr("Peak gain %1 dBi @ %2 MHz")
        .arg(formatDecimal(summary.peakGain->gainDb), formatDecimal(summary.peakGain->frequencyMHz)));
    resultExtrema_->setText(extrema.join(QStringLiteral(" · ")));
    quickResults_->setEnabled(!resultsStale_);
    resultStale_->setVisible(resultsStale_);
}

void ControlPanel::markResultsStale()
{
    if (std::ranges::none_of(resultLaunchers_, [](const auto& launcher) {
            return launcher.workspace != Workspace::GeometryView && launcher.available;
        })) return;
    resultsStale_ = true;
    resultStatus_ = tr("Results stale");
    refreshHeader();
    refreshFooter();
    refreshQuickResults();
}

void ControlPanel::setStudyStatus(
    Study study, StudyState state, const QString& text, const QString& toolTip)
{
    const auto index = static_cast<std::size_t>(study);
    if (state == StudyState::Stale && studyStates_[index] != StudyState::Complete) return;
    studyStates_[index] = state;
    const auto defaultText = [this, state] {
        switch (state) {
        case StudyState::NotRun: return tr("not run"); case StudyState::Running: return tr("running");
        case StudyState::Complete: return tr("complete"); case StudyState::Failed: return tr("failed");
        case StudyState::Stale: return tr("stale");
        }
        return QString{};
    }();
    studyStateLabels_[index]->setText(text.isEmpty() ? defaultText : text);
    studyStateLabels_[index]->setToolTip(toolTip.isEmpty() ? text : toolTip);
}

void ControlPanel::clearStudy(Study study)
{
    setStudyStatus(study, StudyState::NotRun);
}

void ControlPanel::setRecentRuns(const std::vector<AnalysisRunRecord>& runs)
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

void ControlPanel::setResultWindowVisible(Workspace workspace, bool visible)
{
    auto* launcher = resultLauncher(workspace);
    if (launcher == nullptr) return;
    launcher->visible = visible;
    refreshResultLauncher(workspace);
}

void ControlPanel::setOpenWorkspaceCallback(std::function<void(Workspace)> callback)
{
    openWorkspace_ = std::move(callback);
}

void ControlPanel::setOpenEditorCallback(std::function<void(Editor)> callback)
{
    openEditor_ = std::move(callback);
}

void ControlPanel::refreshHeader()
{
    modelName_->setText(modified_ ? tr("%1 *").arg(fileName_) : fileName_);
    backendState_->setText(backend_.isEmpty() ? tr("Setup solver") : backend_);
    modelState_->setText(modelStatus_);
    resultState_->setText(resultStatus_);
}

void ControlPanel::refreshFooter()
{
    const auto state = resultsStale_ ? tr("model changed · results stale")
        : std::ranges::any_of(resultLaunchers_, [](const auto& launcher) {
              return launcher.workspace != Workspace::GeometryView && launcher.available;
          })
            ? tr("model unchanged since run") : tr("no current results");
    footer_->setText(tr("%1 · %2").arg(lastRunText_, state));
    footer_->setToolTip(resultContext_);
}

void ControlPanel::refreshResultLauncher(Workspace workspace)
{
    auto* launcher = resultLauncher(workspace);
    if (launcher == nullptr) return;
    launcher->button->setEnabled(launcher->available);
    launcher->button->setText(tr("%1 %2").arg(launcher->visible
        ? QStringLiteral("●") : QStringLiteral("○"), launcher->label));
    launcher->button->setToolTip(launcher->available
        ? launcher->visible ? tr("This live window is open; click to raise it")
                  : tr("Open this live result window")
        : workspace == Workspace::GeometryView ? tr("Open a model first")
                                               : tr("Run an analysis that produces these results first"));
}

void ControlPanel::setModelRow(Editor editor, const QString& value)
{
    auto* item = modelRows_->item(static_cast<int>(editor), 1);
    item->setText(value);
    item->setToolTip(value);
}

auto ControlPanel::resultLauncher(Workspace workspace) -> ResultLauncher*
{
    const auto found = std::ranges::find(resultLaunchers_, workspace,
        &ResultLauncher::workspace);
    return found == resultLaunchers_.end() ? nullptr : &*found;
}

}
