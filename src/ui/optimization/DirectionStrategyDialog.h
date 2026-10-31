#pragma once

#include "analysis/AnalysisResult.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace necwb::ui {

class DirectionCutPreviewWidget;

class DirectionStrategyDialog final : public QDialog {
public:
    DirectionStrategyDialog(std::vector<analysis::RadiationSample> radiation,
        double targetFrequencyMHz, analysis::RadiationComponent component,
        analysis::DirectionalEvaluationMode mode, double thetaDegrees,
        double phiDegrees, QWidget* parent = nullptr);

    [[nodiscard]] auto mode() const -> analysis::DirectionalEvaluationMode;
    [[nodiscard]] auto thetaDegrees() const -> double;
    [[nodiscard]] auto phiDegrees() const -> double;
    [[nodiscard]] auto usesLatestPatternPeak() const noexcept -> bool;

private:
    enum class PatternSlice {
        Azimuth,
        Elevation
    };

    enum class GainMeasurement {
        StrongestInSlice,
        SpecificAngle
    };

    [[nodiscard]] auto patternSlice() const -> PatternSlice;
    [[nodiscard]] auto gainMeasurement() const -> GainMeasurement;
    void updateControls();
    void updatePreview();
    void useLatestPeak();

    std::vector<analysis::RadiationSample> radiation_;
    double targetFrequencyMHz_{};
    analysis::RadiationComponent component_{analysis::RadiationComponent::Total};
    QComboBox* patternControl_{};
    QLabel* sliceAngleLabel_{};
    QDoubleSpinBox* sliceAngle_{};
    QComboBox* measurementControl_{};
    QLabel* gainAngleLabel_{};
    QDoubleSpinBox* gainAngle_{};
    QLabel* latestResult_{};
    QLabel* previewLabel_{};
    DirectionCutPreviewWidget* preview_{};
    PatternSlice previousPatternSlice_{PatternSlice::Azimuth};
    bool latestPatternPeakSelected_{};
};

}
