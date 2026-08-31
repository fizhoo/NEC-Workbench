#include "ui/setup/SetupEditor.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {

constexpr auto SourceLineRole = Qt::UserRole;

auto createDecimalControl(QWidget* parent) -> QDoubleSpinBox*
{
    auto* control = new QDoubleSpinBox(parent);
    control->setDecimals(9);
    control->setRange(-1.0e12, 1.0e12);
    control->setKeyboardTracking(false);
    return control;
}

}

SetupEditor::SetupEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* heading = new QLabel(tr("Model Setup"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    heading->setFont(headingFont);
    auto* description = new QLabel(
        tr("Configure frequency, ground, and voltage sources without editing raw NEC cards."), this);
    description->setWordWrap(true);

    auto* frequencyGroup = new QGroupBox(tr("Frequency (FR)"), this);
    auto* frequencyLayout = new QFormLayout(frequencyGroup);
    frequencySweepControl_ = new QCheckBox(tr("Enable frequency sweep"), frequencyGroup);
    frequencyModeControl_ = new QComboBox(frequencyGroup);
    frequencyModeControl_->addItem(tr("Linear / additive"), 0);
    frequencyModeControl_->addItem(tr("Multiplicative"), 1);
    startFrequencyControl_ = createDecimalControl(frequencyGroup);
    startFrequencyControl_->setRange(0.000001, 1.0e12);
    startFrequencyControl_->setSuffix(tr(" MHz"));
    endFrequencyControl_ = createDecimalControl(frequencyGroup);
    endFrequencyControl_->setRange(0.000001, 1.0e12);
    endFrequencyControl_->setSuffix(tr(" MHz"));
    frequencyStepControl_ = createDecimalControl(frequencyGroup);
    frequencyStepControl_->setRange(0.0, 1.0e12);
    startFrequencyLabel_ = new QLabel(tr("Frequency"), frequencyGroup);
    endFrequencyLabel_ = new QLabel(tr("End frequency"), frequencyGroup);
    frequencyModeLabel_ = new QLabel(tr("Sweep spacing"), frequencyGroup);
    frequencyStepLabel_ = new QLabel(tr("Step"), frequencyGroup);
    frequencySummaryTitleLabel_ = new QLabel(tr("Sweep summary"), frequencyGroup);
    frequencySummaryLabel_ = new QLabel(frequencyGroup);
    frequencySummaryLabel_->setWordWrap(true);
    frequencyValidationLabel_ = new QLabel(frequencyGroup);
    frequencyValidationLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
    frequencyValidationLabel_->setWordWrap(true);
    frequencyLayout->addRow({}, frequencySweepControl_);
    frequencyLayout->addRow(startFrequencyLabel_, startFrequencyControl_);
    frequencyLayout->addRow(endFrequencyLabel_, endFrequencyControl_);
    frequencyLayout->addRow(frequencyModeLabel_, frequencyModeControl_);
    frequencyLayout->addRow(frequencyStepLabel_, frequencyStepControl_);
    frequencyLayout->addRow(frequencySummaryTitleLabel_, frequencySummaryLabel_);
    frequencyLayout->addRow({}, frequencyValidationLabel_);
    auto* frequencyButtons = new QHBoxLayout;
    auto* applyFrequencyButton = new QPushButton(tr("Apply Frequency"), frequencyGroup);
    removeFrequencyButton_ = new QPushButton(tr("Remove FR Card"), frequencyGroup);
    frequencyButtons->addWidget(applyFrequencyButton);
    frequencyButtons->addWidget(removeFrequencyButton_);
    frequencyButtons->addStretch();
    frequencyLayout->addRow(frequencyButtons);

    auto* groundGroup = new QGroupBox(tr("Ground Environment (GN / GE)"), this);
    auto* groundLayout = new QFormLayout(groundGroup);
    groundTypeControl_ = new QComboBox(groundGroup);
    groundTypeControl_->addItem(tr("Free space / no ground"), static_cast<int>(model::GroundType::FreeSpace));
    groundTypeControl_->addItem(tr("Perfect conducting ground"), static_cast<int>(model::GroundType::Perfect));
    groundTypeControl_->addItem(tr("Real ground — reflection approximation"),
        static_cast<int>(model::GroundType::ReflectionApproximation));
    groundTypeControl_->addItem(tr("Real ground — Sommerfeld/Norton"),
        static_cast<int>(model::GroundType::SommerfeldNorton));
    groundPresetControl_ = new QComboBox(groundGroup);
    groundPresetControl_->addItem(tr("Custom"), 0);
    groundPresetControl_->addItem(tr("Average ground (εr 13, 0.005 S/m)"), 1);
    relativePermittivityControl_ = createDecimalControl(groundGroup);
    relativePermittivityControl_->setRange(0.000001, 1.0e9);
    conductivityControl_ = createDecimalControl(groundGroup);
    conductivityControl_->setRange(0.0, 1.0e9);
    conductivityControl_->setSuffix(tr(" S/m"));
    connectGroundEndsControl_ = new QCheckBox(tr("Connect wire ends that terminate on Z = 0"), groundGroup);
    auto* applyGroundButton = new QPushButton(tr("Apply Ground"), groundGroup);
    groundLayout->addRow(tr("Environment"), groundTypeControl_);
    groundLayout->addRow(tr("Material preset"), groundPresetControl_);
    groundLayout->addRow(tr("Relative permittivity"), relativePermittivityControl_);
    groundLayout->addRow(tr("Conductivity"), conductivityControl_);
    groundLayout->addRow({}, connectGroundEndsControl_);
    groundLayout->addRow(applyGroundButton);

    auto* excitationGroup = new QGroupBox(tr("Voltage Sources (EX 0)"), this);
    auto* excitationLayout = new QVBoxLayout(excitationGroup);
    excitationTable_ = new QTableWidget(excitationGroup);
    excitationTable_->setColumnCount(4);
    excitationTable_->setHorizontalHeaderLabels({tr("Wire"), tr("Segment"), tr("Magnitude"), tr("Phase (deg)")});
    excitationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    excitationTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    excitationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    excitationTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    excitationTable_->verticalHeader()->setVisible(false);

    auto* excitationForm = new QFormLayout;
    wireControl_ = new QComboBox(excitationGroup);
    segmentControl_ = new QSpinBox(excitationGroup);
    segmentControl_->setRange(1, 1);
    magnitudeControl_ = createDecimalControl(excitationGroup);
    magnitudeControl_->setRange(0.0, 1.0e12);
    phaseControl_ = createDecimalControl(excitationGroup);
    phaseControl_->setRange(-360.0, 360.0);
    phaseControl_->setSuffix(tr("°"));
    excitationForm->addRow(tr("Wire tag"), wireControl_);
    excitationForm->addRow(tr("Segment"), segmentControl_);
    excitationForm->addRow(tr("Magnitude"), magnitudeControl_);
    excitationForm->addRow(tr("Phase"), phaseControl_);

    auto* excitationButtons = new QHBoxLayout;
    addExcitationButton_ = new QPushButton(tr("Add Source"), excitationGroup);
    updateExcitationButton_ = new QPushButton(tr("Update Selected"), excitationGroup);
    deleteExcitationButton_ = new QPushButton(tr("Delete Selected"), excitationGroup);
    excitationButtons->addWidget(addExcitationButton_);
    excitationButtons->addWidget(updateExcitationButton_);
    excitationButtons->addWidget(deleteExcitationButton_);
    excitationButtons->addStretch();

    excitationLayout->addWidget(excitationTable_);
    excitationLayout->addLayout(excitationForm);
    excitationLayout->addLayout(excitationButtons);

    layout->addWidget(heading);
    layout->addWidget(description);
    auto* compactSetup = new QSplitter(Qt::Horizontal, this);
    compactSetup->setObjectName(QStringLiteral("modelSetupCompactSplitter"));
    compactSetup->setChildrenCollapsible(false);
    compactSetup->addWidget(frequencyGroup);
    compactSetup->addWidget(groundGroup);
    compactSetup->setStretchFactor(0, 1);
    compactSetup->setStretchFactor(1, 1);
    layout->addWidget(compactSetup);
    layout->addWidget(excitationGroup, 1);

    connect(frequencySweepControl_, &QCheckBox::toggled, this, [this] { updateFrequencyControls(); });
    connect(frequencyModeControl_, &QComboBox::currentIndexChanged, this, [this] { updateFrequencyControls(); });
    connect(startFrequencyControl_, &QDoubleSpinBox::valueChanged, this, [this] { updateFrequencyControls(); });
    connect(endFrequencyControl_, &QDoubleSpinBox::valueChanged, this, [this] { updateFrequencyControls(); });
    connect(frequencyStepControl_, &QDoubleSpinBox::valueChanged, this, [this] { updateFrequencyControls(); });
    connect(applyFrequencyButton, &QPushButton::clicked, this, [this] {
        model::FrequencyDefinition frequency;
        frequency.steppingMode = frequencySweepControl_->isChecked()
            ? frequencyModeControl_->currentData().toInt() : 0;
        frequency.startMHz = startFrequencyControl_->value();
        frequency.step = frequencySweepControl_->isChecked() ? frequencyStepControl_->value() : 0.0;
        if (frequencySweepControl_->isChecked()) {
            const auto count = model::frequencyPointCount(frequency.steppingMode,
                frequency.startMHz, endFrequencyControl_->value(), frequency.step);
            if (!count) {
                frequencyValidationLabel_->setText(frequency.steppingMode == 1
                        ? tr("Use an end frequency at or above the start and a multiplier greater than 1.")
                        : tr("Use an end frequency at or above the start and a positive step."));
                frequencyValidationLabel_->show();
                return;
            }
            frequency.count = *count;
        } else {
            frequency.count = 1;
        }
        frequencyValidationLabel_->hide();
        frequency.sourceLine = setup_.frequency ? setup_.frequency->sourceLine : 0;
        emit frequencyChanged(frequency);
    });
    connect(removeFrequencyButton_, &QPushButton::clicked, this, [this] {
        if (setup_.frequency) {
            emit frequencyDeleteRequested(setup_.frequency->sourceLine);
        }
    });
    connect(groundTypeControl_, &QComboBox::currentIndexChanged, this, [this] { updateGroundControls(); });
    connect(groundPresetControl_, &QComboBox::currentIndexChanged, this, [this] {
        if (groundPresetControl_->currentData().toInt() == 1) {
            relativePermittivityControl_->setValue(13.0);
            conductivityControl_->setValue(0.005);
        }
    });
    connect(applyGroundButton, &QPushButton::clicked, this, [this] {
        model::GroundDefinition ground;
        ground.type = static_cast<model::GroundType>(groundTypeControl_->currentData().toInt());
        ground.relativePermittivity = relativePermittivityControl_->value();
        ground.conductivity = conductivityControl_->value();
        ground.geometryGroundFlag = ground.type == model::GroundType::FreeSpace
            ? 0 : connectGroundEndsControl_->isChecked() ? 1 : -1;
        if (setup_.ground) {
            ground.sourceLine = setup_.ground->sourceLine;
            ground.geometryEndSourceLine = setup_.ground->geometryEndSourceLine;
        }
        emit groundChanged(ground);
    });
    connect(excitationTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        loadSelectedExcitation();
        updateExcitationActions();
        if (!updating_ && excitationTable_->currentRow() >= 0) {
            emit excitationSelected(excitationTable_->item(
                excitationTable_->currentRow(), 0)->data(SourceLineRole).toULongLong());
        }
    });
    connect(wireControl_, &QComboBox::currentIndexChanged, this, [this] {
        const auto* wire = model_.wireByTag(wireControl_->currentData().toInt());
        segmentControl_->setMaximum(wire == nullptr ? 1 : wire->segments);
    });
    connect(addExcitationButton_, &QPushButton::clicked, this, [this] {
        emit excitationChanged(editedExcitation(0));
    });
    connect(updateExcitationButton_, &QPushButton::clicked, this, [this] {
        const auto row = excitationTable_->currentRow();
        if (row >= 0) {
            emit excitationChanged(editedExcitation(
                excitationTable_->item(row, 0)->data(SourceLineRole).toULongLong()));
        }
    });
    connect(deleteExcitationButton_, &QPushButton::clicked, this, [this] {
        const auto row = excitationTable_->currentRow();
        if (row >= 0) {
            emit excitationDeleteRequested(
                excitationTable_->item(row, 0)->data(SourceLineRole).toULongLong());
        }
    });

    startFrequencyControl_->setValue(14.175);
    endFrequencyControl_->setValue(30.0);
    frequencyStepControl_->setValue(0.1);
    magnitudeControl_->setValue(1.0);
    relativePermittivityControl_->setValue(13.0);
    conductivityControl_->setValue(0.005);
    connectGroundEndsControl_->setChecked(true);
    updateFrequencyControls();
    updateGroundControls();
    updateExcitationActions();
}

