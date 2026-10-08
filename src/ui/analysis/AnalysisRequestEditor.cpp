#include "ui/analysis/AnalysisRequestEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/PendingEditIndicator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {
constexpr auto SourceLineRole = Qt::UserRole;
constexpr auto PendingDeletionRole = Qt::UserRole + 7;

enum class PatternType { Full3D, Horizontal, Vertical, Custom };
enum class PatternFrequencyMode { ModelSweep, Single, Selected, Continuous };

auto angleControl(QWidget* parent, double minimum, double maximum) -> QDoubleSpinBox*
{
    auto* control = new QDoubleSpinBox(parent);
    control->setDecimals(3);
    control->setRange(minimum, maximum);
    control->setSuffix(QObject::tr("°"));
    control->setKeyboardTracking(false);
    return control;
}

auto patternType(const model::RadiationPatternRequest& pattern) -> PatternType
{
    if (pattern.thetaCount == 1 && pattern.phiCount > 1) return PatternType::Horizontal;
    if (pattern.thetaCount > 1 && pattern.phiCount == 2
        && std::abs(std::abs(pattern.phiStep) - 180.0) < 1.0e-9) return PatternType::Vertical;
    if (pattern.thetaCount > 1 && pattern.phiCount > 2) return PatternType::Full3D;
    return PatternType::Custom;
}

auto patternTypeName(PatternType type) -> QString
{
    switch (type) {
    case PatternType::Full3D: return QObject::tr("Full 3D pattern");
    case PatternType::Horizontal: return QObject::tr("Horizontal cut");
    case PatternType::Vertical: return QObject::tr("Vertical cut");
    case PatternType::Custom: return QObject::tr("Custom RP grid");
    }
    return {};
}

auto rangeSummary(double start, int count, double step) -> QString
{
    if (count <= 1) return QObject::tr("Fixed %1°").arg(formatDecimal(start));
    const auto end = start + (count - 1) * step;
    return QObject::tr("%1–%2° / %3°")
        .arg(formatDecimal(start), formatDecimal(end), formatDecimal(step));
}
}

