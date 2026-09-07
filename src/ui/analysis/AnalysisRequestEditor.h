#pragma once

#include "analysis/SolverInput.h"
#include "model/ModelSetup.h"

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace necwb::ui {

class AnalysisRequestEditor final : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisRequestEditor(QWidget* parent = nullptr);

    void setData(const model::ModelSetup& setup);
    void selectPattern(std::size_t sourceLine);
    void setReadiness(const QStringList& blockingReasons);
    [[nodiscard]] auto radiationSweepMode() const -> analysis::RadiationSweepMode;

signals:
    void executionChanged(bool enabled, model::ExecutionRequest execution);
    void patternChanged(model::RadiationPatternRequest pattern);
    void patternDeleteRequested(std::size_t sourceLine);

private:
    [[nodiscard]] auto patternRequest() const -> model::RadiationPatternRequest;
    [[nodiscard]] auto angularCount(double start, double end, double step) const -> int;
    void addPatternDraft(const model::RadiationPatternRequest& pattern);
    void applyPatternPreset(int typeValue);
    void loadSelectedPattern();
    void updatePatternTypeFromFields();
    void updatePatternControls();
    void updatePatternTableRow(int row, const model::RadiationPatternRequest& pattern);

    model::ModelSetup setup_;
    QCheckBox* executionControl_{};
    QTableWidget* patterns_{};
    QComboBox* patternTypeControl_{};
    QDoubleSpinBox* thetaStartControl_{};
    QDoubleSpinBox* thetaEndControl_{};
    QDoubleSpinBox* thetaStepControl_{};
    QDoubleSpinBox* phiStartControl_{};
    QDoubleSpinBox* phiEndControl_{};
    QDoubleSpinBox* phiStepControl_{};
    QComboBox* radiationSweepControl_{};
    QLabel* validationLabel_{};
    QLabel* readinessLabel_{};
    QLabel* sweepCostLabel_{};
    QWidget* patternControls_{};
    QPushButton* resetPatternButton_{};
    int lastPatternPreset_{-1};
    bool loadingPattern_{};
};

}