void SetupEditor::selectExcitation(std::size_t sourceLine)
{
    updating_ = true;
    const QSignalBlocker blocker(excitationTable_);
    excitationTable_->clearSelection();
    for (auto row = 0; row < excitationTable_->rowCount(); ++row) {
        if (excitationTable_->item(row, 0)->data(SourceLineRole).toULongLong() == sourceLine) {
            excitationTable_->selectRow(row);
            excitationTable_->scrollToItem(excitationTable_->item(row, 0));
            break;
        }
    }
    updating_ = false;
    loadSelectedExcitation();
    updateExcitationActions();
}

void SetupEditor::setData(const model::AntennaModel& model, const model::ModelSetup& setup)
{
    updating_ = true;
    model_ = model;
    setup_ = setup;
    wireControl_->clear();
    for (const auto& wire : model_.wires()) {
        wireControl_->addItem(tr("Wire %1").arg(wire.tag), wire.tag);
    }
    if (setup_.frequency) {
        frequencyModeControl_->setCurrentIndex(frequencyModeControl_->findData(setup_.frequency->steppingMode));
        frequencySweepControl_->setChecked(setup_.frequency->count > 1);
        startFrequencyControl_->setValue(setup_.frequency->startMHz);
        endFrequencyControl_->setValue(model::frequencyEndMHz(*setup_.frequency));
        frequencyStepControl_->setValue(setup_.frequency->step);
    } else {
        frequencyModeControl_->setCurrentIndex(frequencyModeControl_->findData(0));
        frequencySweepControl_->setChecked(false);
        startFrequencyControl_->setValue(14.175);
        endFrequencyControl_->setValue(30.0);
        frequencyStepControl_->setValue(0.1);
    }
    removeFrequencyButton_->setEnabled(setup_.frequency.has_value());
    if (setup_.ground) {
        groundTypeControl_->setCurrentIndex(groundTypeControl_->findData(static_cast<int>(setup_.ground->type)));
        relativePermittivityControl_->setValue(setup_.ground->relativePermittivity);
        conductivityControl_->setValue(setup_.ground->conductivity);
        connectGroundEndsControl_->setChecked(setup_.ground->geometryGroundFlag == 1);
        groundPresetControl_->setCurrentIndex(
            std::abs(setup_.ground->relativePermittivity - 13.0) < 1.0e-9
                    && std::abs(setup_.ground->conductivity - 0.005) < 1.0e-12
                ? 1 : 0);
    }

    excitationTable_->setRowCount(static_cast<int>(setup_.excitations.size()));
    auto row = 0;
    for (const auto& excitation : setup_.excitations) {
        const QStringList values{QString::number(excitation.wireTag), QString::number(excitation.segment),
            QString::number(excitation.magnitude, 'g', 10), QString::number(excitation.phaseDegrees, 'g', 10)};
        for (auto column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setData(SourceLineRole, static_cast<qulonglong>(excitation.sourceLine));
            excitationTable_->setItem(row, column, item);
        }
        ++row;
    }
    updating_ = false;
    addExcitationButton_->setEnabled(!model_.empty());
    updateFrequencyControls();
    updateGroundControls();
    updateExcitationActions();
}