AnalysisRequestEditor::AnalysisRequestEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    auto* heading = new QLabel(tr("Requested Results"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);
    auto* description = new QLabel(tr(
        "Manage every XQ and RP request already present in the NEC deck."), this);
    description->setWordWrap(true);

    auto* currentGroup = new QGroupBox(tr("Currents and Feed Impedance"), this);
    auto* currentLayout = new QHBoxLayout(currentGroup);
    executionControl_ = new QCheckBox(
        tr("Request structure currents and feed impedance (XQ)"), currentGroup);
    auto* applyExecution = new QPushButton(tr("Apply Current Request"), currentGroup);
    currentLayout->addWidget(executionControl_);
    currentLayout->addStretch();
    currentLayout->addWidget(applyExecution);

    auto* patternGroup = new QGroupBox(tr("Far-Field Radiation Patterns"), this);
    auto* patternLayout = new QVBoxLayout(patternGroup);

    auto* frequencyPolicyGroup = new QGroupBox(tr("Pattern Frequencies"), patternGroup);
    frequencyPolicyGroup->setObjectName(QStringLiteral("radiationFrequencyPolicyGroup"));
    auto* frequencyPolicyLayout = new QVBoxLayout(frequencyPolicyGroup);
    auto* frequencyPolicyRow = new QHBoxLayout;
    radiationFrequencyModeControl_ = new QComboBox(frequencyPolicyGroup);
    radiationFrequencyModeControl_->setObjectName(QStringLiteral("radiationFrequencyMode"));
    radiationFrequencyModeControl_->addItem(tr("Use Model FR Frequencies"),
        static_cast<int>(PatternFrequencyMode::ModelSweep));
    radiationFrequencyModeControl_->addItem(tr("Single Frequency"),
        static_cast<int>(PatternFrequencyMode::Single));
    radiationFrequencyModeControl_->addItem(tr("Use Selected Frequencies"),
        static_cast<int>(PatternFrequencyMode::Selected));
    radiationFrequencyModeControl_->addItem(tr("Custom Continuous Sweep"),
        static_cast<int>(PatternFrequencyMode::Continuous));
    const auto savedMode = QSettings{}.value(QStringLiteral("analysis/patternFrequencyMode"),
        static_cast<int>(PatternFrequencyMode::Single)).toInt();
    radiationFrequencyModeControl_->setCurrentIndex(
        std::max(0, radiationFrequencyModeControl_->findData(savedMode)));
    frequencyPolicyRow->addWidget(new QLabel(tr("Run every RP request at:"), frequencyPolicyGroup));
    frequencyPolicyRow->addWidget(radiationFrequencyModeControl_, 1);
    frequencyPolicyRow->addStretch();
    radiationFrequencyPages_ = new QStackedWidget(frequencyPolicyGroup);

    auto* modelFrequencyPage = new QLabel(tr(
        "Uses the model's FR card directly. NEC calculates every RP request at every FR point."),
        radiationFrequencyPages_);
    modelFrequencyPage->setWordWrap(true);
    radiationFrequencyPages_->addWidget(modelFrequencyPage);

    auto* singleFrequencyPage = new QWidget(radiationFrequencyPages_);
    auto* singleFrequencyLayout = new QHBoxLayout(singleFrequencyPage);
    singleFrequencyLayout->setContentsMargins(0, 0, 0, 0);
    singleFrequencyControl_ = new QDoubleSpinBox(singleFrequencyPage);
    singleFrequencyControl_->setObjectName(QStringLiteral("radiationSingleFrequency"));
    singleFrequencyControl_->setDecimals(3);
    singleFrequencyControl_->setRange(0.000001, 1.0e9);
    singleFrequencyControl_->setSuffix(tr(" MHz"));
    singleFrequencyLayout->addWidget(new QLabel(tr("Frequency"), singleFrequencyPage));
    singleFrequencyLayout->addWidget(singleFrequencyControl_);
    singleFrequencyLayout->addStretch();
    radiationFrequencyPages_->addWidget(singleFrequencyPage);

    auto* selectedFrequencyPage = new QWidget(radiationFrequencyPages_);
    auto* selectedFrequencyLayout = new QVBoxLayout(selectedFrequencyPage);
    selectedFrequencyLayout->setContentsMargins(0, 0, 0, 0);
    auto* selectedFrequencyActions = new QHBoxLayout;
    frequencyEntryControl_ = new QDoubleSpinBox(selectedFrequencyPage);
    frequencyEntryControl_->setObjectName(QStringLiteral("radiationFrequencyEntry"));
    frequencyEntryControl_->setDecimals(3);
    frequencyEntryControl_->setRange(0.000001, 1.0e9);
    frequencyEntryControl_->setSuffix(tr(" MHz"));
    auto* addFrequency = new QPushButton(tr("Add"), selectedFrequencyPage);
    auto* addAmateurBandCenters = new QPushButton(
        tr("Amateur Band Centers…"), selectedFrequencyPage);
    addAmateurBandCenters->setObjectName(
        QStringLiteral("radiationAddAmateurBandCenters"));
    addAmateurBandCenters->setToolTip(tr(
        "Add one representative center frequency for each selected amateur band."));
    auto* pasteFrequencies = new QPushButton(tr("Paste List…"), selectedFrequencyPage);
    auto* removeFrequencies = new QPushButton(tr("Remove"), selectedFrequencyPage);
    auto* clearFrequencies = new QPushButton(tr("Clear All"), selectedFrequencyPage);
    clearFrequencies->setObjectName(QStringLiteral("radiationClearFrequencies"));
    selectedFrequencyActions->addWidget(frequencyEntryControl_);
    selectedFrequencyActions->addWidget(addFrequency);
    selectedFrequencyActions->addWidget(addAmateurBandCenters);
    selectedFrequencyActions->addStretch();
    selectedFrequencyActions->addWidget(pasteFrequencies);
    selectedFrequencyActions->addWidget(removeFrequencies);
    selectedFrequencyActions->addWidget(clearFrequencies);
    selectedFrequencies_ = new QListWidget(selectedFrequencyPage);
    selectedFrequencies_->setObjectName(QStringLiteral("radiationSelectedFrequencies"));
    selectedFrequencies_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    selectedFrequencies_->setViewMode(QListView::IconMode);
    selectedFrequencies_->setFlow(QListView::LeftToRight);
    selectedFrequencies_->setWrapping(true);
    selectedFrequencies_->setResizeMode(QListView::Adjust);
    selectedFrequencies_->setMovement(QListView::Static);
    selectedFrequencies_->setUniformItemSizes(true);
    selectedFrequencies_->setGridSize(QSize(92, 28));
    selectedFrequencies_->setSpacing(2);
    selectedFrequencies_->setMinimumHeight(72);
    selectedFrequencies_->setMaximumHeight(120);
    selectedFrequencies_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    selectedFrequencies_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    selectedFrequencyLayout->addLayout(selectedFrequencyActions);
    selectedFrequencyLayout->addWidget(new QLabel(
        tr("Selected frequencies (MHz)"), selectedFrequencyPage));
    selectedFrequencyLayout->addWidget(selectedFrequencies_);
    radiationFrequencyPages_->addWidget(selectedFrequencyPage);

    auto* continuousFrequencyPage = new QWidget(radiationFrequencyPages_);
    auto* continuousFrequencyLayout = new QHBoxLayout(continuousFrequencyPage);
    continuousFrequencyLayout->setContentsMargins(0, 0, 0, 0);
    continuousStartControl_ = new QDoubleSpinBox(continuousFrequencyPage);
    continuousStopControl_ = new QDoubleSpinBox(continuousFrequencyPage);
    continuousStepControl_ = new QDoubleSpinBox(continuousFrequencyPage);
    continuousStartControl_->setObjectName(QStringLiteral("radiationContinuousStart"));
    continuousStopControl_->setObjectName(QStringLiteral("radiationContinuousStop"));
    continuousStepControl_->setObjectName(QStringLiteral("radiationContinuousStep"));
    for (auto* control : {continuousStartControl_, continuousStopControl_,
             continuousStepControl_}) {
        control->setDecimals(3);
        control->setRange(0.000001, 1.0e9);
        control->setSuffix(tr(" MHz"));
    }
    continuousStartControl_->setValue(14.0);
    continuousStopControl_->setValue(14.35);
    continuousStepControl_->setValue(0.05);
    const QSettings frequencySettings;
    singleFrequencyInitialized_ = frequencySettings.contains(
        QStringLiteral("analysis/patternSingleFrequencyMHz"));
    selectedFrequenciesInitialized_ = frequencySettings.contains(
        QStringLiteral("analysis/patternSelectedFrequenciesMHz"));
    singleFrequencyControl_->setValue(frequencySettings.value(
        QStringLiteral("analysis/patternSingleFrequencyMHz"), 14.175).toDouble());
    continuousStartControl_->setValue(frequencySettings.value(
        QStringLiteral("analysis/patternContinuousStartMHz"), 14.0).toDouble());
    continuousStopControl_->setValue(frequencySettings.value(
        QStringLiteral("analysis/patternContinuousStopMHz"), 14.35).toDouble());
    continuousStepControl_->setValue(frequencySettings.value(
        QStringLiteral("analysis/patternContinuousStepMHz"), 0.05).toDouble());
    continuousFrequencyLayout->addWidget(new QLabel(tr("Start"), continuousFrequencyPage));
    continuousFrequencyLayout->addWidget(continuousStartControl_);
    continuousFrequencyLayout->addWidget(new QLabel(tr("Stop"), continuousFrequencyPage));
    continuousFrequencyLayout->addWidget(continuousStopControl_);
    continuousFrequencyLayout->addWidget(new QLabel(tr("Step"), continuousFrequencyPage));
    continuousFrequencyLayout->addWidget(continuousStepControl_);
    radiationFrequencyPages_->addWidget(continuousFrequencyPage);

    sweepCostLabel_ = new QLabel(frequencyPolicyGroup);
    sweepCostLabel_->setWordWrap(true);
    applyFrequencyButton_ = new QPushButton(tr("Apply Pattern Frequencies"), frequencyPolicyGroup);
    applyFrequencyButton_->setObjectName(QStringLiteral("applyRadiationFrequenciesButton"));
    applyFrequencyButton_->setToolTip(tr(
        "Apply this frequency selection to every RP request without changing the RP cards."));
    auto* frequencyApplyRow = new QHBoxLayout;
    frequencyApplyRow->addWidget(sweepCostLabel_, 1);
    frequencyApplyRow->addWidget(applyFrequencyButton_);
    frequencyPolicyLayout->addLayout(frequencyPolicyRow);
    frequencyPolicyLayout->addWidget(radiationFrequencyPages_);
    frequencyPolicyLayout->addLayout(frequencyApplyRow);
    patternLayout->addWidget(frequencyPolicyGroup);

    patterns_ = new QTableWidget(0, 4, patternGroup);
    patterns_->setObjectName(QStringLiteral("radiationRequestsTable"));
    patterns_->setHorizontalHeaderLabels(
        {tr("Pattern"), tr("Theta"), tr("Phi"), tr("Samples")});
    patterns_->horizontalHeader()->setStretchLastSection(false);
    patterns_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    patterns_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    patterns_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    patterns_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    patterns_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    patterns_->setSelectionBehavior(QAbstractItemView::SelectRows);
    patterns_->setSelectionMode(QAbstractItemView::SingleSelection);
    patternLayout->addWidget(patterns_);

    auto* patternButtons = new QHBoxLayout;
    auto* addPattern = new QPushButton(tr("Add Pattern"), patternGroup);
    addPattern->setObjectName(QStringLiteral("addRadiationPatternButton"));
    auto* duplicatePattern = new QPushButton(tr("Duplicate"), patternGroup);
    deletePatternButton_ = new QPushButton(tr("Delete Pattern"), patternGroup);
    deletePatternButton_->setObjectName(QStringLiteral("deleteRadiationPatternButton"));
    deletePatternButton_->setToolTip(tr(
        "Mark the selected RP request for deletion. Apply Pattern Changes commits it."));
    patternButtons->addWidget(addPattern);
    patternButtons->addWidget(duplicatePattern);
    patternButtons->addWidget(deletePatternButton_);
    patternButtons->addStretch();
    patternLayout->addLayout(patternButtons);
    auto* patternActionHint = new QLabel(tr(
        "Add, edit, or mark a pattern for deletion, then apply the selected pattern change."),
        patternGroup);
    patternActionHint->setWordWrap(true);
    patternLayout->addWidget(patternActionHint);

    auto* selectedPatternGroup = new QGroupBox(tr("Selected Pattern Details"), patternGroup);
    selectedPatternGroup->setObjectName(QStringLiteral("selectedRadiationPatternGroup"));
    patternControls_ = selectedPatternGroup;
    auto* selectedPatternLayout = new QVBoxLayout(selectedPatternGroup);
    auto* patternTypeRow = new QHBoxLayout;
    patternTypeControl_ = new QComboBox(patternControls_);
    patternTypeControl_->setObjectName(QStringLiteral("radiationPatternType"));
    patternTypeControl_->addItem(patternTypeName(PatternType::Full3D),
        static_cast<int>(PatternType::Full3D));
    patternTypeControl_->addItem(patternTypeName(PatternType::Horizontal),
        static_cast<int>(PatternType::Horizontal));
    patternTypeControl_->addItem(patternTypeName(PatternType::Vertical),
        static_cast<int>(PatternType::Vertical));
    patternTypeControl_->addItem(patternTypeName(PatternType::Custom),
        static_cast<int>(PatternType::Custom));
    resetPatternButton_ = new QPushButton(tr("Reset to Preset"), patternControls_);
    resetPatternButton_->setObjectName(QStringLiteral("resetRadiationPatternButton"));
    patternTypeRow->addWidget(new QLabel(tr("Pattern type:"), patternControls_));
    patternTypeRow->addWidget(patternTypeControl_);
    patternTypeRow->addStretch();
    patternTypeRow->addWidget(resetPatternButton_);
    selectedPatternLayout->addLayout(patternTypeRow);

    auto* angleColumns = new QHBoxLayout;
    auto* thetaGroup = new QGroupBox(tr("Theta"), patternControls_);
    thetaGroup->setObjectName(QStringLiteral("radiationThetaGroup"));
    auto* thetaForm = new QFormLayout(thetaGroup);
    auto* phiGroup = new QGroupBox(tr("Phi"), patternControls_);
    phiGroup->setObjectName(QStringLiteral("radiationPhiGroup"));
    auto* phiForm = new QFormLayout(phiGroup);
    thetaStartControl_ = angleControl(thetaGroup, 0.0, 180.0);
    thetaStartControl_->setObjectName(QStringLiteral("radiationThetaStart"));
    thetaEndControl_ = angleControl(thetaGroup, 0.0, 180.0);
    thetaEndControl_->setObjectName(QStringLiteral("radiationThetaEnd"));
    thetaStepControl_ = angleControl(thetaGroup, 0.001, 180.0);
    thetaStepControl_->setObjectName(QStringLiteral("radiationThetaStep"));
    phiStartControl_ = angleControl(phiGroup, -360.0, 360.0);
    phiStartControl_->setObjectName(QStringLiteral("radiationPhiStart"));
    phiEndControl_ = angleControl(phiGroup, -360.0, 360.0);
    phiEndControl_->setObjectName(QStringLiteral("radiationPhiEnd"));
    phiStepControl_ = angleControl(phiGroup, 0.001, 360.0);
    phiStepControl_->setObjectName(QStringLiteral("radiationPhiStep"));
    for (auto* control : {thetaStartControl_, thetaEndControl_, thetaStepControl_,
             phiStartControl_, phiEndControl_, phiStepControl_})
        control->setMinimumWidth(110);
    thetaForm->addRow(tr("Start"), thetaStartControl_);
    thetaForm->addRow(tr("End"), thetaEndControl_);
    thetaForm->addRow(tr("Step"), thetaStepControl_);
    phiForm->addRow(tr("Start"), phiStartControl_);
    phiForm->addRow(tr("End"), phiEndControl_);
    phiForm->addRow(tr("Step"), phiStepControl_);
    angleColumns->addWidget(thetaGroup, 1);
    angleColumns->addWidget(phiGroup, 1);
    selectedPatternLayout->addLayout(angleColumns);

    applyPatternButton_ = new QPushButton(tr("Apply Pattern Changes"), patternControls_);
    applyPatternButton_->setObjectName(QStringLiteral("applyRadiationPatternButton"));
    auto* patternActions = new QHBoxLayout;
    patternActions->addStretch();
    patternActions->addWidget(applyPatternButton_);
    selectedPatternLayout->addLayout(patternActions);
    patternLayout->addWidget(patternControls_);

    validationLabel_ = new QLabel(this);
    validationLabel_->setStyleSheet(QStringLiteral("color: #b03030;"));
    validationLabel_->hide();
    auto* readinessGroup = new QGroupBox(tr("Analysis Readiness"), this);
    auto* readinessLayout = new QVBoxLayout(readinessGroup);
    readinessLabel_ = new QLabel(readinessGroup);
    readinessLabel_->setWordWrap(true);
    readinessLayout->addWidget(readinessLabel_);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addWidget(currentGroup);
    layout->addWidget(patternGroup, 1);
    layout->addWidget(validationLabel_);
    layout->addWidget(readinessGroup);

    connect(applyExecution, &QPushButton::clicked, this, [this] {
        model::ExecutionRequest request;
        request.sourceLine = setup_.executionRequest ? setup_.executionRequest->sourceLine : 0;
        emit executionChanged(executionControl_->isChecked(), request);
    });
    connect(addPattern, &QPushButton::clicked, this, [this] {
        for (auto row = 0; row < patterns_->rowCount(); ++row) {
            if (patterns_->item(row, 0)->data(SourceLineRole).toULongLong() != 0) continue;
            patterns_->selectRow(row);
            setPatternPending(true);
            return;
        }
        addPatternDraft({37, 36, 0.0, 0.0, 5.0, 10.0, 0});
    });
    connect(duplicatePattern, &QPushButton::clicked, this, [this] {
        if (patterns_->currentRow() < 0) return;
        auto pattern = patternRequest();
        pattern.sourceLine = 0;
        addPatternDraft(pattern);
    });
    connect(deletePatternButton_, &QPushButton::clicked, this, [this] {
        const auto row = patterns_->currentRow();
        if (row < 0) return;
        const auto pendingDeletion = !patternDeletionPending(row);
        for (auto column = 0; column < patterns_->columnCount(); ++column)
            patterns_->item(row, column)->setData(PendingDeletionRole, pendingDeletion);
        loadSelectedPattern();
    });
    connect(applyPatternButton_, &QPushButton::clicked, this, [this] {
        if (patterns_->currentRow() < 0) return;
        const auto row = patterns_->currentRow();
        if (patternDeletionPending(row)) {
            const auto sourceLine = patterns_->item(row, 0)->data(SourceLineRole).toULongLong();
            if (sourceLine == 0) {
                patterns_->removeRow(row);
                loadSelectedPattern();
            } else {
                emit patternDeleteRequested(sourceLine);
            }
            return;
        }
        if (thetaEndControl_->value() < thetaStartControl_->value()
            || phiEndControl_->value() < phiStartControl_->value()) {
            validationLabel_->setText(tr("Pattern end angles must not be less than their start angles."));
            validationLabel_->show();
            return;
        }
        validationLabel_->hide();
        auto pattern = patternRequest();
        pattern.sourceLine = patterns_->item(patterns_->currentRow(), 0)
            ->data(SourceLineRole).toULongLong();
        setPatternPending(false);
        emit patternChanged(pattern);
    });
    connect(patterns_, &QTableWidget::currentCellChanged, this,
        [this] { loadSelectedPattern(); });
    connect(patternTypeControl_, &QComboBox::currentIndexChanged, this, [this] {
        if (loadingPattern_) return;
        const auto typeValue = patternTypeControl_->currentData().toInt();
        if (static_cast<PatternType>(typeValue) != PatternType::Custom)
            lastPatternPreset_ = typeValue;
        applyPatternPreset(typeValue);
        setPatternPending(true);
    });
    connect(resetPatternButton_, &QPushButton::clicked, this, [this] {
        if (lastPatternPreset_ >= 0) {
            applyPatternPreset(lastPatternPreset_);
            setPatternPending(true);
        }
    });
    for (auto* control : {thetaStartControl_, thetaEndControl_, thetaStepControl_,
             phiStartControl_, phiEndControl_, phiStepControl_})
        connect(control, &QDoubleSpinBox::valueChanged, this,
            [this] {
                if (loadingPattern_) return;
                updatePatternTypeFromFields();
                updatePatternControls();
                setPatternPending(true);
            });
    connect(radiationFrequencyModeControl_, &QComboBox::currentIndexChanged,
        this, [this] {
            if (!loadingFrequency_) setFrequencyPending(true);
            updateFrequencyControls();
        });
    connect(singleFrequencyControl_, &QDoubleSpinBox::valueChanged,
        this, [this] {
            if (!loadingFrequency_) setFrequencyPending(true);
            updatePatternControls();
        });
    for (auto* control : {continuousStartControl_, continuousStopControl_,
             continuousStepControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged, this,
            [this] {
                if (!loadingFrequency_) setFrequencyPending(true);
                updatePatternControls();
            });
    }
    connect(addFrequency, &QPushButton::clicked, this,
        [this] { addSelectedFrequency(frequencyEntryControl_->value()); });
    connect(removeFrequencies, &QPushButton::clicked,
        this, [this] { removeSelectedFrequencies(); });
    connect(addAmateurBandCenters, &QPushButton::clicked,
        this, [this] { chooseAmateurBandCenters(); });
    connect(clearFrequencies, &QPushButton::clicked, this, [this] {
        selectedFrequencies_->clear();
        setFrequencyPending(true);
        updatePatternControls();
    });
    connect(pasteFrequencies, &QPushButton::clicked, this, [this] {
        bool accepted{};
        const auto text = QInputDialog::getMultiLineText(this, tr("Paste Pattern Frequencies"),
            tr("Enter MHz values separated by spaces, commas, semicolons, or new lines:"),
            {}, &accepted);
        if (!accepted) return;
        for (const auto& value : text.split(
                 QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
            bool valid{};
            const auto frequencyMHz = value.toDouble(&valid);
            if (valid) addSelectedFrequency(frequencyMHz);
        }
    });
    auto* deleteFrequency = new QShortcut(QKeySequence(Qt::Key_Delete), selectedFrequencies_);
    connect(deleteFrequency, &QShortcut::activated,
        this, [this] { removeSelectedFrequencies(); });
    auto* backspaceFrequency = new QShortcut(
        QKeySequence(Qt::Key_Backspace), selectedFrequencies_);
    connect(backspaceFrequency, &QShortcut::activated,
        this, [this] { removeSelectedFrequencies(); });
    const auto savedFrequencies = frequencySettings.value(
        QStringLiteral("analysis/patternSelectedFrequenciesMHz")).toString()
        .split(QLatin1Char(','), Qt::SkipEmptyParts);
    loadingFrequency_ = true;
    for (const auto& value : savedFrequencies) {
        bool valid{};
        const auto frequencyMHz = value.toDouble(&valid);
        if (valid) addSelectedFrequency(frequencyMHz);
    }
    loadingFrequency_ = false;
    connect(applyFrequencyButton_, &QPushButton::clicked, this, [this] {
        if (analysis::frequencyPlanPoints(
                frequencyPlan(captureFrequencySelection())).empty()) return;
        appliedFrequencySelection_ = captureFrequencySelection();
        persistFrequencySelection();
        setFrequencyPending(false);
        updatePatternControls();
    });
    updateFrequencyControls();
    frequencySelectionInitialized_ = true;
    appliedFrequencySelection_ = captureFrequencySelection();
    setFrequencyPending(false);
    updatePatternControls();
    setReadiness({tr("Model has not been checked yet.")});
}

void AnalysisRequestEditor::setData(const model::ModelSetup& setup)
{
    const auto selectedLine = patterns_->currentRow() < 0 ? std::size_t{}
        : patterns_->item(patterns_->currentRow(), 0)->data(SourceLineRole).toULongLong();
    setup_ = setup;
    executionControl_->setChecked(setup_.executionRequest.has_value());
    const QSignalBlocker blocker(patterns_);
    patterns_->setRowCount(static_cast<int>(setup_.radiationPatterns.size()));
    for (auto row = 0; row < static_cast<int>(setup_.radiationPatterns.size()); ++row)
        updatePatternTableRow(row, setup_.radiationPatterns[row]);
    const auto frequencies = modelFrequencies();
    auto initializedFrequencyDefaults = false;
    loadingFrequency_ = true;
    if (!frequencies.empty()) {
        if (!singleFrequencyInitialized_) {
            singleFrequencyControl_->setValue(frequencies[frequencies.size() / 2]);
            singleFrequencyInitialized_ = true;
            initializedFrequencyDefaults = true;
        }
        if (!selectedFrequenciesInitialized_) {
            addSelectedFrequency(frequencies.front());
            if (frequencies.size() > 2)
                addSelectedFrequency(frequencies[frequencies.size() / 2]);
            if (frequencies.size() > 1) addSelectedFrequency(frequencies.back());
            selectedFrequenciesInitialized_ = true;
            initializedFrequencyDefaults = true;
        }
    }
    loadingFrequency_ = false;
    if (initializedFrequencyDefaults) {
        appliedFrequencySelection_ = captureFrequencySelection();
        persistFrequencySelection();
        setFrequencyPending(false);
    }
    selectPattern(selectedLine);
    updateFrequencyControls();
    updatePatternControls();
}

void AnalysisRequestEditor::selectPattern(std::size_t sourceLine)
{
    for (auto row = 0; row < patterns_->rowCount(); ++row) {
        if (patterns_->item(row, 0)->data(SourceLineRole).toULongLong() != sourceLine) continue;
        patterns_->selectRow(row);
        loadSelectedPattern();
        return;
    }
    patterns_->clearSelection();
    loadSelectedPattern();
}

auto AnalysisRequestEditor::hasPendingEdits() const noexcept -> bool
{
    return patternPending_ || frequencyPending_;
}

void AnalysisRequestEditor::discardPendingEdits()
{
    const auto appliedFrequencies = appliedFrequencySelection_;
    setData(setup_);
    restoreFrequencySelection(appliedFrequencies);
    setPatternPending(false);
    setFrequencyPending(false);
}

void AnalysisRequestEditor::addPatternDraft(const model::RadiationPatternRequest& pattern)
{
    for (auto row = 0; row < patterns_->rowCount(); ++row) {
        if (patterns_->item(row, 0)->data(SourceLineRole).toULongLong() != 0) continue;
        patterns_->selectRow(row);
        loadSelectedPattern();
        return;
    }
    const auto row = patterns_->rowCount();
    patterns_->insertRow(row);
    updatePatternTableRow(row, pattern);
    patterns_->selectRow(row);
    loadSelectedPattern();
    setPatternPending(true);
}

void AnalysisRequestEditor::updatePatternTableRow(
    int row, const model::RadiationPatternRequest& pattern)
{
    for (auto column = 0; column < patterns_->columnCount(); ++column) {
        auto* item = new QTableWidgetItem;
        item->setData(SourceLineRole, static_cast<qulonglong>(pattern.sourceLine));
        item->setData(Qt::UserRole + 1, QVariant::fromValue(pattern.thetaCount));
        item->setData(Qt::UserRole + 2, QVariant::fromValue(pattern.phiCount));
        item->setData(Qt::UserRole + 3, pattern.thetaStart);
        item->setData(Qt::UserRole + 4, pattern.phiStart);
        item->setData(Qt::UserRole + 5, pattern.thetaStep);
        item->setData(Qt::UserRole + 6, pattern.phiStep);
        item->setData(PendingDeletionRole, false);
        patterns_->setItem(row, column, item);
    }
    updatePatternTableRowText(row, pattern, false);
}

void AnalysisRequestEditor::updatePatternTableRowText(
    int row, const model::RadiationPatternRequest& pattern, bool draft, bool pendingDeletion)
{
    auto type = patternTypeName(patternType(pattern));
    if (pendingDeletion) type += tr(" — Pending deletion");
    else if (draft) type += tr(" — Draft");
    const QStringList cells{type,
        rangeSummary(pattern.thetaStart, pattern.thetaCount, pattern.thetaStep),
        rangeSummary(pattern.phiStart, pattern.phiCount, pattern.phiStep),
        QString::number(static_cast<qlonglong>(pattern.thetaCount) * pattern.phiCount)};
    for (auto column = 0; column < cells.size(); ++column) {
        patterns_->item(row, column)->setText(cells[column]);
        patterns_->item(row, column)->setForeground(pendingDeletion
            ? palette().brush(QPalette::Disabled, QPalette::Text) : QBrush{});
    }
}

auto AnalysisRequestEditor::patternFromTableRow(int row) const
    -> model::RadiationPatternRequest
{
    const auto* item = patterns_->item(row, 0);
    model::RadiationPatternRequest pattern;
    pattern.sourceLine = item->data(SourceLineRole).toULongLong();
    pattern.thetaCount = item->data(Qt::UserRole + 1).toInt();
    pattern.phiCount = item->data(Qt::UserRole + 2).toInt();
    pattern.thetaStart = item->data(Qt::UserRole + 3).toDouble();
    pattern.phiStart = item->data(Qt::UserRole + 4).toDouble();
    pattern.thetaStep = item->data(Qt::UserRole + 5).toDouble();
    pattern.phiStep = item->data(Qt::UserRole + 6).toDouble();
    return pattern;
}

auto AnalysisRequestEditor::patternDeletionPending(int row) const -> bool
{
    return row >= 0 && row < patterns_->rowCount()
        && patterns_->item(row, 0)->data(PendingDeletionRole).toBool();
}

void AnalysisRequestEditor::refreshPatternPendingState()
{
    const auto deletionPending = [this] {
        for (auto row = 0; row < patterns_->rowCount(); ++row)
            if (patternDeletionPending(row)) return true;
        return false;
    }();
    patternPending_ = patternPending_ || deletionPending;
    setPendingEditIndicator(applyPatternButton_, patternPending_);
}

void AnalysisRequestEditor::applyPatternPreset(int typeValue)
{
    const auto type = static_cast<PatternType>(typeValue);
    if (type == PatternType::Custom) {
        resetPatternButton_->setEnabled(lastPatternPreset_ >= 0);
        updatePatternControls();
        return;
    }
    loadingPattern_ = true;
    patternTypeControl_->setCurrentIndex(patternTypeControl_->findData(typeValue));
    switch (type) {
    case PatternType::Full3D:
        thetaStartControl_->setValue(0); thetaEndControl_->setValue(180); thetaStepControl_->setValue(5);
        phiStartControl_->setValue(0); phiEndControl_->setValue(350); phiStepControl_->setValue(10); break;
    case PatternType::Horizontal:
        thetaStartControl_->setValue(90); thetaEndControl_->setValue(90); thetaStepControl_->setValue(1);
        phiStartControl_->setValue(0); phiEndControl_->setValue(359); phiStepControl_->setValue(1); break;
    case PatternType::Vertical:
        thetaStartControl_->setValue(0); thetaEndControl_->setValue(180); thetaStepControl_->setValue(1);
        phiStartControl_->setValue(0); phiEndControl_->setValue(180); phiStepControl_->setValue(180); break;
    case PatternType::Custom: break;
    }
    loadingPattern_ = false;
    resetPatternButton_->setEnabled(true);
    updatePatternControls();
}

void AnalysisRequestEditor::loadSelectedPattern()
{
    for (auto patternRow = 0; patternRow < patterns_->rowCount(); ++patternRow)
        updatePatternTableRowText(patternRow, patternFromTableRow(patternRow), false,
            patternDeletionPending(patternRow));
    const auto row = patterns_->currentRow();
    patternControls_->setEnabled(row >= 0);
    if (row < 0) {
        patternPending_ = false;
        refreshPatternPendingState();
        return;
    }
    const auto* item = patterns_->item(row, 0);
    const auto pattern = patternFromTableRow(row);
    const auto type = patternType(pattern);
    lastPatternPreset_ = type == PatternType::Custom ? -1 : static_cast<int>(type);
    loadingPattern_ = true;
    patternTypeControl_->setCurrentIndex(patternTypeControl_->findData(
        static_cast<int>(type)));
    thetaStartControl_->setValue(pattern.thetaStart);
    thetaEndControl_->setValue(pattern.thetaStart + (pattern.thetaCount - 1) * pattern.thetaStep);
    thetaStepControl_->setValue(std::max(pattern.thetaStep, 0.001));
    phiStartControl_->setValue(pattern.phiStart);
    phiEndControl_->setValue(pattern.phiStart + (pattern.phiCount - 1) * pattern.phiStep);
    phiStepControl_->setValue(std::max(pattern.phiStep, 0.001));
    loadingPattern_ = false;
    resetPatternButton_->setEnabled(lastPatternPreset_ >= 0);
    const auto pendingDeletion = patternDeletionPending(row);
    for (auto* control : {thetaStartControl_, thetaEndControl_, thetaStepControl_,
             phiStartControl_, phiEndControl_, phiStepControl_})
        control->setEnabled(!pendingDeletion);
    patternTypeControl_->setEnabled(!pendingDeletion);
    resetPatternButton_->setEnabled(!pendingDeletion && lastPatternPreset_ >= 0);
    deletePatternButton_->setText(pendingDeletion ? tr("Restore Pattern") : tr("Delete Pattern"));
    patternPending_ = item->data(SourceLineRole).toULongLong() == 0 || pendingDeletion;
    refreshPatternPendingState();
    updatePatternControls();
}

void AnalysisRequestEditor::updatePatternTypeFromFields()
{
    const auto type = patternType(patternRequest());
    const QSignalBlocker blocker(patternTypeControl_);
    patternTypeControl_->setCurrentIndex(patternTypeControl_->findData(static_cast<int>(type)));
    if (type != PatternType::Custom) lastPatternPreset_ = static_cast<int>(type);
    resetPatternButton_->setEnabled(lastPatternPreset_ >= 0);
}

auto AnalysisRequestEditor::patternRequest() const -> model::RadiationPatternRequest
{
    model::RadiationPatternRequest pattern;
    pattern.thetaStart = thetaStartControl_->value();
    pattern.thetaStep = thetaStepControl_->value();
    pattern.thetaCount = angularCount(pattern.thetaStart, thetaEndControl_->value(), pattern.thetaStep);
    pattern.phiStart = phiStartControl_->value();
    pattern.phiStep = phiStepControl_->value();
    pattern.phiCount = angularCount(pattern.phiStart, phiEndControl_->value(), pattern.phiStep);
    if (pattern.thetaCount == 1) pattern.thetaStep = 0.0;
    if (pattern.phiCount == 1) pattern.phiStep = 0.0;
    return pattern;
}

auto AnalysisRequestEditor::angularCount(double start, double end, double step) const -> int
{
    return std::max(1, static_cast<int>(std::floor((end - start) / step + 1.0e-9)) + 1);
}

auto AnalysisRequestEditor::radiationFrequencyPlan() const -> analysis::FrequencyPlan
{
    return frequencyPlan(appliedFrequencySelection_);
}

auto AnalysisRequestEditor::frequencyPlan(
    const FrequencySelectionState& state) const -> analysis::FrequencyPlan
{
    const auto mode = static_cast<PatternFrequencyMode>(state.mode);
    switch (mode) {
    case PatternFrequencyMode::ModelSweep:
        return {analysis::FrequencyPlanMode::ModelSweep, modelFrequencies(), {}};
    case PatternFrequencyMode::Single:
        return {analysis::FrequencyPlanMode::Explicit, {state.singleMHz}, {}};
    case PatternFrequencyMode::Selected:
        return {analysis::FrequencyPlanMode::Explicit, state.selectedMHz, {}};
    case PatternFrequencyMode::Continuous:
        return {analysis::FrequencyPlanMode::Explicit, {},
            {{state.continuousStartMHz, state.continuousStopMHz,
                state.continuousStepMHz}}};
    }
    return {};
}

void AnalysisRequestEditor::updatePatternControls()
{
    const auto draftFrequencyPlan = frequencyPlan(captureFrequencySelection());
    const auto draftFrequencyPoints = analysis::frequencyPlanPoints(draftFrequencyPlan);
    applyFrequencyButton_->setEnabled(!draftFrequencyPoints.empty());
    const auto radiationFrequencyCount = static_cast<int>((frequencyPending_
        ? draftFrequencyPoints
        : analysis::frequencyPlanPoints(radiationFrequencyPlan())).size());
    auto anglesPerFrequency = qlonglong{};
    for (const auto& pattern : setup_.radiationPatterns)
        anglesPerFrequency += static_cast<qlonglong>(pattern.thetaCount) * pattern.phiCount;
    if (patterns_->currentRow() >= 0
        && patterns_->item(patterns_->currentRow(), 0)->data(SourceLineRole).toULongLong() == 0) {
        const auto draft = patternRequest();
        anglesPerFrequency += static_cast<qlonglong>(draft.thetaCount) * draft.phiCount;
    }
    sweepCostLabel_->setText(tr(
        "%1 RP request(s) · %2 radiation frequency calculation(s) · approximately %3 samples.")
        .arg(patterns_->rowCount()).arg(radiationFrequencyCount)
        .arg(anglesPerFrequency * radiationFrequencyCount));
}

void AnalysisRequestEditor::setPatternPending(bool pending)
{
    patternPending_ = pending;
    const auto row = patterns_->currentRow();
    if (row >= 0 && !patternDeletionPending(row))
        updatePatternTableRowText(row,
            pending ? patternRequest() : patternFromTableRow(row), pending);
    refreshPatternPendingState();
}

void AnalysisRequestEditor::setFrequencyPending(bool pending)
{
    frequencyPending_ = pending;
    setPendingEditIndicator(applyFrequencyButton_, pending);
}

void AnalysisRequestEditor::updateFrequencyControls()
{
    const auto page = radiationFrequencyModeControl_->currentData().toInt();
    radiationFrequencyPages_->setCurrentIndex(page);
    updatePatternControls();
}

void AnalysisRequestEditor::addSelectedFrequency(double frequencyMHz)
{
    if (!std::isfinite(frequencyMHz) || frequencyMHz <= 0.0) return;
    std::vector<double> frequencies;
    frequencies.reserve(static_cast<std::size_t>(selectedFrequencies_->count()) + 1);
    for (auto row = 0; row < selectedFrequencies_->count(); ++row)
        frequencies.push_back(selectedFrequencies_->item(row)->data(Qt::UserRole).toDouble());
    frequencies.push_back(frequencyMHz);
    std::ranges::sort(frequencies);
    const auto duplicates = std::ranges::unique(frequencies);
    frequencies.erase(duplicates.begin(), duplicates.end());
    const QSignalBlocker blocker(selectedFrequencies_);
    selectedFrequencies_->clear();
    for (const auto frequency : frequencies) {
        auto* item = new QListWidgetItem(formatDecimal(frequency), selectedFrequencies_);
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(tr("%1 MHz").arg(formatDecimal(frequency)));
        item->setData(Qt::UserRole, frequency);
    }
    if (!loadingFrequency_) setFrequencyPending(true);
    updatePatternControls();
}

void AnalysisRequestEditor::removeSelectedFrequencies()
{
    const auto selected = selectedFrequencies_->selectedItems();
    for (auto* item : selected) delete item;
    if (selected.empty()) return;
    setFrequencyPending(true);
    updatePatternControls();
}

void AnalysisRequestEditor::chooseAmateurBandCenters()
{
    const auto& presets = analysis::amateurBandPresets();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("radiationAmateurBandCenterDialog"));
    dialog.setWindowTitle(tr("Add Amateur Band Centers"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(tr(
        "Select one or more bands. This adds one representative center frequency per band; "
        "it does not calculate the entire band."), &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* bandLayout = new QGridLayout;
    std::vector<QCheckBox*> checks;
    checks.reserve(presets.size());
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto& preset = presets[index];
        const auto centerMHz = (preset.startMHz + preset.endMHz) / 2.0;
        auto* check = new QCheckBox(tr("%1  (%2 MHz)")
            .arg(QString::fromUtf8(preset.name.data(),
                static_cast<qsizetype>(preset.name.size())))
            .arg(centerMHz, 0, 'f', 3), &dialog);
        check->setObjectName(QStringLiteral("radiationAmateurBandCenterCheck%1").arg(index));
        checks.push_back(check);
        bandLayout->addWidget(check, static_cast<int>(index % 6),
            static_cast<int>(index / 6));
    }
    layout->addLayout(bandLayout);

    auto* selectionRow = new QHBoxLayout;
    auto* selectAll = new QPushButton(tr("Select All"), &dialog);
    auto* clearSelection = new QPushButton(tr("Clear"), &dialog);
    auto* summary = new QLabel(&dialog);
    summary->setObjectName(QStringLiteral("radiationAmateurBandCenterSummary"));
    selectionRow->addWidget(selectAll);
    selectionRow->addWidget(clearSelection);
    selectionRow->addStretch();
    selectionRow->addWidget(summary);
    layout->addLayout(selectionRow);

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* addSelected = dialogButtons->addButton(
        tr("Add Selected Centers"), QDialogButtonBox::AcceptRole);
    addSelected->setObjectName(QStringLiteral("radiationAddSelectedBandCenters"));
    layout->addWidget(dialogButtons);
    const auto updateSummary = [&checks, summary, addSelected] {
        const auto count = static_cast<int>(std::ranges::count_if(checks,
            [](const auto* check) { return check->isChecked(); }));
        summary->setText(QObject::tr("%1 center frequency point(s)").arg(count));
        addSelected->setEnabled(count > 0);
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

    for (std::size_t index = 0; index < checks.size(); ++index) {
        if (!checks[index]->isChecked()) continue;
        addSelectedFrequency((presets[index].startMHz + presets[index].endMHz) / 2.0);
    }
}

void AnalysisRequestEditor::persistFrequencySelection() const
{
    if (!frequencySelectionInitialized_) return;
    QSettings settings;
    settings.setValue(QStringLiteral("analysis/patternFrequencyMode"),
        radiationFrequencyModeControl_->currentData().toInt());
    settings.setValue(QStringLiteral("analysis/patternSingleFrequencyMHz"),
        singleFrequencyControl_->value());
    settings.setValue(QStringLiteral("analysis/patternContinuousStartMHz"),
        continuousStartControl_->value());
    settings.setValue(QStringLiteral("analysis/patternContinuousStopMHz"),
        continuousStopControl_->value());
    settings.setValue(QStringLiteral("analysis/patternContinuousStepMHz"),
        continuousStepControl_->value());
    QStringList frequencies;
    for (auto row = 0; row < selectedFrequencies_->count(); ++row)
        frequencies.append(QString::number(
            selectedFrequencies_->item(row)->data(Qt::UserRole).toDouble(), 'g', 15));
    settings.setValue(QStringLiteral("analysis/patternSelectedFrequenciesMHz"),
        frequencies.join(QLatin1Char(',')));
}

auto AnalysisRequestEditor::captureFrequencySelection() const
    -> FrequencySelectionState
{
    FrequencySelectionState state;
    state.mode = radiationFrequencyModeControl_->currentData().toInt();
    state.singleMHz = singleFrequencyControl_->value();
    state.selectedMHz.reserve(static_cast<std::size_t>(selectedFrequencies_->count()));
    for (auto row = 0; row < selectedFrequencies_->count(); ++row)
        state.selectedMHz.push_back(
            selectedFrequencies_->item(row)->data(Qt::UserRole).toDouble());
    state.continuousStartMHz = continuousStartControl_->value();
    state.continuousStopMHz = continuousStopControl_->value();
    state.continuousStepMHz = continuousStepControl_->value();
    return state;
}

void AnalysisRequestEditor::restoreFrequencySelection(
    const FrequencySelectionState& state)
{
    loadingFrequency_ = true;
    const auto modeIndex = radiationFrequencyModeControl_->findData(state.mode);
    if (modeIndex >= 0) radiationFrequencyModeControl_->setCurrentIndex(modeIndex);
    singleFrequencyControl_->setValue(state.singleMHz);
    continuousStartControl_->setValue(state.continuousStartMHz);
    continuousStopControl_->setValue(state.continuousStopMHz);
    continuousStepControl_->setValue(state.continuousStepMHz);
    selectedFrequencies_->clear();
    for (const auto frequencyMHz : state.selectedMHz)
        addSelectedFrequency(frequencyMHz);
    loadingFrequency_ = false;
    radiationFrequencyPages_->setCurrentIndex(state.mode);
    updatePatternControls();
}

auto AnalysisRequestEditor::modelFrequencies() const -> std::vector<double>
{
    std::vector<double> frequencies;
    if (!setup_.frequency) return frequencies;
    frequencies.reserve(static_cast<std::size_t>(std::max(1, setup_.frequency->count)));
    for (auto index = 0; index < std::max(1, setup_.frequency->count); ++index) {
        frequencies.push_back(setup_.frequency->steppingMode == 1
            ? setup_.frequency->startMHz * std::pow(setup_.frequency->step, index)
            : setup_.frequency->startMHz + index * setup_.frequency->step);
    }
    return frequencies;
}

void AnalysisRequestEditor::setReadiness(const QStringList& blockingReasons)
{
    if (blockingReasons.empty()) {
        readinessLabel_->setText(tr("✓ Model deck and solver configuration are ready for execution."));
        readinessLabel_->setStyleSheet(QStringLiteral("color: #247a35;"));
        return;
    }
    QStringList lines{tr("Not ready:")};
    for (const auto& reason : blockingReasons) lines.append(QStringLiteral("• %1").arg(reason));
    readinessLabel_->setText(lines.join(QLatin1Char('\n')));
    readinessLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
}

}
