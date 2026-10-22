#include "ui/dashboard/DashboardPage.h"

#include "ui/DisplayFormat.h"

#include <QAction>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPushButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <utility>

namespace necwb::ui {
namespace {

auto panel(const QString& title, QWidget* content, QWidget* parent) -> QGroupBox*
{
    auto* group = new QGroupBox(title, parent);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(6, 8, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(content);
    return group;
}

auto summaryTable(const QStringList& labels, QWidget* parent) -> QTableWidget*
{
    auto* table = new QTableWidget(labels.size(), 2, parent);
    table->setHorizontalHeaderLabels({QObject::tr("Item"), QObject::tr("Value")});
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(21);
    table->horizontalHeader()->setFixedHeight(23);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    for (auto row = 0; row < labels.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(labels[row]));
        table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("—")));
    }
    table->resizeColumnToContents(0);
    return table;
}

auto groundName(const std::optional<model::GroundDefinition>& ground) -> QString
{
    if (!ground || ground->type == model::GroundType::FreeSpace) return QObject::tr("Free space");
    if (ground->type == model::GroundType::Perfect) return QObject::tr("Perfect ground");
    if (ground->type == model::GroundType::SommerfeldNorton) return QObject::tr("Sommerfeld/Norton");
    return QObject::tr("Reflection approximation");
}

}

