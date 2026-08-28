#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"

#include <QDialog>

#include <array>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace necwb::ui {

class WirePropertiesDialog final : public QDialog {
public:
    WirePropertiesDialog(const model::Wire& wire, const model::AntennaModel& model,
        model::LengthUnit lengthUnit, QWidget* parent = nullptr);

    [[nodiscard]] auto wire() const -> model::Wire;

protected:
    void accept() override;

private:
    void selectGauge(int index);
    void updateDiameterLabel();

    model::Wire original_;
    model::AntennaModel model_;
    model::LengthUnit lengthUnit_;
    QSpinBox* tagControl_{};
    QSpinBox* segmentsControl_{};
    std::array<QDoubleSpinBox*, 6> coordinateControls_{};
    QComboBox* gaugeControl_{};
    QDoubleSpinBox* radiusControl_{};
    QLabel* diameterLabel_{};
    QLabel* validationLabel_{};
    bool updatingRadius_{};
};

}
