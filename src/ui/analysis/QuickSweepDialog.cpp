#include "ui/analysis/QuickSweepDialog.h"

#include "ui/DisplayFormat.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {

auto frequencyEnd(const model::FrequencyDefinition& definition) -> double
{
    if (definition.count <= 1) return definition.startMHz;
    if (definition.steppingMode == 1)
        return definition.startMHz * std::pow(definition.step, definition.count - 1);
    return definition.startMHz + definition.step * (definition.count - 1);
}

}

QuickSweepDialog::QuickSweepDialog(const model::FrequencyDefinition& initial, QWidget* parent,
    bool radiationAvailable)
    : QDialog(parent)
{
    setWindowTitle(tr("Quick Frequency Sweep"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(tr(
        "Run a temporary frequency sweep without changing the authored FR card. "
        "The archived run retains both the original source and generated sweep deck."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout;
    start_ = new QDoubleSpinBox(this);
    start_->setObjectName(QStringLiteral("quickSweepStart"));
    start_->setDecimals(DisplayDecimalPlaces);
    start_->setRange(0.000001, 1.0e9);
    start_->setSuffix(tr(" MHz"));
    stop_ = new QDoubleSpinBox(this);
    stop_->setObjectName(QStringLiteral("quickSweepStop"));
    stop_->setDecimals(DisplayDecimalPlaces);
    stop_->setRange(0.000001, 1.0e9);
    stop_->setSuffix(tr(" MHz"));
    spacing_ = new QComboBox(this);
    spacing_->setObjectName(QStringLiteral("quickSweepSpacing"));
    spacing_->addItem(tr("Linear — fixed step"), 0);
    spacing_->addItem(tr("Logarithmic — fixed point count"), 1);
    step_ = new QDoubleSpinBox(this);
    step_->setObjectName(QStringLiteral("quickSweepStep"));
    step_->setDecimals(6);
    step_->setRange(0.000001, 1.0e9);
    step_->setSuffix(tr(" MHz"));
    points_ = new QSpinBox(this);
    points_->setObjectName(QStringLiteral("quickSweepPoints"));
    points_->setRange(2, 10000);
    radiation_ = new QCheckBox(tr("Include existing RP radiation requests (slower)"), this);
    radiation_->setObjectName(QStringLiteral("quickSweepRadiation"));
    radiation_->setToolTip(tr(
        "When disabled, the generated deck removes RP cards and calculates impedance, SWR, and currents only."));
    radiation_->setEnabled(radiationAvailable);
    if (!radiationAvailable)
        radiation_->setToolTip(tr("The current model has no RP radiation requests to include."));
    form->addRow(tr("Start frequency:"), start_);
    form->addRow(tr("Stop frequency:"), stop_);
    form->addRow(tr("Spacing:"), spacing_);
    form->addRow(tr("Linear step:"), step_);
    form->addRow(tr("Logarithmic points:"), points_);
    form->addRow(QString{}, radiation_);
    layout->addLayout(form);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("quickSweepSummary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Run Quick Sweep"));
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons_);

    const auto initialEnd = frequencyEnd(initial);
    const auto start = std::max(0.000001, initial.startMHz);
    start_->setValue(start);
    stop_->setValue(initial.count > 1 && initialEnd > start
            ? initialEnd : start + std::max(0.1, start * 0.025));
    spacing_->setCurrentIndex(initial.steppingMode == 1 ? 1 : 0);
    step_->setValue(initial.steppingMode == 0 && initial.count > 1 && initial.step > 0.0
            ? initial.step : std::max(0.01, start * 0.001));
    points_->setValue(std::clamp(initial.count, 2, 10000));

    for (auto* control : {start_, stop_, step_})
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] { updateSummary(); });
    connect(points_, &QSpinBox::valueChanged, this, [this] { updateSummary(); });
    connect(spacing_, &QComboBox::currentIndexChanged, this, [this] {
        updateControls();
        updateSummary();
    });
    updateControls();
    updateSummary();
}

auto QuickSweepDialog::frequencyDefinition() const -> model::FrequencyDefinition
{
    return definition_;
}

auto QuickSweepDialog::includeRadiationPatterns() const -> bool
{
    return radiation_->isChecked();
}

void QuickSweepDialog::updateControls()
{
    const auto logarithmic = spacing_->currentData().toInt() == 1;
    step_->setEnabled(!logarithmic);
    points_->setEnabled(logarithmic);
}

void QuickSweepDialog::updateSummary()
{
    const auto start = start_->value();
    const auto stop = stop_->value();
    const auto logarithmic = spacing_->currentData().toInt() == 1;
    auto valid = stop > start;
    auto actualStop = stop;
    definition_ = {};
    definition_.startMHz = start;
    if (logarithmic) {
        definition_.steppingMode = 1;
        definition_.count = points_->value();
        definition_.step = valid
            ? std::pow(stop / start, 1.0 / (definition_.count - 1)) : 0.0;
    } else {
        definition_.steppingMode = 0;
        definition_.step = step_->value();
        definition_.count = valid
            ? static_cast<int>(std::floor((stop - start) / definition_.step + 1.0e-9)) + 1 : 0;
        valid = valid && definition_.count >= 2 && definition_.count <= 10000;
        if (valid) actualStop = start + definition_.step * (definition_.count - 1);
    }
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid);
    if (!valid) {
        summary_->setText(stop <= start
                ? tr("Stop frequency must be greater than start frequency.")
                : tr("Choose a step that produces between 2 and 10,000 points."));
        return;
    }
    summary_->setText(tr("%1 to %2 MHz · %3 points · %4")
        .arg(formatDecimal(start), formatDecimal(actualStop))
        .arg(definition_.count)
        .arg(logarithmic ? tr("logarithmic") : tr("linear")));
}

}
