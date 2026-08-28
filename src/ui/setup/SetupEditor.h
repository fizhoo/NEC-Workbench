#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QWidget>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace necwb::ui {

class SetupEditor final : public QWidget {
    Q_OBJECT

public:
    explicit SetupEditor(QWidget* parent = nullptr);

    void setData(const model::AntennaModel& model, const model::ModelSetup& setup);
    void selectExcitation(std::size_t sourceLine);

signals:
    void frequencyChanged(model::FrequencyDefinition frequency);
    void frequencyDeleteRequested(std::size_t sourceLine);
    void groundChanged(model::GroundDefinition ground);
    void excitationChanged(model::Excitation excitation);
    void excitationDeleteRequested(std::size_t sourceLine);
    void excitationSelected(std::size_t sourceLine);

private:
    void loadSelectedExcitation();
    void updateFrequencyControls();
    void updateGroundControls();
    void updateExcitationActions();
    [[nodiscard]] auto editedExcitation(std::size_t sourceLine) const -> model::Excitation;

    model::AntennaModel model_;
    model::ModelSetup setup_;
    QCheckBox* frequencySweepControl_{};
    QComboBox* frequencyModeControl_{};
    QDoubleSpinBox* startFrequencyControl_{};
    QDoubleSpinBox* endFrequencyControl_{};
    QDoubleSpinBox* frequencyStepControl_{};
    QLabel* startFrequencyLabel_{};
    QLabel* endFrequencyLabel_{};
    QLabel* frequencyModeLabel_{};
    QLabel* frequencyStepLabel_{};
    QLabel* frequencySummaryTitleLabel_{};
    QLabel* frequencySummaryLabel_{};
    QLabel* frequencyValidationLabel_{};
    QPushButton* removeFrequencyButton_{};
    QComboBox* groundTypeControl_{};
    QComboBox* groundPresetControl_{};
    QDoubleSpinBox* relativePermittivityControl_{};
    QDoubleSpinBox* conductivityControl_{};
    QCheckBox* connectGroundEndsControl_{};
    QTableWidget* excitationTable_{};
    QComboBox* wireControl_{};
    QSpinBox* segmentControl_{};
    QDoubleSpinBox* magnitudeControl_{};
    QDoubleSpinBox* phaseControl_{};
    QPushButton* addExcitationButton_{};
    QPushButton* updateExcitationButton_{};
    QPushButton* deleteExcitationButton_{};
    bool updating_{};
};

}
