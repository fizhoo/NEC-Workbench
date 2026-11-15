#include "ui/setup/SetupEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/PendingEditIndicator.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {

constexpr auto GroundPermittivityRole = Qt::UserRole + 1;
constexpr auto GroundConductivityRole = Qt::UserRole + 2;

// The spin boxes and lists ask for the width of their widest value or item; these take
// what the column gives them.
constexpr auto FieldMinimumWidth = 96;

auto createDecimalControl(QWidget* parent) -> QDoubleSpinBox*
{
    auto* control = new QDoubleSpinBox(parent);
    control->setDecimals(9);
    control->setRange(-1.0e12, 1.0e12);
    control->setKeyboardTracking(false);
    control->setMinimumWidth(FieldMinimumWidth);
    return control;
}

auto compactCombo(QWidget* parent) -> QComboBox*
{
    auto* combo = new QComboBox(parent);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(8);
    return combo;
}

auto compactForm(QWidget* page) -> QFormLayout*
{
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return layout;
}

auto buttonRow(std::initializer_list<QPushButton*> buttons) -> QHBoxLayout*
{
    auto* row = new QHBoxLayout;
    for (auto* button : buttons) row->addWidget(button);
    row->addStretch();
    return row;
}

}

SetupEditor::SetupEditor(QWidget* parent)
    : QWidget(parent)
{
    hide();

    // Each page is a short form that fits the left column: labels above their fields when
    // the column is narrow, short names, and the detail in tooltips.
    frequencyPage_ = new QWidget(this);
    frequencyPage_->setObjectName(QStringLiteral("frequencyEditorPage"));
    auto* frequencyLayout = compactForm(frequencyPage_);
    frequencySweepControl_ = new QCheckBox(tr("Sweep"), frequencyPage_);
    frequencySweepControl_->setToolTip(tr("Solve at a range of frequencies (FR with several points)"));
    frequencyModeControl_ = compactCombo(frequencyPage_);
    frequencyModeControl_->addItem(tr("Additive"), 0);
    frequencyModeControl_->setItemData(0, tr("Each point adds the step"), Qt::ToolTipRole);
    frequencyModeControl_->addItem(tr("Multiplicative"), 1);
    frequencyModeControl_->setItemData(1, tr("Each point multiplies by the step"), Qt::ToolTipRole);
    startFrequencyControl_ = createDecimalControl(frequencyPage_);
    startFrequencyControl_->setDecimals(DisplayDecimalPlaces);
    startFrequencyControl_->setRange(0.000001, 1.0e12);
    startFrequencyControl_->setSuffix(tr(" MHz"));
    endFrequencyControl_ = createDecimalControl(frequencyPage_);
    endFrequencyControl_->setDecimals(DisplayDecimalPlaces);
    endFrequencyControl_->setRange(0.000001, 1.0e12);
    endFrequencyControl_->setSuffix(tr(" MHz"));
    frequencyStepControl_ = createDecimalControl(frequencyPage_);
    frequencyStepControl_->setDecimals(DisplayDecimalPlaces);
    frequencyStepControl_->setRange(0.0, 1.0e12);
    startFrequencyLabel_ = new QLabel(tr("Frequency"), frequencyPage_);
    endFrequencyLabel_ = new QLabel(tr("End"), frequencyPage_);
    frequencyModeLabel_ = new QLabel(tr("Spacing"), frequencyPage_);
    frequencyStepLabel_ = new QLabel(tr("Step"), frequencyPage_);
    frequencySummaryTitleLabel_ = new QLabel(tr("Points"), frequencyPage_);
    frequencySummaryLabel_ = new QLabel(frequencyPage_);
    frequencySummaryLabel_->setWordWrap(true);
    frequencyValidationLabel_ = new QLabel(frequencyPage_);
    frequencyValidationLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
    frequencyValidationLabel_->setWordWrap(true);
    frequencyLayout->addRow({}, frequencySweepControl_);
    frequencyLayout->addRow(startFrequencyLabel_, startFrequencyControl_);
    frequencyLayout->addRow(endFrequencyLabel_, endFrequencyControl_);
    frequencyLayout->addRow(frequencyModeLabel_, frequencyModeControl_);
    frequencyLayout->addRow(frequencyStepLabel_, frequencyStepControl_);
    frequencyLayout->addRow(frequencySummaryTitleLabel_, frequencySummaryLabel_);
    frequencyLayout->addRow(frequencyValidationLabel_);
    applyFrequencyButton_ = new QPushButton(tr("Apply"), frequencyPage_);
    applyFrequencyButton_->setObjectName(QStringLiteral("applyFrequencyButton"));
    removeFrequencyButton_ = new QPushButton(tr("Remove FR"), frequencyPage_);
    removeFrequencyButton_->setToolTip(tr("Remove the FR card from the deck"));
    frequencyLayout->addRow(buttonRow({applyFrequencyButton_, removeFrequencyButton_}));

    environmentPage_ = new QWidget(this);
    environmentPage_->setObjectName(QStringLiteral("environmentEditorPage"));
    auto* groundLayout = compactForm(environmentPage_);
    groundTypeControl_ = compactCombo(environmentPage_);
    groundTypeControl_->setToolTip(tr("The ground under the antenna (GN, and the GE flag)"));
    const auto addGroundType = [this](const QString& label, model::GroundType type,
                                   const QString& description) {
        groundTypeControl_->addItem(label, static_cast<int>(type));
        groundTypeControl_->setItemData(groundTypeControl_->count() - 1, description, Qt::ToolTipRole);
    };
    addGroundType(tr("Free space"), model::GroundType::FreeSpace, tr("Free space, no ground"));
    addGroundType(tr("Perfect"), model::GroundType::Perfect, tr("Perfectly conducting ground"));
    addGroundType(tr("Reflection"), model::GroundType::ReflectionApproximation,
        tr("Real ground, reflection-coefficient approximation"));
    addGroundType(tr("Sommerfeld"), model::GroundType::SommerfeldNorton,
        tr("Real ground, Sommerfeld/Norton method"));
    groundPresetControl_ = compactCombo(environmentPage_);
    groundPresetControl_->setObjectName(QStringLiteral("groundPresetControl"));
    groundPresetControl_->addItem(tr("Custom"));
    const auto addGroundPreset = [this](const QString& label, double relativePermittivity,
                                     double conductivity, const QString& description) {
        const auto index = groundPresetControl_->count();
        groundPresetControl_->addItem(label);
        groundPresetControl_->setItemData(index, relativePermittivity, GroundPermittivityRole);
        groundPresetControl_->setItemData(index, conductivity, GroundConductivityRole);
        groundPresetControl_->setItemData(index, tr("%1 εr %2, σ %3 S/m").arg(description,
            formatDecimal(relativePermittivity), formatDecimal(conductivity)), Qt::ToolTipRole);
    };
    addGroundPreset(tr("Salt water"), 81.0, 5.0, tr("Salt water; an approximate starting value."));
    addGroundPreset(tr("Fresh water"), 80.0, 0.001,
        tr("Fresh water; high permittivity but low conductivity."));
    addGroundPreset(tr("Very good"), 20.0, 0.0303, tr("Pastoral low hills with rich soil."));
    addGroundPreset(tr("Good"), 14.0, 0.01, tr("Pastoral low hills with rich soil."));
    addGroundPreset(tr("Average"), 13.0, 0.005, tr("Typical heavy-clay ground."));
    addGroundPreset(tr("Poor, rocky"), 13.0, 0.002, tr("Rocky or mountainous terrain."));
    addGroundPreset(tr("Sandy, dry"), 10.0, 0.002, tr("Sandy, dry, flat, or coastal terrain."));
    addGroundPreset(tr("Very poor, urban"), 5.0, 0.001, tr("Cities and industrial areas."));
    addGroundPreset(tr("Extremely poor"), 3.0, 0.001,
        tr("Dense industrial areas with tall buildings."));
    relativePermittivityControl_ = createDecimalControl(environmentPage_);
    relativePermittivityControl_->setObjectName(QStringLiteral("groundRelativePermittivity"));
    relativePermittivityControl_->setDecimals(DisplayDecimalPlaces);
    relativePermittivityControl_->setRange(0.000001, 1.0e9);
    relativePermittivityControl_->setToolTip(tr("Relative permittivity (dielectric constant)"));
    conductivityControl_ = createDecimalControl(environmentPage_);
    conductivityControl_->setObjectName(QStringLiteral("groundConductivity"));
    conductivityControl_->setRange(0.0, 1.0e9);
    conductivityControl_->setSuffix(tr(" S/m"));
    connectGroundEndsControl_ = new QCheckBox(tr("Connect ends at Z = 0"), environmentPage_);
    connectGroundEndsControl_->setToolTip(
        tr("Connect wire ends that terminate on Z = 0 to the ground (GE 1)"));
    applyGroundButton_ = new QPushButton(tr("Apply"), environmentPage_);
    applyGroundButton_->setObjectName(QStringLiteral("applyGroundButton"));
    groundLayout->addRow(tr("Type"), groundTypeControl_);
    groundLayout->addRow(tr("Preset"), groundPresetControl_);
    groundLayout->addRow(tr("εr"), relativePermittivityControl_);
    groundLayout->addRow(tr("σ"), conductivityControl_);
    groundLayout->addRow({}, connectGroundEndsControl_);
    groundLayout->addRow(buttonRow({applyGroundButton_}));

    // The reference impedance SWR is measured against.
    referenceImpedancePage_ = new QWidget(this);
    referenceImpedancePage_->setObjectName(QStringLiteral("referenceImpedancePage"));
    auto* referenceLayout = compactForm(referenceImpedancePage_);
    referenceImpedanceControl_ = createDecimalControl(referenceImpedancePage_);
    referenceImpedanceControl_->setObjectName(QStringLiteral("sourceReferenceImpedance"));
    referenceImpedanceControl_->setDecimals(DisplayDecimalPlaces);
    referenceImpedanceControl_->setRange(0.001, 1000000.0);
    referenceImpedanceControl_->setValue(50.0);
    referenceImpedanceControl_->setSuffix(tr(" Ω"));
    referenceImpedanceControl_->setToolTip(tr(
        "Reference impedance used for Workbench SWR, matching results, and optimizer defaults. "
        "It is stored as the Z0 compatibility card and does not change the NEC field solution."));
    applyReferenceImpedanceButton_ = new QPushButton(tr("Apply"), referenceImpedancePage_);
    applyReferenceImpedanceButton_->setObjectName(
        QStringLiteral("applyReferenceImpedanceButton"));
    removeReferenceImpedanceButton_ = new QPushButton(tr("50 Ω"), referenceImpedancePage_);
    removeReferenceImpedanceButton_->setObjectName(
        QStringLiteral("removeReferenceImpedanceButton"));
    removeReferenceImpedanceButton_->setToolTip(tr("Remove the Z0 card and use the default 50 Ω"));
    referenceLayout->addRow(tr("Z0"), referenceImpedanceControl_);
    referenceLayout->addRow(buttonRow({applyReferenceImpedanceButton_,
        removeReferenceImpedanceButton_}));

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
    connect(referenceImpedanceControl_, &QDoubleSpinBox::valueChanged,
        this, [this] { if (!updating_) setReferenceImpedancePending(true); });
    connect(applyReferenceImpedanceButton_, &QPushButton::clicked, this, [this] {
        model::ReferenceImpedanceDefinition reference;
        reference.ohms = referenceImpedanceControl_->value();
        reference.sourceLine = setup_.referenceImpedance
            ? setup_.referenceImpedance->sourceLine : 0;
        setReferenceImpedancePending(false);
        emit referenceImpedanceChanged(reference);
    });
    connect(removeReferenceImpedanceButton_, &QPushButton::clicked, this, [this] {
        if (setup_.referenceImpedance)
            emit referenceImpedanceDeleteRequested(setup_.referenceImpedance->sourceLine);
    });
    startFrequencyControl_->setValue(14.175);
    endFrequencyControl_->setValue(30.0);
    frequencyStepControl_->setValue(0.1);
    relativePermittivityControl_->setValue(13.0);
    conductivityControl_->setValue(0.005);
    connectGroundEndsControl_->setChecked(true);
    updateGroundPresetSelection();
    updateFrequencyControls();
    updateGroundControls();
    setFrequencyPending(false);
    setGroundPending(false);
    setReferenceImpedancePending(false);
}