DashboardPage::DashboardPage(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);

    auto* headingPanel = new QWidget(this);
    auto* headingLayout = new QHBoxLayout(headingPanel);
    headingLayout->setContentsMargins(2, 0, 2, 0);
    headingLayout->setSpacing(8);
    auto* heading = new QLabel(tr("Project Dashboard"), headingPanel);
    heading->setObjectName(QStringLiteral("dashboardHeading"));
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    heading->setFont(headingFont);
    fileState_ = new QLabel(tr("Untitled model · Saved"), headingPanel);
    fileState_->setObjectName(QStringLiteral("dashboardFileState"));
    fileState_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headingLayout->addWidget(heading);
    headingLayout->addWidget(fileState_, 1);
    headingPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* commandPanel = new QGroupBox(tr("Workbench"), this);
    commandPanel->setObjectName(QStringLiteral("dashboardWorkbenchPanel"));
    auto* commandLayout = new QVBoxLayout(commandPanel);
    commandLayout->setContentsMargins(6, 8, 6, 6);
    commandLayout->setSpacing(4);
    auto* quickActionsWidget = new QWidget(commandPanel);
    quickActionsWidget->setObjectName(QStringLiteral("dashboardQuickActionsContainer"));
    quickActions_ = new QHBoxLayout(quickActionsWidget);
    quickActions_->setContentsMargins(0, 0, 0, 0);
    quickActions_->setSpacing(4);
    commandLayout->addWidget(quickActionsWidget);
    auto* launcherLayout = new QGridLayout;
    launcherLayout->setContentsMargins(0, 0, 0, 0);
    launcherLayout->setHorizontalSpacing(4);
    launcherLayout->setVerticalSpacing(3);
    const std::array launchers{
        std::pair{tr("Model"), Workspace::Model},
        std::pair{tr("Setup"), Workspace::Setup},
        std::pair{tr("Antenna View"), Workspace::Antenna},
        std::pair{tr("Impedance / SWR"), Workspace::Impedance},
        std::pair{tr("Currents"), Workspace::Currents},
        std::pair{tr("Radiation"), Workspace::Radiation},
        std::pair{tr("Runs"), Workspace::Runs},
        std::pair{tr("Optimize"), Workspace::Optimize},
    };
    for (auto index = std::size_t{}; index < launchers.size(); ++index) {
        auto* button = new QToolButton(commandPanel);
        button->setObjectName(QStringLiteral("dashboardWorkspaceLauncher%1").arg(index));
        button->setText(launchers[index].first);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(button, &QToolButton::clicked, this, [this, destination = launchers[index].second] {
            if (openWorkspace_) openWorkspace_(destination);
        });
        launcherLayout->addWidget(button, static_cast<int>(index / 4),
            static_cast<int>(index % 4));
    }
    commandLayout->addLayout(launcherLayout);

    auto* statePanel = new QWidget(this);
    statePanel->setObjectName(QStringLiteral("dashboardStatusFooter"));
    auto* stateLayout = new QGridLayout(statePanel);
    stateLayout->setContentsMargins(4, 0, 4, 0);
    stateLayout->setHorizontalSpacing(12);
    stateLayout->addWidget(new QLabel(tr("Model:"), statePanel), 0, 0);
    stateLayout->addWidget(new QLabel(tr("Results:"), statePanel), 0, 2);

    auto* modelPanel = new QWidget(this);
    auto* modelLayout = new QVBoxLayout(modelPanel);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelState_ = new QLabel(tr("No model loaded"), statePanel);
    modelState_->setObjectName(QStringLiteral("dashboardModelState"));
    modelState_->setWordWrap(true);
    modelSummary_ = summaryTable({tr("Wires"), tr("Segments"), tr("Sources"), tr("Loads"),
        tr("TLs"), tr("Solver"), tr("Ground"), tr("Frequency"), tr("SWR Reference")}, modelPanel);
    modelSummary_->setObjectName(QStringLiteral("dashboardModelSummary"));
    modelLayout->addWidget(modelSummary_, 1);
    auto* averageGainHeading = new QLabel(tr("Model Quality"), modelPanel);
    auto averageGainFont = averageGainHeading->font();
    averageGainFont.setBold(true);
    averageGainHeading->setFont(averageGainFont);
    averageGainState_ = new QLabel(tr("AGT not run"), modelPanel);
    averageGainState_->setObjectName(QStringLiteral("dashboardAverageGainState"));
    averageGainState_->setWordWrap(true);
    averageGainButton_ = new QToolButton(modelPanel);
    averageGainButton_->setObjectName(QStringLiteral("dashboardAverageGainButton"));
    averageGainButton_->setText(tr("Run Average Gain Test…"));
    averageGainButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    convergenceState_ = new QLabel(tr("Segmentation convergence not run"), modelPanel);
    convergenceState_->setObjectName(QStringLiteral("dashboardConvergenceState"));
    convergenceState_->setWordWrap(true);
    convergenceButton_ = new QToolButton(modelPanel);
    convergenceButton_->setObjectName(QStringLiteral("dashboardConvergenceButton"));
    convergenceButton_->setText(tr("Segmentation Convergence…"));
    convergenceButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* qualityLayout = new QGridLayout;
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setColumnStretch(0, 1);
    qualityLayout->setColumnStretch(1, 1);
    qualityLayout->addWidget(averageGainHeading, 0, 0, 1, 2);
    qualityLayout->addWidget(averageGainState_, 1, 0);
    qualityLayout->addWidget(convergenceState_, 1, 1);
    qualityLayout->addWidget(averageGainButton_, 2, 0, Qt::AlignLeft);
    qualityLayout->addWidget(convergenceButton_, 2, 1, Qt::AlignLeft);
    modelLayout->addLayout(qualityLayout);

    auto* resultPanel = new QWidget(this);
    auto* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultState_ = new QLabel(tr("No analysis results"), statePanel);
    resultState_->setObjectName(QStringLiteral("dashboardResultState"));
    resultState_->setWordWrap(true);
    quickResults_ = summaryTable({tr("Frequency"), tr("Rin"), tr("Xin"), tr("SWR (50 Ω)"),
        tr("Gain"), tr("Efficiency"), tr("F/B")}, resultPanel);
    quickResults_->setObjectName(QStringLiteral("dashboardQuickResults"));
    resultLayout->addWidget(quickResults_, 1);

    auto* recentRunsPanel = new QWidget(this);
    auto* recentRunsLayout = new QVBoxLayout(recentRunsPanel);
    recentRunsLayout->setContentsMargins(0, 0, 0, 0);
    recentRunsLayout->setSpacing(4);
    recentRuns_ = new QTableWidget(0, 4, recentRunsPanel);
    recentRuns_->setObjectName(QStringLiteral("dashboardRecentRuns"));
    recentRuns_->setHorizontalHeaderLabels(
        {tr("Started"), tr("Model"), tr("Status"), tr("Note / Result")});
    recentRuns_->verticalHeader()->hide();
    recentRuns_->verticalHeader()->setDefaultSectionSize(22);
    recentRuns_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recentRuns_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recentRuns_->setSelectionMode(QAbstractItemView::SingleSelection);
    recentRuns_->setAlternatingRowColors(true);
    recentRuns_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    recentRuns_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    recentRuns_->setMinimumHeight(92);
    recentRuns_->setMaximumHeight(148);
    auto* runButtons = new QHBoxLayout;
    runButtons->setContentsMargins(0, 0, 0, 0);
    runButtons->addStretch();
    auto* openHistory = new QPushButton(tr("Open Run History…"), recentRunsPanel);
    openHistory->setObjectName(QStringLiteral("dashboardOpenRunHistoryButton"));
    runButtons->addWidget(openHistory);
    recentRunsLayout->addWidget(recentRuns_);
    recentRunsLayout->addLayout(runButtons);
    connect(recentRuns_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto* item = recentRuns_->item(row, 0);
        if (item != nullptr && openRecentRun_) openRecentRun_(item->data(Qt::UserRole).toString());
    });
    connect(openHistory, &QPushButton::clicked, this, [this] {
        if (openRunHistory_) openRunHistory_();
    });

    stateLayout->addWidget(modelState_, 0, 1);
    stateLayout->addWidget(resultState_, 0, 3);
    stateLayout->setColumnStretch(1, 1);
    stateLayout->setColumnStretch(3, 1);
    auto* summaries = new QWidget(this);
    auto* summariesLayout = new QHBoxLayout(summaries);
    summariesLayout->setContentsMargins(0, 0, 0, 0);
    summariesLayout->setSpacing(5);
    summariesLayout->addWidget(panel(tr("Model Summary"), modelPanel, summaries), 1);
    summariesLayout->addWidget(panel(tr("Quick Results"), resultPanel, summaries), 1);
    layout->addWidget(headingPanel);
    layout->addWidget(commandPanel);
    layout->addWidget(summaries, 1);
    layout->addWidget(panel(tr("Recent Runs"), recentRunsPanel, this));
    layout->addWidget(statePanel);
}

