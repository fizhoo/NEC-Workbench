#include "ui/analysis/AverageGainResultsView.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

auto environmentName(analysis::AverageGainEnvironment environment) -> QString
{
    return environment == analysis::AverageGainEnvironment::PerfectGround
        ? QObject::tr("Perfect ground · hemisphere · expected AGT 2.0")
        : QObject::tr("Free space · full sphere · expected AGT 1.0");
}

auto classificationName(analysis::AverageGainClassification classification) -> QString
{
    switch (classification) {
    case analysis::AverageGainClassification::Pass: return QObject::tr("Pass");
    case analysis::AverageGainClassification::Usable: return QObject::tr("Usable");
    case analysis::AverageGainClassification::Caution: return QObject::tr("Caution");
    case analysis::AverageGainClassification::Questionable: return QObject::tr("Questionable");
    }
    return QObject::tr("Unknown");
}

}

AverageGainResultsView::AverageGainResultsView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    auto* heading = new QLabel(tr("Average Gain Test"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);
    auto* description = new QLabel(tr(
        "AGT runs one temporary lossless model at one frequency. It does not modify the authored "
        "NEC source. The test is necessary but not sufficient evidence of model adequacy."), this);
    description->setWordWrap(true);
    status_ = new QLabel(tr("No Average Gain Test result is loaded."), this);
    status_->setWordWrap(true);
    details_ = new QTableWidget(8, 2, this);
    details_->setHorizontalHeaderLabels({tr("Item"), tr("Value")});
    details_->verticalHeader()->hide();
    details_->horizontalHeader()->setStretchLastSection(true);
    details_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    details_->setSelectionMode(QAbstractItemView::NoSelection);
    const QStringList labels{tr("Classification"), tr("Average power gain"), tr("Expected gain"),
        tr("Normalized AGT"), tr("Suggested gain adjustment"), tr("Frequency"),
        tr("Test environment"), tr("Solid angle")};
    for (auto row = 0; row < labels.size(); ++row) {
        details_->setItem(row, 0, new QTableWidgetItem(labels[row]));
        details_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("—")));
    }
    details_->resizeColumnToContents(0);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addWidget(status_);
    layout->addWidget(details_, 1);
}

void AverageGainResultsView::clear()
{
    hasResult_ = false;
    status_->setText(tr("No Average Gain Test result is loaded."));
    for (auto row = 0; row < details_->rowCount(); ++row) setValue(row, QStringLiteral("—"));
}

void AverageGainResultsView::setRunning(double frequencyMHz,
    analysis::AverageGainEnvironment environment, const QString& context)
{
    clear();
    status_->setText(tr("Running AGT · %1").arg(context));
    setValue(5, tr("%1 MHz").arg(frequencyMHz, 0, 'g', 12));
    setValue(6, environmentName(environment));
}

void AverageGainResultsView::setResult(const analysis::AverageGainAssessment& assessment,
    double frequencyMHz, analysis::AverageGainEnvironment environment,
    std::optional<double> solidAnglePi, const QString& context)
{
    hasResult_ = true;
    status_->setText(tr("Completed · %1").arg(context));
    setValue(0, classificationName(assessment.classification));
    setValue(1, QString::number(assessment.averagePowerGain, 'g', 10));
    setValue(2, QString::number(assessment.expectedGain, 'g', 6));
    setValue(3, QString::number(assessment.normalizedGain, 'g', 10));
    setValue(4, tr("%1 dB").arg(assessment.gainAdjustmentDb, 0, 'f', 3));
    setValue(5, tr("%1 MHz").arg(frequencyMHz, 0, 'g', 12));
    setValue(6, environmentName(environment));
    setValue(7, solidAnglePi ? tr("%1π steradians").arg(*solidAnglePi, 0, 'g', 8) : tr("Not reported"));
}

void AverageGainResultsView::setFailure(const QString& message, const QString& context)
{
    clear();
    status_->setText(tr("AGT failed · %1 · %2").arg(context, message));
}

void AverageGainResultsView::markStale()
{
    if (hasResult_) status_->setText(tr("STALE — the model changed after this Average Gain Test."));
}

void AverageGainResultsView::setValue(int row, const QString& value)
{
    details_->item(row, 1)->setText(value);
}

}
