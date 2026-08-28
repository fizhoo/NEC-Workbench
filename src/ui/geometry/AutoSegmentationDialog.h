#pragma once

#include "model/AutoSegmentation.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace necwb::ui {

class AutoSegmentationDialog final : public QDialog {
public:
    AutoSegmentationDialog(const model::AntennaModel& model,
        const std::vector<model::Excitation>& excitations, double maximumFrequencyMHz,
        QWidget* parent = nullptr);

    [[nodiscard]] auto proposal() const -> model::SegmentationProposal;

private:
    void refreshProposal();

    model::AntennaModel model_;
    std::vector<model::Excitation> excitations_;
    double maximumFrequencyMHz_{};
    model::SegmentationProposal proposal_;
    QSpinBox* segmentsPerWavelengthControl_{};
    QCheckBox* oddExcitedWiresControl_{};
    QLabel* summary_{};
    QTableWidget* table_{};
};

}
