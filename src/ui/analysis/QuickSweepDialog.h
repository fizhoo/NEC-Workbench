#pragma once

#include "model/ModelSetup.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace necwb::ui {

class QuickSweepDialog final : public QDialog {
public:
    explicit QuickSweepDialog(const model::FrequencyDefinition& initial,
        QWidget* parent = nullptr, bool radiationAvailable = true);

    [[nodiscard]] auto frequencyDefinition() const -> model::FrequencyDefinition;
    [[nodiscard]] auto includeRadiationPatterns() const -> bool;

private:
    void updateControls();
    void updateSummary();

    QDoubleSpinBox* start_{};
    QDoubleSpinBox* stop_{};
    QComboBox* spacing_{};
    QDoubleSpinBox* step_{};
    QSpinBox* points_{};
    QCheckBox* radiation_{};
    QLabel* summary_{};
    QDialogButtonBox* buttons_{};
    model::FrequencyDefinition definition_;
};

}
