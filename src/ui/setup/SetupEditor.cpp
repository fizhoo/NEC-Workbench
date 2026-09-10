#include "ui/setup/SetupEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/PendingEditIndicator.h"

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
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {

constexpr auto SourceLineRole = Qt::UserRole;
constexpr auto GroundPermittivityRole = Qt::UserRole + 1;
constexpr auto GroundConductivityRole = Qt::UserRole + 2;

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
    hide();

    frequencyPage_ = new QWidget(this);
    frequencyPage_->setObjectName(QStringLiteral("frequencyEditorPage"));
    auto* frequencyPageLayout = new QVBoxLayout(frequencyPage_);
    frequencyPageLayout->setContentsMargins(12, 12, 12, 12);
    auto* frequencyGroup = new QGroupBox(tr("Frequency (FR)"), frequencyPage_);
    auto* frequencyLayout = new QFormLayout(frequencyGroup);
    frequencySweepControl_ = new QCheckBox(tr("Enable frequency sweep"), frequencyGroup);
    frequencyModeControl_ = new QComboBox(frequencyGroup);
    frequencyModeControl_->addItem(tr("Linear / additive"), 0);
    frequencyModeControl_->addItem(tr("Multiplicative"), 1);
    startFrequencyControl_ = createDecimalControl(frequencyGroup);
    startFrequencyControl_->setDecimals(DisplayDecimalPlaces);
    startFrequencyControl_->setRange(0.000001, 1.0e12);
    startFrequencyControl_->setSuffix(tr(" MHz"));
    endFrequencyControl_ = createDecimalControl(frequencyGroup);
    endFrequencyControl_->setDecimals(DisplayDecimalPlaces);
    endFrequencyControl_->setRange(0.000001, 1.0e12);
    endFrequencyControl_->setSuffix(tr(" MHz"));
    frequencyStepControl_ = createDecimalControl(frequencyGroup);
    frequencyStepControl_->setDecimals(DisplayDecimalPlaces);
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
    applyFrequencyButton_ = new QPushButton(tr("Apply Frequency"), frequencyGroup);
    applyFrequencyButton_->setObjectName(QStringLiteral("applyFrequencyButton"));
    removeFrequencyButton_ = new QPushButton(tr("Remove FR Card"), frequencyGroup);
    frequencyButtons->addWidget(applyFrequencyButton_);
    frequencyButtons->addWidget(removeFrequencyButton_);
    frequencyButtons->addStretch();
    frequencyLayout->addRow(frequencyButtons);

    environmentPage_ = new QWidget(this);
    environmentPage_->setObjectName(QStringLiteral("environmentEditorPage"));
    auto* environmentPageLayout = new QVBoxLayout(environmentPage_);
    environmentPageLayout->setContentsMargins(12, 12, 12, 12);
    auto* groundGroup = new QGroupBox(tr("Ground Environment (GN / GE)"), environmentPage_);
    auto* groundLayout = new QFormLayout(groundGroup);
    groundTypeControl_ = new QComboBox(groundGroup);
    groundTypeControl_->addItem(tr("Free space / no ground"), static_cast<int>(model::GroundType::FreeSpace));
    groundTypeControl_->addItem(tr("Perfect conducting ground"), static_cast<int>(model::GroundType::Perfect));
    groundTypeControl_->addItem(tr("Real ground — reflection approximation"),
        static_cast<int>(model::GroundType::ReflectionApproximation));
    groundTypeControl_->addItem(tr("Real ground — Sommerfeld/Norton"),
        static_cast<int>(model::GroundType::SommerfeldNorton));
    groundPresetControl_ = new QComboBox(groundGroup);
    groundPresetControl_->setObjectName(QStringLiteral("groundPresetControl"));
    groundPresetControl_->addItem(tr("Custom"));
    const auto addGroundPreset = [this](const QString& label, double relativePermittivity,
                                     double conductivity, const QString& description) {
        const auto index = groundPresetControl_->count();
        groundPresetControl_->addItem(label);
        groundPresetControl_->setItemData(index, relativePermittivity, GroundPermittivityRole);
        groundPresetControl_->setItemData(index, conductivity, GroundConductivityRole);
        groundPresetControl_->setItemData(index, description, Qt::ToolTipRole);
    };
    addGroundPreset(tr("Salt water (εr 81, 5 S/m)"), 81.0, 5.0,
        tr("Salt water; an approximate starting value."));
    addGroundPreset(tr("Fresh water (εr 80, 0.001 S/m)"), 80.0, 0.001,
        tr("Fresh water; high permittivity but low conductivity."));
    addGroundPreset(tr("Very good ground (εr 20, 0.0303 S/m)"), 20.0, 0.0303,
        tr("Pastoral low hills with rich soil."));
    addGroundPreset(tr("Good ground (εr 14, 0.01 S/m)"), 14.0, 0.01,
        tr("Pastoral low hills with rich soil."));
    addGroundPreset(tr("Average ground (εr 13, 0.005 S/m)"), 13.0, 0.005,
        tr("Typical heavy-clay ground."));
    addGroundPreset(tr("Poor rocky ground (εr 13, 0.002 S/m)"), 13.0, 0.002,
        tr("Rocky or mountainous terrain."));
    addGroundPreset(tr("Sandy / dry ground (εr 10, 0.002 S/m)"), 10.0, 0.002,
        tr("Sandy, dry, flat, or coastal terrain."));
    addGroundPreset(tr("Very poor urban ground (εr 5, 0.001 S/m)"), 5.0, 0.001,
        tr("Cities and industrial areas."));
    addGroundPreset(tr("Extremely poor urban ground (εr 3, 0.001 S/m)"), 3.0, 0.001,
        tr("Dense industrial areas with tall buildings."));
    relativePermittivityControl_ = createDecimalControl(groundGroup);
    relativePermittivityControl_->setObjectName(QStringLiteral("groundRelativePermittivity"));
    relativePermittivityControl_->setDecimals(DisplayDecimalPlaces);
    relativePermittivityControl_->setRange(0.000001, 1.0e9);
    conductivityControl_ = createDecimalControl(groundGroup);
    conductivityControl_->setObjectName(QStringLiteral("groundConductivity"));
    conductivityControl_->setRange(0.0, 1.0e9);
    conductivityControl_->setSuffix(tr(" S/m"));
    connectGroundEndsControl_ = new QCheckBox(tr("Connect wire ends that terminate on Z = 0"), groundGroup);
    applyGroundButton_ = new QPushButton(tr("Apply Ground"), groundGroup);
    applyGroundButton_->setObjectName(QStringLiteral("applyGroundButton"));
    groundLayout->addRow(tr("Environment"), groundTypeControl_);
    groundLayout->addRow(tr("Material preset"), groundPresetControl_);
    groundLayout->addRow(tr("Relative permittivity"), relativePermittivityControl_);
    groundLayout->addRow(tr("Conductivity"), conductivityControl_);
    groundLayout->addRow({}, connectGroundEndsControl_);
    groundLayout->addRow(applyGroundButton_);

    sourcesPage_ = new QWidget(this);
    sourcesPage_->setObjectName(QStringLiteral("sourcesEditorPage"));
    auto* sourcesPageLayout = new QVBoxLayout(sourcesPage_);
    sourcesPageLayout->setContentsMargins(12, 12, 12, 12);
    auto* excitationGroup = new QGroupBox(tr("Voltage Sources (EX 0)"), sourcesPage_);
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
    magnitudeControl_->setDecimals(DisplayDecimalPlaces);
    magnitudeControl_->setRange(0.0, 1.0e12);
    phaseControl_ = createDecimalControl(excitationGroup);
    phaseControl_->setDecimals(DisplayDecimalPlaces);
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

    frequencyPageLayout->addWidget(frequencyGroup);
    frequencyPageLayout->addStretch();
    environmentPageLayout->addWidget(groundGroup);
    environmentPageLayout->addStretch();
    sourcesPageLayout->addWidget(excitationGroup, 1);

    connect(frequencySweepControl_, &QCheckBox::toggled, this, [this] {
        updateFrequencyControls(); if (!updating_) setFrequencyPending(true);
    });
    connect(frequencyModeControl_, &QComboBox::currentIndexChanged, this, [this] {
        updateFrequencyControls(); if (!updating_) setFrequencyPending(true);
    });
    const auto frequencyEdited = [this] {
        updateFrequencyControls(); if (!updating_) setFrequencyPending(true);
    };
    connect(startFrequencyControl_, &QDoubleSpinBox::valueChanged, this, frequencyEdited);
    connect(endFrequencyControl_, &QDoubleSpinBox::valueChanged, this, frequencyEdited);
    connect(frequencyStepControl_, &QDoubleSpinBox::valueChanged, this, frequencyEdited);
    connect(applyFrequencyButton_, &QPushButton::clicked, this, [this] {
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
        setFrequencyPending(false);
        emit frequencyChanged(frequency);
    });
    connect(removeFrequencyButton_, &QPushButton::clicked, this, [this] {
        if (setup_.frequency) {
            emit frequencyDeleteRequested(setup_.frequency->sourceLine);
        }
    });
    connect(groundTypeControl_, &QComboBox::currentIndexChanged, this, [this] {
        updateGroundControls(); if (!updating_) setGroundPending(true);
    });
    connect(groundPresetControl_, &QComboBox::currentIndexChanged, this, [this] {
        const auto relativePermittivity = groundPresetControl_->currentData(GroundPermittivityRole);
        const auto conductivity = groundPresetControl_->currentData(GroundConductivityRole);
        if (!relativePermittivity.isValid() || !conductivity.isValid()) return;
        relativePermittivityControl_->setValue(relativePermittivity.toDouble());
        conductivityControl_->setValue(conductivity.toDouble());
        if (!updating_) setGroundPending(true);
    });
    connect(relativePermittivityControl_, &QDoubleSpinBox::valueChanged,
        this, [this] { updateGroundPresetSelection(); if (!updating_) setGroundPending(true); });
    connect(conductivityControl_, &QDoubleSpinBox::valueChanged,
        this, [this] { updateGroundPresetSelection(); if (!updating_) setGroundPending(true); });
    connect(connectGroundEndsControl_, &QCheckBox::toggled, this,
        [this] { if (!updating_) setGroundPending(true); });
    connect(applyGroundButton_, &QPushButton::clicked, this, [this] {
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
        setGroundPending(false);
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
        if (!updating_) setExcitationPending(true);
    });
    connect(segmentControl_, &QSpinBox::valueChanged, this,
        [this] { if (!updating_) setExcitationPending(true); });
    connect(magnitudeControl_, &QDoubleSpinBox::valueChanged, this,
        [this] { if (!updating_) setExcitationPending(true); });
    connect(phaseControl_, &QDoubleSpinBox::valueChanged, this,
        [this] { if (!updating_) setExcitationPending(true); });
    connect(addExcitationButton_, &QPushButton::clicked, this, [this] {
        setExcitationPending(false);
        emit excitationChanged(editedExcitation(0));
    });
    connect(updateExcitationButton_, &QPushButton::clicked, this, [this] {
        const auto row = excitationTable_->currentRow();
        if (row >= 0) {
            setExcitationPending(false);
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
    updateGroundPresetSelection();
    updateFrequencyControls();
    updateGroundControls();
    updateExcitationActions();
    setFrequencyPending(false);
    setGroundPending(false);
    setExcitationPending(false);
}

auto SetupEditor::hasPendingEdits(const QWidget* page) const -> bool
{
    if (page == frequencyPage_) return frequencyPending_;
    if (page == environmentPage_) return groundPending_;
    if (page == sourcesPage_) return excitationPending_;
    return false;
}

void SetupEditor::discardPendingEdits(const QWidget* page)
{
    if (!hasPendingEdits(page)) return;
    setData(model_, setup_);
}

auto SetupEditor::frequencyPage() const -> QWidget*
{
    return frequencyPage_;
}

auto SetupEditor::sourcesPage() const -> QWidget*
{
    return sourcesPage_;
}

auto SetupEditor::environmentPage() const -> QWidget*
{
    return environmentPage_;
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
        updateGroundPresetSelection();
    } else {
        groundTypeControl_->setCurrentIndex(groundTypeControl_->findData(
            static_cast<int>(model::GroundType::FreeSpace)));
        relativePermittivityControl_->setValue(13.0);
        conductivityControl_->setValue(0.005);
        connectGroundEndsControl_->setChecked(true);
        updateGroundPresetSelection();
    }

    excitationTable_->setRowCount(static_cast<int>(setup_.excitations.size()));
    auto row = 0;
    for (const auto& excitation : setup_.excitations) {
        const QStringList values{QString::number(excitation.wireTag),
            QString::number(excitation.segment), formatDecimal(excitation.magnitude),
            formatDecimal(excitation.phaseDegrees)};
        for (auto column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setData(SourceLineRole, static_cast<qulonglong>(excitation.sourceLine));
            excitationTable_->setItem(row, column, item);
        }
        ++row;
    }
    updating_ = false;
    setFrequencyPending(false);
    setGroundPending(false);
    setExcitationPending(false);
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
        .arg(*count).arg(formatDecimal(actualEnd)));
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

void SetupEditor::updateGroundPresetSelection()
{
    auto matchingIndex = 0;
    for (auto index = 1; index < groundPresetControl_->count(); ++index) {
        const auto relativePermittivity = groundPresetControl_->itemData(
            index, GroundPermittivityRole).toDouble();
        const auto conductivity = groundPresetControl_->itemData(
            index, GroundConductivityRole).toDouble();
        if (std::abs(relativePermittivityControl_->value() - relativePermittivity) < 1.0e-9
            && std::abs(conductivityControl_->value() - conductivity) < 1.0e-12) {
            matchingIndex = index;
            break;
        }
    }
    const QSignalBlocker blocker(groundPresetControl_);
    groundPresetControl_->setCurrentIndex(matchingIndex);
}

void SetupEditor::updateExcitationActions()
{
    const bool selected = excitationTable_->currentRow() >= 0;
    updateExcitationButton_->setEnabled(selected);
    deleteExcitationButton_->setEnabled(selected);
    setPendingEditIndicator(selected ? updateExcitationButton_ : addExcitationButton_,
        excitationPending_);
    setPendingEditIndicator(selected ? addExcitationButton_ : updateExcitationButton_, false);
}

void SetupEditor::setFrequencyPending(bool pending)
{
    frequencyPending_ = pending;
    setPendingEditIndicator(applyFrequencyButton_, pending);
}

void SetupEditor::setGroundPending(bool pending)
{
    groundPending_ = pending;
    setPendingEditIndicator(applyGroundButton_, pending);
}

void SetupEditor::setExcitationPending(bool pending)
{
    excitationPending_ = pending;
    updateExcitationActions();
}

auto SetupEditor::editedExcitation(std::size_t sourceLine) const -> model::Excitation
{
    return {0, wireControl_->currentData().toInt(), segmentControl_->value(),
        magnitudeControl_->value(), phaseControl_->value(), sourceLine};
}

}