void DashboardPage::setRecentRuns(const std::vector<AnalysisRunRecord>& runs)
{
    recentRuns_->setRowCount(0);
    for (const auto& run : runs) {
        if (recentRuns_->rowCount() == 5) break;
        if (run.runType == QStringLiteral("optimization-candidate")
            || run.runType == QStringLiteral("convergence-step")) continue;
        const auto row = recentRuns_->rowCount();
        recentRuns_->insertRow(row);
        const QStringList values{
            run.started.toString(QStringLiteral("MM-dd HH:mm")),
            run.sourceFile.isEmpty() ? tr("Archived model.nec")
                                     : QFileInfo(run.sourceFile).fileName(),
            run.status,
            run.userNote.trimmed().isEmpty()
                ? (run.summary.trimmed().isEmpty() ? tr("—") : run.summary.simplified())
                : run.userNote.simplified(),
        };
        for (auto column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setData(Qt::UserRole, run.id);
            if (column == 3) item->setToolTip(values[column]);
            recentRuns_->setItem(row, column, item);
        }
    }
}

void DashboardPage::setOpenRecentRunCallback(
    std::function<void(const QString&)> callback)
{
    openRecentRun_ = std::move(callback);
}

void DashboardPage::setOpenRunHistoryCallback(std::function<void()> callback)
{
    openRunHistory_ = std::move(callback);
}

void DashboardPage::setOpenWorkspaceCallback(std::function<void(Workspace)> callback)
{
    openWorkspace_ = std::move(callback);
}

