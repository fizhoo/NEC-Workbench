#include "ui/analysis/AnalysisRequestEditor.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {

auto angleControl(QWidget* parent, double minimum, double maximum) -> QDoubleSpinBox*
{
    auto* control = new QDoubleSpinBox(parent);
    control->setDecimals(3);
    control->setRange(minimum, maximum);
    control->setSuffix(QObject::tr("°"));
    control->setKeyboardTracking(false);
    return control;
}

}

AnalysisRequestEditor::AnalysisRequestEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    auto* heading = new QLabel(tr("Requested Results"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);
    auto* description = new QLabel(tr("Choose the calculations written into the NEC deck before launching a solver."), this);
    description->setWordWrap(true);

    auto* currentGroup = new QGroupBox(tr("Currents and Feed Impedance"), this);
    auto* currentLayout = new QVBoxLayout(currentGroup);
    executionControl_ = new QCheckBox(tr("Request structure currents and feed impedance (XQ)"), currentGroup);
    currentLayout->addWidget(executionControl_);

    auto* patternGroup = new QGroupBox(tr("Far-Field Radiation Pattern"), this);
    auto* patternLayout = new QVBoxLayout(patternGroup);
    patternControl_ = new QCheckBox(tr("Request a normal far-field pattern (RP)"), patternGroup);
    patternControls_ = new QWidget(patternGroup);
    auto* patternForm = new QFormLayout(patternControls_);
    thetaStartControl_ = angleControl(patternControls_, 0.0, 180.0);
    thetaEndControl_ = angleControl(patternControls_, 0.0, 180.0);
    thetaStepControl_ = angleControl(patternControls_, 0.001, 180.0);
    phiStartControl_ = angleControl(patternControls_, -360.0, 360.0);
    phiEndControl_ = angleControl(patternControls_, -360.0, 360.0);
    phiStepControl_ = angleControl(patternControls_, 0.001, 360.0);
    thetaEndControl_->setValue(180.0);
    thetaStepControl_->setValue(5.0);
    phiEndControl_->setValue(350.0);
    phiStepControl_->setValue(10.0);
    patternForm->addRow(tr("Theta start"), thetaStartControl_);
    patternForm->addRow(tr("Theta end"), thetaEndControl_);
    patternForm->addRow(tr("Theta step"), thetaStepControl_);
    patternForm->addRow(tr("Phi start"), phiStartControl_);
    patternForm->addRow(tr("Phi end"), phiEndControl_);
    patternForm->addRow(tr("Phi step"), phiStepControl_);
    auto* patternPresets = new QHBoxLayout;
    auto* cutPreset = new QPushButton(tr("2D Elevation Cut"), patternControls_);
    auto* spherePreset = new QPushButton(tr("Full 3D Pattern"), patternControls_);
    patternPresets->addWidget(cutPreset);
    patternPresets->addWidget(spherePreset);
    patternPresets->addStretch();
    patternForm->addRow(tr("Preset"), patternPresets);
    patternLayout->addWidget(patternControl_);
    patternLayout->addWidget(patternControls_);

    auto* applyButton = new QPushButton(tr("Apply Requested Results"), this);
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
    layout->addWidget(patternGroup);
    layout->addWidget(applyButton);
    layout->addWidget(validationLabel_);
    layout->addWidget(readinessGroup);
    layout->addStretch();

    connect(patternControl_, &QCheckBox::toggled, this, [this] { updatePatternControls(); });
    connect(cutPreset, &QPushButton::clicked, this, [this] {
        thetaStartControl_->setValue(0.0); thetaEndControl_->setValue(180.0); thetaStepControl_->setValue(2.0);
        phiStartControl_->setValue(0.0); phiEndControl_->setValue(180.0); phiStepControl_->setValue(180.0);
    });
    connect(spherePreset, &QPushButton::clicked, this, [this] {
        thetaStartControl_->setValue(0.0); thetaEndControl_->setValue(180.0); thetaStepControl_->setValue(5.0);
        phiStartControl_->setValue(0.0); phiEndControl_->setValue(350.0); phiStepControl_->setValue(10.0);
    });
    connect(applyButton, &QPushButton::clicked, this, [this] {
        if (patternControl_->isChecked()
            && (thetaEndControl_->value() < thetaStartControl_->value()
                || phiEndControl_->value() < phiStartControl_->value())) {
            validationLabel_->setText(tr("Pattern end angles must not be less than their start angles."));
            validationLabel_->show();
            return;
        }
        validationLabel_->hide();
        model::ExecutionRequest execution;
        execution.sourceLine = setup_.executionRequest ? setup_.executionRequest->sourceLine : 0;
        auto pattern = patternRequest();
        pattern.sourceLine = setup_.radiationPattern ? setup_.radiationPattern->sourceLine : 0;
        emit requestsChanged(executionControl_->isChecked(), execution,
            patternControl_->isChecked(), pattern);
    });
    updatePatternControls();
    setReadiness({tr("Model has not been checked yet.")});
}

void AnalysisRequestEditor::setData(const model::ModelSetup& setup)
{
    setup_ = setup;
    executionControl_->setChecked(setup_.executionRequest.has_value());
    patternControl_->setChecked(setup_.radiationPattern.has_value());
    if (setup_.radiationPattern) {
        const auto& pattern = *setup_.radiationPattern;
        thetaStartControl_->setValue(pattern.thetaStart);
        thetaStepControl_->setValue(std::max(pattern.thetaStep, 0.001));
        thetaEndControl_->setValue(pattern.thetaStart + (pattern.thetaCount - 1) * pattern.thetaStep);
        phiStartControl_->setValue(pattern.phiStart);
        phiStepControl_->setValue(std::max(pattern.phiStep, 0.001));
        phiEndControl_->setValue(pattern.phiStart + (pattern.phiCount - 1) * pattern.phiStep);
    }
    updatePatternControls();
}

void AnalysisRequestEditor::setReadiness(const QStringList& blockingReasons)
{
    if (blockingReasons.empty()) {
        readinessLabel_->setText(tr("✓ Model deck and solver configuration are ready for execution."));
        readinessLabel_->setStyleSheet(QStringLiteral("color: #247a35;"));
        return;
    }
    QStringList lines{tr("Not ready:")};
    for (const auto& reason : blockingReasons) {
        lines.append(QStringLiteral("• %1").arg(reason));
    }
    readinessLabel_->setText(lines.join(QLatin1Char('\n')));
    readinessLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
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
    if (pattern.phiCount == 1) {
        pattern.phiStep = 0.0;
    }
    return pattern;
}

auto AnalysisRequestEditor::angularCount(double start, double end, double step) const -> int
{
    return std::max(1, static_cast<int>(std::floor((end - start) / step + 1.0e-9)) + 1);
}

void AnalysisRequestEditor::updatePatternControls()
{
    patternControls_->setEnabled(patternControl_->isChecked());
}

}
