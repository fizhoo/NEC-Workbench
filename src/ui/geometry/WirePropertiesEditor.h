#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"

#include <QWidget>

#include <array>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace necwb::ui {

// Edits one wire in the properties pane. Changes stay pending until Apply, which checks them
// and hands the wire on; Revert restores the wire as it is in the model.
class WirePropertiesEditor final : public QWidget {
public:
    explicit WirePropertiesEditor(QWidget* parent = nullptr);

    void setWire(const model::Wire& wire, const model::AntennaModel& model,
        model::LengthUnit lengthUnit);
    [[nodiscard]] auto wire() const -> model::Wire;
    [[nodiscard]] auto hasPendingEdits() const noexcept -> bool { return pending_; }
    void discardPendingEdits();
    // Called with the wire as it was and as applied.
    void setAppliedCallback(std::function<void(const model::Wire&, const model::Wire&)> callback);

private:
    void apply();
    void selectGauge(int index);
    void updateDiameterLabel();
    void setPending(bool pending);
    [[nodiscard]] auto validationError(const model::Wire& updated) const -> QString;

    model::Wire original_;
    model::AntennaModel model_;
    model::LengthUnit lengthUnit_{model::LengthUnit::Meter};
    QLabel* title_{};
    QSpinBox* tagControl_{};
    QSpinBox* segmentsControl_{};
    std::array<QDoubleSpinBox*, 6> coordinateControls_{};
    QComboBox* gaugeControl_{};
    QDoubleSpinBox* radiusControl_{};
    QLabel* diameterLabel_{};
    QLabel* validationLabel_{};
    QPushButton* applyButton_{};
    QPushButton* revertButton_{};
    std::function<void(const model::Wire&, const model::Wire&)> applied_;
    bool updating_{};
    bool selectingGauge_{};
    bool pending_{};
};

}
