#pragma once

#include "analysis/SegmentCurrentDisplay.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace necwb::ui {

class CurrentAntennaCanvas;

// Draws segment currents on a 2D view of the solved antenna: a magnitude envelope beside
// each wire, phase as wire color, or the instantaneous current over one RF cycle.
class CurrentAntennaView final : public QWidget {
public:
    enum class DisplayMode {
        Magnitude,
        Phase,
        Instantaneous
    };

    explicit CurrentAntennaView(QWidget* parent = nullptr);

    void setCurrents(analysis::DisplayedCurrents currents, const model::AntennaModel& model);

private:
    void updateControls();
    void advanceCycle();

    CurrentAntennaCanvas* canvas_{};
    QComboBox* mode_{};
    QComboBox* plane_{};
    QSlider* scale_{};
    QLabel* scaleLabel_{};
    QSlider* cycle_{};
    QLabel* cycleLabel_{};
    QPushButton* play_{};
    QTimer* animation_{};
};

}
