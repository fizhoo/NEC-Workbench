#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QWidget>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace necwb::ui {

class SetupEditor final : public QWidget {
    Q_OBJECT

public:
    explicit SetupEditor(QWidget* parent = nullptr);

    [[nodiscard]] auto frequencyPage() const -> QWidget*;
    [[nodiscard]] auto referenceImpedancePage() const -> QWidget*;
    [[nodiscard]] auto environmentPage() const -> QWidget*;
    void setData(const model::AntennaModel& model, const model::ModelSetup& setup);
    [[nodiscard]] auto hasPendingEdits(const QWidget* page) const -> bool;
    void discardPendingEdits(const QWidget* page);

signals:
    void frequencyChanged(model::FrequencyDefinition frequency);
    void frequencyDeleteRequested(std::size_t sourceLine);
    void groundChanged(model::GroundDefinition ground);
    void referenceImpedanceChanged(model::ReferenceImpedanceDefinition reference);
    void referenceImpedanceDeleteRequested(std::size_t sourceLine);

private:
    void updateFrequencyControls();
    void updateGroundControls();
    void updateGroundPresetSelection();
    void setFrequencyPending(bool pending);
    void setGroundPending(bool pending);
    void setReferenceImpedancePending(bool pending);

    model::AntennaModel model_;
    model::ModelSetup setup_;
    QWidget* frequencyPage_{};
    QWidget* referenceImpedancePage_{};
    QWidget* environmentPage_{};
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
    QPushButton* applyFrequencyButton_{};
    QComboBox* groundTypeControl_{};
    QComboBox* groundPresetControl_{};
    QDoubleSpinBox* relativePermittivityControl_{};
    QDoubleSpinBox* conductivityControl_{};
    QCheckBox* connectGroundEndsControl_{};
    QPushButton* applyGroundButton_{};
    QDoubleSpinBox* referenceImpedanceControl_{};
    QPushButton* applyReferenceImpedanceButton_{};
    QPushButton* removeReferenceImpedanceButton_{};
    bool frequencyPending_{};
    bool groundPending_{};
    bool referenceImpedancePending_{};
    bool updating_{};
};

}