auto SetupEditor::hasPendingEdits(const QWidget* page) const -> bool
{
    if (page == frequencyPage_) return frequencyPending_;
    if (page == environmentPage_) return groundPending_;
    if (page == referenceImpedancePage_) return referenceImpedancePending_;
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

auto SetupEditor::referenceImpedancePage() const -> QWidget*
{
    return referenceImpedancePage_;
}

auto SetupEditor::environmentPage() const -> QWidget*
{
    return environmentPage_;
}

void SetupEditor::setData(const model::AntennaModel& model, const model::ModelSetup& setup)
{
    updating_ = true;
    model_ = model;
    setup_ = setup;
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

    referenceImpedanceControl_->setValue(model::referenceImpedanceOhms(setup_));
    removeReferenceImpedanceButton_->setEnabled(setup_.referenceImpedance.has_value());

    updating_ = false;
    setFrequencyPending(false);
    setGroundPending(false);
    setReferenceImpedancePending(false);
    updateFrequencyControls();
    updateGroundControls();
}

void SetupEditor::updateFrequencyControls()
{
    const bool sweepEnabled = frequencySweepControl_->isChecked();
    const bool multiplicative = frequencyModeControl_->currentData().toInt() == 1;
    if (sweepEnabled && multiplicative && frequencyStepControl_->value() <= 1.0) {
        const QSignalBlocker blocker(frequencyStepControl_);
        frequencyStepControl_->setValue(1.1);
    }
    startFrequencyLabel_->setText(sweepEnabled ? tr("Start") : tr("Frequency"));
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
    frequencySummaryLabel_->setText(tr("%1, ending at %2 MHz")
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

void SetupEditor::setReferenceImpedancePending(bool pending)
{
    referenceImpedancePending_ = pending;
    setPendingEditIndicator(applyReferenceImpedanceButton_, pending);
}

}
