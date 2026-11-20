#pragma once

#include "nec/SegmentationEdit.h"

#include <QDialog>

#include <string>

class QCheckBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace necwb::ui {

// Previews the edit Automatic Segmentation makes to the deck, which Apply takes as it is.
class AutoSegmentationDialog final : public QDialog {
public:
    // `source` is the authored deck; `model` and `setup` come from its resolved form.
    AutoSegmentationDialog(std::string source, const model::AntennaModel& model,
        const model::ModelSetup& setup, double maximumFrequencyMHz, QWidget* parent = nullptr);

    [[nodiscard]] auto edit() const -> const nec::SegmentationEdit&;

private:
    void refreshProposal();

    std::string source_;
    model::AntennaModel model_;
    model::ModelSetup setup_;
    double maximumFrequencyMHz_{};
    nec::SegmentationEdit edit_;
    QSpinBox* segmentsPerWavelengthControl_{};
    QCheckBox* oddExcitedWiresControl_{};
    QLabel* summary_{};
    QTableWidget* table_{};
};

}