void SetupEditor::loadSelectedExcitation()
{
    const auto row = excitationTable_->currentRow();
    if (row < 0) {
        return;
    }
    const auto sourceLine = excitationTable_->item(row, 0)->data(SourceLineRole).toULongLong();
    const auto found = std::ranges::find(setup_.excitations, sourceLine, &model::Excitation::sourceLine);
    if (found == setup_.excitations.end()) {
        return;
    }
    updating_ = true;
    wireControl_->setCurrentIndex(wireControl_->findData(found->wireTag));
    const auto* wire = model_.wireByTag(found->wireTag);
    segmentControl_->setMaximum(wire == nullptr ? 1 : wire->segments);
    segmentControl_->setValue(found->segment);
    magnitudeControl_->setValue(found->magnitude);
    phaseControl_->setValue(found->phaseDegrees);
    updating_ = false;
}

void SetupEditor::updateFrequencyControls()
{
    const bool sweepEnabled = frequencySweepControl_->isChecked();
    const bool multiplicative = frequencyModeControl_->currentData().toInt() == 1;
    if (sweepEnabled && multiplicative && frequencyStepControl_->value() <= 1.0) {
        const QSignalBlocker blocker(frequencyStepControl_);
        frequencyStepControl_->setValue(1.1);
    }
    startFrequencyLabel_->setText(sweepEnabled ? tr("Start frequency") : tr("Frequency"));
    endFrequencyLabel_->setVisible(sweepEnabled);
    endFrequencyControl_->setVisible(sweepEnabled);
    frequencyModeLabel_->setVisible(sweepEnabled);
    frequencyModeControl_->setVisible(sweepEnabled);
    frequencyStepLabel_->setVisible(sweepEnabled);
    frequencyStepControl_->setVisible(sweepEnabled);
    frequencySummaryTitleLabel_->setVisible(sweepEnabled);
    frequencySummaryLabel_->setVisible(sweepEnabled);
    frequencyStepLabel_->setText(multiplicative ? tr("Multiplier") : tr("Step"));
    frequencyStepControl_->setSuffix(multiplicative ? QString{} : tr(" MHz"));
    if (!sweepEnabled) {
        frequencyValidationLabel_->hide();
        return;
    }
    const auto count = model::frequencyPointCount(frequencyModeControl_->currentData().toInt(),
        startFrequencyControl_->value(), endFrequencyControl_->value(), frequencyStepControl_->value());
    if (!count) {
        frequencySummaryLabel_->setText(tr("Enter a valid sweep range and spacing."));
        return;
    }
    const model::FrequencyDefinition frequency{frequencyModeControl_->currentData().toInt(), *count,
        startFrequencyControl_->value(), frequencyStepControl_->value(), 0};
    const auto actualEnd = model::frequencyEndMHz(frequency);
    frequencySummaryLabel_->setText(tr("%1 points; actual final frequency: %2 MHz")
        .arg(*count).arg(actualEnd, 0, 'g', 12));
}

void SetupEditor::updateGroundControls()
{
    const auto type = static_cast<model::GroundType>(groundTypeControl_->currentData().toInt());
    const bool finiteGround = type == model::GroundType::ReflectionApproximation
        || type == model::GroundType::SommerfeldNorton;
    const bool hasGround = type != model::GroundType::FreeSpace;
    groundPresetControl_->setEnabled(finiteGround);
    relativePermittivityControl_->setEnabled(finiteGround);
    conductivityControl_->setEnabled(finiteGround);
    connectGroundEndsControl_->setEnabled(hasGround);
}

void SetupEditor::updateExcitationActions()
{
    const bool selected = excitationTable_->currentRow() >= 0;
    updateExcitationButton_->setEnabled(selected);
    deleteExcitationButton_->setEnabled(selected);
}

auto SetupEditor::editedExcitation(std::size_t sourceLine) const -> model::Excitation
{
    return {0, wireControl_->currentData().toInt(), segmentControl_->value(),
        magnitudeControl_->value(), phaseControl_->value(), sourceLine};
}

}
