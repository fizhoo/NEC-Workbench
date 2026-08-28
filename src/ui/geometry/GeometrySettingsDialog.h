#pragma once

#include "ui/geometry/GeometrySettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace necwb::ui {

class GeometrySettingsDialog final : public QDialog {
public:
    explicit GeometrySettingsDialog(const GeometrySettings& settings, QWidget* parent = nullptr);

    [[nodiscard]] auto settings() const -> GeometrySettings;

private:
    void changeLengthUnit(model::LengthUnit unit);
    void updateUnitControls();

    model::LengthUnit lengthUnit_;
    SnapUnitBehavior snapUnitBehavior_;
    QComboBox* lengthUnitControl_{};
    QCheckBox* automaticGridControl_{};
    QDoubleSpinBox* majorGridControl_{};
    QSpinBox* minorDivisionsControl_{};
    QCheckBox* showGridControl_{};
    QCheckBox* showAxesControl_{};
    QCheckBox* showLabelsControl_{};
    QCheckBox* gridSnappingControl_{};
    QComboBox* snapUnitBehaviorControl_{};
    QDoubleSpinBox* snapSpacingControl_{};
    QCheckBox* endpointSnappingControl_{};
    QDoubleSpinBox* endpointToleranceControl_{};
};

}
