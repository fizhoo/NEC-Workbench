#pragma once

#include "analysis/FrequencyPlan.h"
#include "model/ModelSetup.h"

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace necwb::ui {

class AnalysisRequestEditor final : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisRequestEditor(QWidget* parent = nullptr);

    void setData(const model::ModelSetup& setup);
    void selectPattern(std::size_t sourceLine);
    void setReadiness(const QStringList& blockingReasons);
    [[nodiscard]] auto hasPendingEdits() const noexcept -> bool;
    void discardPendingEdits();
    [[nodiscard]] auto radiationFrequencyPlan() const -> analysis::FrequencyPlan;

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
    void setPatternPending(bool pending);
    void setFrequencyPending(bool pending);
    void updateFrequencyControls();
    void addSelectedFrequency(double frequencyMHz);
    void removeSelectedFrequencies();
    void chooseAmateurBandCenters();
    void persistFrequencySelection() const;
    struct FrequencySelectionState {
        int mode{};
        double singleMHz{};
        std::vector<double> selectedMHz;
        double continuousStartMHz{};
        double continuousStopMHz{};
        double continuousStepMHz{};
    };
    [[nodiscard]] auto captureFrequencySelection() const -> FrequencySelectionState;
    void restoreFrequencySelection(const FrequencySelectionState& state);
    [[nodiscard]] auto frequencyPlan(const FrequencySelectionState& state) const
        -> analysis::FrequencyPlan;
    [[nodiscard]] auto modelFrequencies() const -> std::vector<double>;

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
    QComboBox* radiationFrequencyModeControl_{};
    QStackedWidget* radiationFrequencyPages_{};
    QDoubleSpinBox* singleFrequencyControl_{};
    QListWidget* selectedFrequencies_{};
    QDoubleSpinBox* frequencyEntryControl_{};
    QDoubleSpinBox* continuousStartControl_{};
    QDoubleSpinBox* continuousStopControl_{};
    QDoubleSpinBox* continuousStepControl_{};
    QLabel* validationLabel_{};
    QLabel* readinessLabel_{};
    QLabel* sweepCostLabel_{};
    QWidget* patternControls_{};
    QPushButton* resetPatternButton_{};
    QPushButton* applyPatternButton_{};
    QPushButton* applyFrequencyButton_{};
    int lastPatternPreset_{-1};
    bool loadingPattern_{};
    bool patternPending_{};
    bool frequencyPending_{};
    bool loadingFrequency_{};
    bool frequencySelectionInitialized_{};
    bool singleFrequencyInitialized_{};
    bool selectedFrequenciesInitialized_{};
    FrequencySelectionState appliedFrequencySelection_;
};

}