void DashboardPage::setAverageGainAction(QAction* action)
{
    averageGainButton_->setDefaultAction(action);
    averageGainButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void DashboardPage::setConvergenceAction(QAction* action)
{
    convergenceButton_->setDefaultAction(action);
    convergenceButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void DashboardPage::setQuickActions(QAction* check, QAction* run)
{
    while (auto* item = quickActions_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (auto* action : {check, run}) {
        if (action == nullptr) continue;
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("dashboardQuickAction"));
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        quickActions_->addWidget(button);
    }
    quickActions_->addStretch();
}

void DashboardPage::setDocumentState(const QString& fileName, bool modified)
{
    fileState_->setText(tr("%1 · %2").arg(fileName.isEmpty() ? tr("Untitled model.nec") : fileName,
        modified ? tr("Unsaved changes") : tr("Saved")));
}

void DashboardPage::setValue(QTableWidget* table, int row, const QString& value)
{
    table->item(row, 1)->setText(value);
}

void DashboardPage::setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
    const QString& solver, bool checked, std::size_t errors, std::size_t warnings)
{
    auto segments = 0;
    for (const auto& wire : model.wires()) segments += wire.segments;
    setValue(modelSummary_, 0, QString::number(model.wireCount()));
    setValue(modelSummary_, 1, QString::number(segments));
    setValue(modelSummary_, 2, QString::number(setup.excitations.size()));
    setValue(modelSummary_, 3, QString::number(setup.loads.size()));
    setValue(modelSummary_, 4, QString::number(setup.transmissionLines.size()));
    setValue(modelSummary_, 5, solver);
    setValue(modelSummary_, 6, groundName(setup.ground));
    setValue(modelSummary_, 7, setup.frequency
        ? (setup.frequency->count > 1
            ? tr("%1–%2 MHz (%3 points)")
                  .arg(formatDecimal(setup.frequency->startMHz))
                  .arg(formatDecimal(model::frequencyEndMHz(*setup.frequency)))
                  .arg(setup.frequency->count)
            : tr("%1 MHz").arg(formatDecimal(setup.frequency->startMHz)))
        : tr("Not configured"));
    setValue(modelSummary_, 8, tr("%1 Ω").arg(formatDecimal(model::referenceImpedanceOhms(setup))));
    modelState_->setText(!checked ? tr("Model changed — validation required")
        : errors > 0 ? tr("Invalid model — %1 error(s), %2 warning(s)").arg(errors).arg(warnings)
        : tr("Ready — %1 warning(s)").arg(warnings));
}

void DashboardPage::setResults(const analysis::AnalysisResult& result,
    const QString& context, bool stale)
{
    hasResults_ = !result.feedpoints.empty() || !result.currents.empty() || !result.radiation.empty();
    resultsStale_ = stale;
    resultContext_ = context;
    quickResults_->item(3, 0)->setText(
        tr("SWR (%1 Ω)").arg(formatDecimal(result.referenceImpedanceOhms)));
    for (auto row = 0; row < quickResults_->rowCount(); ++row) setValue(quickResults_, row, QStringLiteral("—"));
    if (!result.feedpoints.empty()) {
        const auto& feedpoint = result.feedpoints.front();
        setValue(quickResults_, 0, tr("%1 MHz").arg(formatDecimal(feedpoint.frequencyMHz)));
        setValue(quickResults_, 1, tr("%1 Ω").arg(formatDecimal(feedpoint.impedance.real())));
        setValue(quickResults_, 2, tr("%1 Ω").arg(formatDecimal(feedpoint.impedance.imag())));
        const auto swr = analysis::standingWaveRatio(
            feedpoint.impedance, result.referenceImpedanceOhms);
        setValue(quickResults_, 3, std::isfinite(swr) ? formatDecimal(swr) : tr("∞"));
    }
    if (!result.radiation.empty()) {
        const auto peak = std::ranges::max(result.radiation, {}, &analysis::RadiationSample::totalGainDb);
        if (result.feedpoints.empty())
            setValue(quickResults_, 0, tr("%1 MHz").arg(formatDecimal(peak.frequencyMHz)));
        setValue(quickResults_, 4, tr("%1 dBi").arg(formatDecimal(peak.totalGainDb)));
    }
    resultState_->setText(!hasResults_ ? tr("No supported results in %1").arg(context)
        : stale ? tr("STALE — model changed after %1").arg(context)
        : tr("Current — %1").arg(context));
}

void DashboardPage::markResultsStale()
{
    if (hasResults_) {
        resultsStale_ = true;
        resultState_->setText(tr("STALE — model changed after %1").arg(resultContext_));
    }
}

void DashboardPage::setAverageGainRunning(double frequencyMHz)
{
    averageGainState_->setText(tr("AGT running at %1 MHz…").arg(formatDecimal(frequencyMHz)));
}

void DashboardPage::setAverageGainResult(
    const analysis::AverageGainAssessment& assessment, double frequencyMHz)
{
    hasAverageGainResult_ = true;
    QString classification;
    switch (assessment.classification) {
    case analysis::AverageGainClassification::Pass: classification = tr("Pass"); break;
    case analysis::AverageGainClassification::Usable: classification = tr("Usable"); break;
    case analysis::AverageGainClassification::Caution: classification = tr("Caution"); break;
    case analysis::AverageGainClassification::Questionable: classification = tr("Questionable"); break;
    }
    averageGainState_->setText(tr("AGT %1 · %2 at %3 MHz")
        .arg(formatDecimal(assessment.normalizedGain))
        .arg(classification)
        .arg(formatDecimal(frequencyMHz)));
}

void DashboardPage::setAverageGainFailure(const QString& message)
{
    hasAverageGainResult_ = false;
    averageGainState_->setText(tr("AGT failed · %1").arg(message));
}

void DashboardPage::markAverageGainStale()
{
    if (hasAverageGainResult_) averageGainState_->setText(tr("AGT stale · model changed after the test"));
}

void DashboardPage::clearAverageGain()
{
    hasAverageGainResult_ = false;
    averageGainState_->setText(tr("AGT not run"));
}

void DashboardPage::setConvergenceState(const QString& summary)
{
    hasConvergenceResult_ = true;
    convergenceState_->setText(summary);
}

void DashboardPage::markConvergenceStale()
{
    if (hasConvergenceResult_)
        convergenceState_->setText(tr("Segmentation convergence stale · model changed after the study"));
}

void DashboardPage::clearConvergence()
{
    hasConvergenceResult_ = false;
    convergenceState_->setText(tr("Segmentation convergence not run"));
}

}
