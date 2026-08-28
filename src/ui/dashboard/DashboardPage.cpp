#include "ui/dashboard/DashboardPage.h"

#include "ui/analysis/FieldResultsViews.h"
#include "ui/editor/NecEditor.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTextDocument>
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

    editor_ = new NecEditor(this);
    editor_->setDocument(document);
    editor_->setMinimumSize(360, 240);

    view3D_ = new Radiation3DView(this);
    view3D_->setMinimumSize(420, 300);

    auto* modelPanel = new QWidget(this);
    auto* modelLayout = new QVBoxLayout(modelPanel);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelState_ = new QLabel(tr("No model loaded"), modelPanel);
    modelState_->setWordWrap(true);
    modelSummary_ = summaryTable({tr("Wires"), tr("Segments"), tr("Sources"), tr("Loads"),
        tr("TLs"), tr("Solver"), tr("Ground")}, modelPanel);
    modelLayout->addWidget(modelState_);
    modelLayout->addWidget(modelSummary_, 1);

    auto* resultPanel = new QWidget(this);
    auto* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultState_ = new QLabel(tr("No analysis results"), resultPanel);
    resultState_->setWordWrap(true);
    quickResults_ = summaryTable({tr("Frequency"), tr("Rin"), tr("Xin"), tr("SWR (50 Ω)"),
        tr("Gain"), tr("Efficiency"), tr("F/B")}, resultPanel);
    resultLayout->addWidget(resultState_);
    resultLayout->addWidget(quickResults_, 1);

    layout->addWidget(panel(tr("NEC Source"), editor_, this), 0, 0);
    layout->addWidget(panel(tr("3D Overview"), view3D_, this), 0, 1);
    layout->addWidget(panel(tr("Model Summary"), modelPanel, this), 1, 0);
    layout->addWidget(panel(tr("Quick Results"), resultPanel, this), 1, 1);
    layout->setRowStretch(0, 3);
    layout->setRowStretch(1, 2);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
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
        setValue(quickResults_, 0, tr("%1 MHz").arg(feedpoint.frequencyMHz, 0, 'g', 10));
        setValue(quickResults_, 1, tr("%1 Ω").arg(feedpoint.impedance.real(), 0, 'g', 8));
        setValue(quickResults_, 2, tr("%1 Ω").arg(feedpoint.impedance.imag(), 0, 'g', 8));
        const auto swr = analysis::standingWaveRatio(feedpoint.impedance);
        setValue(quickResults_, 3, std::isfinite(swr) ? QString::number(swr, 'f', 3) : tr("∞"));
    }
    if (!result.radiation.empty()) {
        const auto peak = std::ranges::max(result.radiation, {}, &analysis::RadiationSample::totalGainDb);
        if (result.feedpoints.empty()) setValue(quickResults_, 0, tr("%1 MHz").arg(peak.frequencyMHz, 0, 'g', 10));
        setValue(quickResults_, 4, tr("%1 dBi").arg(peak.totalGainDb, 0, 'f', 2));
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

}
