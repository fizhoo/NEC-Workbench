#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace necwb::ui {

class ExcitationPropertiesDialog final : public QDialog {
public:
    ExcitationPropertiesDialog(const model::Excitation& excitation,
        const model::AntennaModel& model, QWidget* parent = nullptr);

    [[nodiscard]] auto excitation() const -> model::Excitation;

protected:
    void accept() override;

private:
    model::Excitation original_;
    model::AntennaModel model_;
    QComboBox* wireControl_{};
    QSpinBox* segmentControl_{};
    QDoubleSpinBox* magnitudeControl_{};
    QDoubleSpinBox* phaseControl_{};
    QLabel* validationLabel_{};
};

}
