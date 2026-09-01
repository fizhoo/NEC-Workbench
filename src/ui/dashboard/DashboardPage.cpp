#include "ui/dashboard/DashboardPage.h"

#include "ui/DisplayFormat.h"

#include "ui/analysis/FieldResultsViews.h"
#include "ui/editor/NecEditor.h"

#include <QAction>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <ranges>

namespace necwb::ui {
namespace {

auto panel(const QString& title, QWidget* content, QWidget* parent) -> QGroupBox*
{
    auto* group = new QGroupBox(title, parent);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 10, 8, 8);
    layout->addWidget(content);
    return group;
}

auto summaryTable(const QStringList& labels, QWidget* parent) -> QTableWidget*
{
    auto* table = new QTableWidget(labels.size(), 2, parent);
    table->setHorizontalHeaderLabels({QObject::tr("Item"), QObject::tr("Value")});
    table->verticalHeader()->hide();
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

DashboardPage::DashboardPage(QTextDocument* document, QWidget* parent) : QWidget(parent)
{
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* headingPanel = new QWidget(this);
    auto* headingLayout = new QHBoxLayout(headingPanel);
    headingLayout->setContentsMargins(4, 2, 4, 2);
    auto* headingText = new QVBoxLayout;
    auto* heading = new QLabel(tr("Project Dashboard"), headingPanel);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize()+5);
    heading->setFont(headingFont);
    fileState_ = new QLabel(tr("Untitled model · Saved"), headingPanel);
    fileState_->setObjectName(QStringLiteral("dashboardFileState"));
    fileState_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headingText->addWidget(heading);
    headingText->addWidget(fileState_);
    quickActions_ = new QHBoxLayout;
    quickActions_->setSpacing(6);
    headingLayout->addLayout(headingText);
    headingLayout->addStretch();
    headingLayout->addLayout(quickActions_);

    editor_ = new NecEditor(this);
    editor_->setDocument(document);
    editor_->setMinimumSize(360, 240);

    view3D_ = new Radiation3DView(this);
    view3D_->setOverviewMode(true);
    view3D_->setMinimumSize(420, 300);

    auto* modelPanel = new QWidget(this);
    auto* modelLayout = new QVBoxLayout(modelPanel);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelState_ = new QLabel(tr("No model loaded"), modelPanel);
    modelState_->setWordWrap(true);
    modelSummary_ = summaryTable({tr("Wires"), tr("Segments"), tr("Sources"), tr("Loads"),
        tr("TLs"), tr("Solver"), tr("Ground"), tr("Frequency")}, modelPanel);
    modelLayout->addWidget(modelState_);
    modelLayout->addWidget(modelSummary_, 1);
    auto* averageGainHeading = new QLabel(tr("Model Quality"), modelPanel);
    auto averageGainFont = averageGainHeading->font();
    averageGainFont.setBold(true);
    averageGainHeading->setFont(averageGainFont);
    averageGainState_ = new QLabel(tr("AGT not run"), modelPanel);
    averageGainState_->setWordWrap(true);
    averageGainButton_ = new QToolButton(modelPanel);
    averageGainButton_->setText(tr("Run Average Gain Test…"));
    averageGainButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    convergenceState_ = new QLabel(tr("Segmentation convergence not run"), modelPanel);
    convergenceState_->setWordWrap(true);
    convergenceButton_ = new QToolButton(modelPanel);
    convergenceButton_->setText(tr("Segmentation Convergence…"));
    convergenceButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    modelLayout->addWidget(averageGainHeading);
    modelLayout->addWidget(averageGainState_);
    modelLayout->addWidget(averageGainButton_, 0, Qt::AlignLeft);
    modelLayout->addWidget(convergenceState_);
    modelLayout->addWidget(convergenceButton_, 0, Qt::AlignLeft);

    auto* resultPanel = new QWidget(this);
    auto* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultState_ = new QLabel(tr("No analysis results"), resultPanel);
    resultState_->setWordWrap(true);
    quickResults_ = summaryTable({tr("Frequency"), tr("Rin"), tr("Xin"), tr("SWR (50 Ω)"),
        tr("Gain"), tr("Efficiency"), tr("F/B")}, resultPanel);
    resultLayout->addWidget(resultState_);
    resultLayout->addWidget(quickResults_, 1);

    layout->addWidget(headingPanel, 0, 0, 1, 2);
    layout->addWidget(panel(tr("NEC Source"), editor_, this), 1, 0);
    layout->addWidget(panel(tr("3D Overview"), view3D_, this), 1, 1);
    layout->addWidget(panel(tr("Model Summary"), modelPanel, this), 2, 0);
    layout->addWidget(panel(tr("Quick Results"), resultPanel, this), 2, 1);
    layout->setRowStretch(1, 3);
    layout->setRowStretch(2, 2);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
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

void DashboardPage::setQuickActions(QAction* geometry, QAction* source, QAction* check,
    QAction* run, QAction* results)
{
    while (auto* item = quickActions_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (auto* action : {geometry, source, check, run, results}) {
        if (action == nullptr) continue;
        auto* button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        quickActions_->addWidget(button);
    }
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
    if (!hasResults_ || !resultsStale_) view3D_->setModel(model);
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
    view3D_->setResults(result, context);
    for (auto row = 0; row < quickResults_->rowCount(); ++row) setValue(quickResults_, row, QStringLiteral("—"));
    if (!result.feedpoints.empty()) {
        const auto& feedpoint = result.feedpoints.front();
        setValue(quickResults_, 0, tr("%1 MHz").arg(formatDecimal(feedpoint.frequencyMHz)));
        setValue(quickResults_, 1, tr("%1 Ω").arg(formatDecimal(feedpoint.impedance.real())));
        setValue(quickResults_, 2, tr("%1 Ω").arg(formatDecimal(feedpoint.impedance.imag())));
        const auto swr = analysis::standingWaveRatio(feedpoint.impedance);
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
