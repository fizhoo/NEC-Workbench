#include "ui/analysis/ImpedanceResultsView.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace necwb::ui {
namespace {

constexpr auto FrequencyRole = Qt::UserRole;

auto sameFrequency(double first, double second) -> bool
{
    return std::abs(first - second) <= 1.0e-9 * std::max({1.0, std::abs(first), std::abs(second)});
}

}

ImpedanceResultsView::ImpedanceResultsView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    auto* heading = new QLabel(tr("Feedpoint Impedance"), this);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 3);
    heading->setFont(font);
    summary_ = new QLabel(tr("Run an analysis to populate impedance results."), this);
    summary_->setWordWrap(true);
    table_ = new QTableWidget(0, 9, this);
    table_->setHorizontalHeaderLabels({tr("Frequency (MHz)"), tr("Wire"), tr("Segment"),
        tr("R (Ω)"), tr("X (Ω)"), tr("|Z| (Ω)"), tr("Phase (°)"), tr("SWR (50 Ω)"),
        tr("Power (W)")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(heading);
    layout->addWidget(summary_);
    layout->addWidget(table_, 1);
}

void ImpedanceResultsView::setResults(const analysis::AnalysisResult& result,
    const QString& runDirectory)
{
    table_->setRowCount(0);
    for (const auto& feedpoint : result.feedpoints) {
        const auto row = table_->rowCount();
        table_->insertRow(row);
        const auto swr = analysis::standingWaveRatio(feedpoint.impedance);
        const QStringList values{
            QString::number(feedpoint.frequencyMHz, 'g', 10),
            QString::number(feedpoint.wireTag),
            QString::number(feedpoint.segment),
            QString::number(feedpoint.impedance.real(), 'g', 10),
            QString::number(feedpoint.impedance.imag(), 'g', 10),
            QString::number(std::abs(feedpoint.impedance), 'g', 10),
            QString::number(std::arg(feedpoint.impedance) * 180.0 / std::acos(-1.0), 'g', 8),
            std::isfinite(swr) ? QString::number(swr, 'f', 3) : tr("∞"),
            QString::number(feedpoint.inputPowerWatts, 'g', 10),
        };
        for (auto column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            item->setData(FrequencyRole, feedpoint.frequencyMHz);
            table_->setItem(row, column, item);
        }
    }
    summary_->setText(result.feedpoints.empty()
            ? tr("No supported antenna-input rows were found in %1.").arg(runDirectory)
            : tr("%1 feedpoint result(s) parsed from %2. SWR uses a 50 Ω reference.")
                .arg(result.feedpoints.size()).arg(runDirectory));
    table_->resizeColumnsToContents();
}

void ImpedanceResultsView::setSelectedFrequency(double frequencyMHz)
{
    table_->clearSelection();
    for (auto row = 0; row < table_->rowCount(); ++row) {
        const auto* item = table_->item(row, 0);
        if (item != nullptr && sameFrequency(item->data(FrequencyRole).toDouble(), frequencyMHz)) {
            table_->selectRow(row);
            table_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

}
