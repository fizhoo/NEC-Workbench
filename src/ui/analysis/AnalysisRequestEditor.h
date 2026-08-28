#pragma once

#include "model/ModelSetup.h"

#include <QWidget>
#include <QStringList>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

namespace necwb::ui {

class AnalysisRequestEditor final : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisRequestEditor(QWidget* parent = nullptr);

    void setData(const model::ModelSetup& setup);
    void setReadiness(const QStringList& blockingReasons);

signals:
    void requestsChanged(bool executionEnabled, model::ExecutionRequest execution,
        bool patternEnabled, model::RadiationPatternRequest pattern);

private:
    [[nodiscard]] auto patternRequest() const -> model::RadiationPatternRequest;
    [[nodiscard]] auto angularCount(double start, double end, double step) const -> int;
    void updatePatternControls();

    model::ModelSetup setup_;
    QCheckBox* executionControl_{};
    QCheckBox* patternControl_{};
    QDoubleSpinBox* thetaStartControl_{};
    QDoubleSpinBox* thetaEndControl_{};
    QDoubleSpinBox* thetaStepControl_{};
    QDoubleSpinBox* phiStartControl_{};
    QDoubleSpinBox* phiEndControl_{};
    QDoubleSpinBox* phiStepControl_{};
    QLabel* validationLabel_{};
    QLabel* readinessLabel_{};
    QWidget* patternControls_{};
};

}
