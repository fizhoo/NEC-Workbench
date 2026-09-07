#include "ui/analysis/AnalysisRequestEditor.h"

#include "ui/DisplayFormat.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {
constexpr auto SourceLineRole = Qt::UserRole;

enum class PatternType { Full3D, Horizontal, Vertical, Custom };

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

    auto* frequencyPolicyGroup = new QGroupBox(tr("Radiation Frequencies for All Patterns"), patternGroup);
    frequencyPolicyGroup->setObjectName(QStringLiteral("radiationFrequencyPolicyGroup"));
    auto* frequencyPolicyLayout = new QVBoxLayout(frequencyPolicyGroup);
    auto* frequencyPolicyRow = new QHBoxLayout;
    radiationSweepControl_ = new QComboBox(frequencyPolicyGroup);
    radiationSweepControl_->setObjectName(QStringLiteral("radiationSweepMode"));
    radiationSweepControl_->addItem(tr("Center frequency only (recommended)"),
        static_cast<int>(analysis::RadiationSweepMode::CenterFrequencyOnly));
    radiationSweepControl_->addItem(tr("Start, center, and end"),
        static_cast<int>(analysis::RadiationSweepMode::RepresentativeFrequencies));
    radiationSweepControl_->addItem(tr("Every frequency"),
        static_cast<int>(analysis::RadiationSweepMode::EveryFrequency));
    const auto savedMode = QSettings{}.value(QStringLiteral("analysis/radiationSweepMode"),
        static_cast<int>(analysis::RadiationSweepMode::CenterFrequencyOnly)).toInt();
    radiationSweepControl_->setCurrentIndex(
        std::max(0, radiationSweepControl_->findData(savedMode)));
    frequencyPolicyRow->addWidget(new QLabel(tr("Run every RP request at:"), frequencyPolicyGroup));
    frequencyPolicyRow->addWidget(radiationSweepControl_);
    frequencyPolicyRow->addStretch();
    sweepCostLabel_ = new QLabel(frequencyPolicyGroup);
    sweepCostLabel_->setWordWrap(true);
    frequencyPolicyLayout->addLayout(frequencyPolicyRow);
    frequencyPolicyLayout->addWidget(sweepCostLabel_);
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
    auto* deletePattern = new QPushButton(tr("Delete"), patternGroup);
    patternButtons->addWidget(addPattern);
    patternButtons->addWidget(duplicatePattern);
    patternButtons->addWidget(deletePattern);
    patternButtons->addStretch();
    patternLayout->addLayout(patternButtons);

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

    auto* applyPattern = new QPushButton(tr("Apply Selected Pattern"), patternControls_);
    applyPattern->setObjectName(QStringLiteral("applyRadiationPatternButton"));
    auto* patternActions = new QHBoxLayout;
    patternActions->addStretch();
    patternActions->addWidget(applyPattern);
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
    connect(deletePattern, &QPushButton::clicked, this, [this] {
        const auto row = patterns_->currentRow();
        if (row < 0) return;
        const auto sourceLine = patterns_->item(row, 0)->data(SourceLineRole).toULongLong();
        if (sourceLine == 0) patterns_->removeRow(row);
        else emit patternDeleteRequested(sourceLine);
        loadSelectedPattern();
    });
    connect(applyPattern, &QPushButton::clicked, this, [this] {
        if (patterns_->currentRow() < 0) return;
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
    });
    connect(resetPatternButton_, &QPushButton::clicked, this, [this] {
        if (lastPatternPreset_ >= 0) applyPatternPreset(lastPatternPreset_);
    });
    for (auto* control : {thetaStartControl_, thetaEndControl_, thetaStepControl_,
             phiStartControl_, phiEndControl_, phiStepControl_})
        connect(control, &QDoubleSpinBox::valueChanged, this,
            [this] {
                if (loadingPattern_) return;
                updatePatternTypeFromFields();
                updatePatternControls();
            });
    connect(radiationSweepControl_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings{}.setValue(QStringLiteral("analysis/radiationSweepMode"),
            radiationSweepControl_->currentData().toInt());
        updatePatternControls();
    });
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
    selectPattern(selectedLine);
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
}

void AnalysisRequestEditor::updatePatternTableRow(
    int row, const model::RadiationPatternRequest& pattern)
{
    const QStringList cells{patternTypeName(patternType(pattern)),
        rangeSummary(pattern.thetaStart, pattern.thetaCount, pattern.thetaStep),
        rangeSummary(pattern.phiStart, pattern.phiCount, pattern.phiStep),
        QString::number(static_cast<qlonglong>(pattern.thetaCount) * pattern.phiCount)};
    for (auto column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(cells[column]);
        item->setData(SourceLineRole, static_cast<qulonglong>(pattern.sourceLine));
        item->setData(Qt::UserRole + 1, QVariant::fromValue(pattern.thetaCount));
        item->setData(Qt::UserRole + 2, QVariant::fromValue(pattern.phiCount));
        item->setData(Qt::UserRole + 3, pattern.thetaStart);
        item->setData(Qt::UserRole + 4, pattern.phiStart);
        item->setData(Qt::UserRole + 5, pattern.thetaStep);
        item->setData(Qt::UserRole + 6, pattern.phiStep);
        patterns_->setItem(row, column, item);
    }
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
    const auto row = patterns_->currentRow();
    patternControls_->setEnabled(row >= 0);
    if (row < 0) return;
    const auto* item = patterns_->item(row, 0);
    model::RadiationPatternRequest pattern;
    pattern.thetaCount = item->data(Qt::UserRole + 1).toInt();
    pattern.phiCount = item->data(Qt::UserRole + 2).toInt();
    pattern.thetaStart = item->data(Qt::UserRole + 3).toDouble();
    pattern.phiStart = item->data(Qt::UserRole + 4).toDouble();
    pattern.thetaStep = item->data(Qt::UserRole + 5).toDouble();
    pattern.phiStep = item->data(Qt::UserRole + 6).toDouble();
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

auto AnalysisRequestEditor::radiationSweepMode() const -> analysis::RadiationSweepMode
{
    return static_cast<analysis::RadiationSweepMode>(
        radiationSweepControl_->currentData().toInt());
}

void AnalysisRequestEditor::updatePatternControls()
{
    const auto frequencyCount = setup_.frequency ? std::max(1, setup_.frequency->count) : 1;
    auto radiationFrequencyCount = 1;
    if (frequencyCount > 1) {
        switch (radiationSweepMode()) {
        case analysis::RadiationSweepMode::CenterFrequencyOnly: radiationFrequencyCount = 1; break;
        case analysis::RadiationSweepMode::RepresentativeFrequencies:
            radiationFrequencyCount = std::min(3, frequencyCount); break;
        case analysis::RadiationSweepMode::EveryFrequency:
            radiationFrequencyCount = frequencyCount; break;
        }
    }
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
