#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/DisplayFormat.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace necwb::ui {

namespace {

auto frequencyAt(const model::FrequencyDefinition& frequency, int index) -> double
{
    return frequency.steppingMode == 1
        ? frequency.startMHz * std::pow(frequency.step, index)
        : frequency.startMHz + index * frequency.step;
}

}

void OptimizationWorkspace::updateFrequencyControls()
{
    const auto explicitMode = selectedFrequencyMode() == FrequencyMode::Explicit;
    const auto continuousMode = selectedFrequencyMode() == FrequencyMode::Continuous;
    explicitFrequencyPanel_->setVisible(explicitMode);
    continuousFrequencyPanel_->setVisible(continuousMode);
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::populateModelFrequencies(const model::FrequencyDefinition& frequency)
{
    modelFrequenciesMHz_.clear();
    const auto count = std::max(1, frequency.count);
    modelFrequenciesMHz_.reserve(static_cast<std::size_t>(count));
    for (auto index = 0; index < count; ++index) {
        const auto frequencyMHz = frequencyAt(frequency, index);
        if (std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            modelFrequenciesMHz_.push_back(frequencyMHz);
    }
    if (!modelFrequenciesMHz_.empty()) {
        continuousStartControl_->setValue(modelFrequenciesMHz_.front());
        continuousStopControl_->setValue(modelFrequenciesMHz_.back());
        if (modelFrequenciesMHz_.size() > 1)
            continuousStepControl_->setValue(
                std::abs(modelFrequenciesMHz_[1] - modelFrequenciesMHz_[0]));
    }
    setExplicitFrequencies(modelFrequenciesMHz_);
}

void OptimizationWorkspace::setExplicitFrequencies(
    const std::vector<double>& frequenciesMHz)
{
    const QSignalBlocker blocker(frequencyTable_);
    frequencyTable_->clear();
    for (const auto frequencyMHz : frequenciesMHz) {
        auto* item = new QListWidgetItem(formatDecimal(frequencyMHz), frequencyTable_);
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(tr("%1 MHz").arg(formatDecimal(frequencyMHz)));
    }
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::addExplicitFrequency(double frequencyMHz)
{
    if (!std::isfinite(frequencyMHz) || frequencyMHz <= 0.0) return;
    auto frequencies = explicitFrequencies();
    frequencies.push_back(frequencyMHz);
    setExplicitFrequencies(analysis::frequencyPlanPoints({
        analysis::FrequencyPlanMode::Explicit, std::move(frequencies), {}}));
}

void OptimizationWorkspace::removeSelectedFrequencies()
{
    if (!frequencyTable_->isEnabled()) return;
    std::set<int, std::greater<>> rows;
    for (const auto* item : frequencyTable_->selectedItems())
        rows.insert(frequencyTable_->row(item));
    for (const auto row : rows) delete frequencyTable_->takeItem(row);
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::clearExplicitFrequencies()
{
    if (!frequencyTable_->isEnabled() || frequencyTable_->count() == 0) return;
    QMessageBox confirmation(this);
    confirmation.setWindowTitle(tr("Clear Selected Frequencies"));
    confirmation.setText(tr("Clear every frequency from the editable list?\n"
                            "The model's FR card will not be changed."));
    auto* clearButton = confirmation.addButton(
        tr("Clear All"), QMessageBox::DestructiveRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != clearButton) return;
    frequencyTable_->clear();
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::chooseAmateurBands()
{
    const auto& presets = analysis::amateurBandPresets();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("optimizationAmateurBandDialog"));
    dialog.setWindowTitle(tr("Add Amateur Bands"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(tr(
        "Select one or more engineering ranges. These presets do not define local operating privileges."),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* bandLayout = new QGridLayout;
    std::vector<QCheckBox*> checks;
    checks.reserve(presets.size());
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto& preset = presets[index];
        auto* check = new QCheckBox(tr("%1  (%2–%3 MHz)")
            .arg(QString::fromUtf8(preset.name.data(),
                static_cast<qsizetype>(preset.name.size())))
            .arg(preset.startMHz, 0, 'f', 3)
            .arg(preset.endMHz, 0, 'f', 3), &dialog);
        check->setObjectName(QStringLiteral("optimizationAmateurBandCheck%1").arg(index));
        checks.push_back(check);
        bandLayout->addWidget(check, static_cast<int>(index % 6),
            static_cast<int>(index / 6));
    }
    layout->addLayout(bandLayout);

    auto* summary = new QLabel(&dialog);
    summary->setObjectName(QStringLiteral("optimizationAmateurBandSummary"));
    auto* selectionButtons = new QHBoxLayout;
    auto* selectAll = new QPushButton(tr("Select All"), &dialog);
    auto* clearSelection = new QPushButton(tr("Clear"), &dialog);
    selectionButtons->addWidget(selectAll);
    selectionButtons->addWidget(clearSelection);
    selectionButtons->addStretch();
    selectionButtons->addWidget(summary);
    layout->addLayout(selectionButtons);

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* addSelected = dialogButtons->addButton(
        tr("Add Selected"), QDialogButtonBox::AcceptRole);
    addSelected->setObjectName(QStringLiteral("optimizationAddSelectedBands"));
    layout->addWidget(dialogButtons);

    const auto updateSummary = [&checks, &presets, summary, addSelected] {
        analysis::FrequencyPlan selected{
            .mode = analysis::FrequencyPlanMode::Explicit,
            .pointsMHz = {},
            .ranges = {},
        };
        auto selectedCount = 0;
        for (std::size_t index = 0; index < checks.size(); ++index) {
            if (!checks[index]->isChecked()) continue;
            ++selectedCount;
            selected.ranges.push_back({presets[index].startMHz,
                presets[index].endMHz, presets[index].stepMHz});
        }
        const auto pointCount = analysis::frequencyPlanPoints(selected).size();
        summary->setText(tr("%1 bands · %2 frequency points")
            .arg(selectedCount).arg(pointCount));
        addSelected->setEnabled(selectedCount > 0);
    };
    for (auto* check : checks)
        connect(check, &QCheckBox::toggled, &dialog, updateSummary);
    connect(selectAll, &QPushButton::clicked, &dialog, [&checks] {
        for (auto* check : checks) check->setChecked(true);
    });
    connect(clearSelection, &QPushButton::clicked, &dialog, [&checks] {
        for (auto* check : checks) check->setChecked(false);
    });
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateSummary();
    if (dialog.exec() != QDialog::Accepted) return;

    analysis::FrequencyPlan plan{
        .mode = analysis::FrequencyPlanMode::Explicit,
        .pointsMHz = explicitFrequencies(),
        .ranges = {},
    };
    for (std::size_t index = 0; index < checks.size(); ++index) {
        if (checks[index]->isChecked()) plan.ranges.push_back({presets[index].startMHz,
            presets[index].endMHz, presets[index].stepMHz});
    }
    setExplicitFrequencies(analysis::frequencyPlanPoints(plan));
}

void OptimizationWorkspace::pasteExplicitFrequencies()
{
    bool accepted{};
    const auto text = QInputDialog::getMultiLineText(this, tr("Paste Frequencies"),
        tr("Enter MHz values separated by spaces, commas, semicolons, or new lines:"),
        {}, &accepted);
    if (!accepted) return;
    const auto values = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
        Qt::SkipEmptyParts);
    std::vector<double> frequenciesMHz;
    frequenciesMHz.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values) {
        bool valid{};
        const auto frequencyMHz = value.toDouble(&valid);
        if (valid && std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            frequenciesMHz.push_back(frequencyMHz);
    }
    setExplicitFrequencies(frequenciesMHz);
}

void OptimizationWorkspace::editFrequencySetup()
{
    const auto original = captureFrequencySelection();
    if (frequencyDialog_->exec() != QDialog::Accepted)
        restoreFrequencySelection(original);
    updateWorkload();
    updateReadiness();
}

auto OptimizationWorkspace::selectedFrequencyMode() const -> FrequencyMode
{
    return static_cast<FrequencyMode>(frequencyModeControl_->currentData().toInt());
}

auto OptimizationWorkspace::selectedFrequencyPlan() const -> analysis::FrequencyPlan
{
    switch (selectedFrequencyMode()) {
    case FrequencyMode::Explicit:
        return {analysis::FrequencyPlanMode::Explicit, explicitFrequencies(), {}};
    case FrequencyMode::Continuous:
        return {analysis::FrequencyPlanMode::Explicit, {},
            {{continuousStartControl_->value(), continuousStopControl_->value(),
                continuousStepControl_->value()}}};
    case FrequencyMode::ModelSweep:
        return {analysis::FrequencyPlanMode::ModelSweep, modelFrequenciesMHz_, {}};
    }
    return {};
}

auto OptimizationWorkspace::explicitFrequencies() const -> std::vector<double>
{
    std::vector<double> frequencies;
    frequencies.reserve(static_cast<std::size_t>(frequencyTable_->count()));
    for (auto row = 0; row < frequencyTable_->count(); ++row) {
        const auto* item = frequencyTable_->item(row);
        if (item == nullptr) continue;
        bool valid{};
        const auto frequencyMHz = item->text().toDouble(&valid);
        if (valid && std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            frequencies.push_back(frequencyMHz);
    }
    return analysis::frequencyPlanPoints({
        analysis::FrequencyPlanMode::Explicit, std::move(frequencies), {}});
}

auto OptimizationWorkspace::captureFrequencySelection() const -> FrequencySelectionState
{
    return {
        .mode = selectedFrequencyMode(),
        .explicitFrequenciesMHz = explicitFrequencies(),
        .continuousStartMHz = continuousStartControl_->value(),
        .continuousStopMHz = continuousStopControl_->value(),
        .continuousStepMHz = continuousStepControl_->value(),
    };
}

void OptimizationWorkspace::restoreFrequencySelection(
    const FrequencySelectionState& state)
{
    setExplicitFrequencies(state.explicitFrequenciesMHz);
    continuousStartControl_->setValue(state.continuousStartMHz);
    continuousStopControl_->setValue(state.continuousStopMHz);
    continuousStepControl_->setValue(state.continuousStepMHz);
    const auto modeIndex = frequencyModeControl_->findData(static_cast<int>(state.mode));
    if (modeIndex >= 0) frequencyModeControl_->setCurrentIndex(modeIndex);
    updateFrequencyControls();
    updateWorkload();
    updateReadiness();
}

}
