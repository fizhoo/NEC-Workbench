#pragma once

#include "analysis/AnalysisResult.h"
#include "model/AntennaModel.h"
#include "ui/analysis/ResultData.h"

#include <QPointF>
#include <QWidget>

#include <functional>
#include <optional>
#include <span>
#include <vector>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;
class QTabWidget;
class QPushButton;

namespace necwb::ui {

class CurrentAntennaView;
class CurrentPlotWidget;
class RadiationPolarWidget;
class RadiationSurfaceWidget;
class DirectionalMetricsView;

[[nodiscard]] auto resultModelExtentFromOrigin(const model::AntennaModel& model) -> double;
[[nodiscard]] auto radiationAnglesCoverCircle(const std::vector<QPointF>& samples) -> bool;
using CurrentPlotPath = std::vector<analysis::SegmentCurrentResult>;
[[nodiscard]] auto connectedCurrentPaths(
    const std::vector<analysis::SegmentCurrentResult>& samples,
    const model::AntennaModel& model) -> std::vector<CurrentPlotPath>;

class CurrentDistributionView final : public QWidget {
public:
    enum class Tab { Antenna, AlongWire, Table };
    explicit CurrentDistributionView(QWidget* parent = nullptr);
    void setResults(ResultData::Pointer data, const QString& runDirectory);
    void setModel(const model::AntennaModel& model);
    void setSelectedFrequency(double frequencyMHz);
    void showTab(Tab tab);
    // Compact shows only the antenna drawing, with no controls and no animation.
    void setCompact(bool compact);

private:
    void refresh();
    ResultData::Pointer data_{ResultData::empty()};
    model::AntennaModel model_;
    QComboBox* frequency_{};
    QLabel* summary_{};
    QTabWidget* views_{};
    CurrentAntennaView* antenna_{};
    CurrentPlotWidget* plot_{};
    QTableWidget* table_{};
    QString runContext_;
    bool compact_{};
};

// Receives an NEC theta and phi chosen on a pattern to use as the forward direction.
using ForwardDirectionCallback = std::function<void(double, double)>;

class RadiationPatternView final : public QWidget {
public:
    using SettingsChangedCallback = std::function<void(const analysis::RadiationDisplaySettings&)>;
    // An elevation cut is a side view at one azimuth; an azimuth cut a top view at one elevation.
    enum class Cut { Elevation, Azimuth };
    explicit RadiationPatternView(QWidget* parent = nullptr);
    void setResults(ResultData::Pointer data, const QString& runDirectory);
    void setReferenceResults(ResultData::Pointer data, QString label);
    void clearReferenceResults();
    void setReferenceVisible(bool visible);
    void setReferenceControlVisible(bool visible);
    void setSelectedFrequency(double frequencyMHz);
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings);
    void setComponent(analysis::RadiationComponent component);
    void setSettingsChangedCallback(SettingsChangedCallback callback);
    void setForwardDirectionCallback(ForwardDirectionCallback callback);
    // Shows that kind of cut through the pattern peak, switching dataset when needed.
    void showCut(Cut cut);
    // Whether any dataset at the selected frequency has that kind of cut.
    [[nodiscard]] auto hasCut(Cut cut) const -> bool;
    void setFrequencyControlVisible(bool visible);
    // Compact shows only the plot, held on one kind of cut through the peak.
    void setCompact(bool compact, Cut cut);

private:
    enum class CutOrientation { Vertical, Horizontal };
    [[nodiscard]] auto radiation() const -> std::span<const analysis::RadiationSample>;
    [[nodiscard]] auto availableCutPlanes(CutOrientation orientation) const -> std::vector<double>;
    [[nodiscard]] auto availableCutPlanes(CutOrientation orientation, int patternIndex) const
        -> std::vector<double>;
    [[nodiscard]] auto bestDatasetForCut(CutOrientation orientation) const -> std::optional<int>;
    [[nodiscard]] auto currentCutPeak() const -> std::optional<analysis::RadiationCutPoint>;
    void selectPlaneThrough(double thetaDegrees, double phiDegrees);
    void refreshCutControls();
    void refreshSelectors();
    void refreshDatasets();
    void refresh();
    void toggleOrientation();
    void stepAngle(int offset);
    void showMaxGainCut();
    void exportImage();
    void exportData();
    void settingsChanged();
    ResultData::Pointer data_{ResultData::empty()};
    ResultData::Pointer referenceData_;
    QString referenceLabel_;
    QCheckBox* referenceControl_{};
    QWidget* controls_{};
    QLabel* frequencyLabel_{};
    QComboBox* frequency_{};
    QComboBox* dataset_{};
    QComboBox* phi_{};
    QComboBox* component_{};
    QComboBox* scale_{};
    QComboBox* floor_{};
    QLabel* cutLabel_{};
    QLabel* summary_{};
    QLabel* hoverReadout_{};
    QPushButton* orientationButton_{};
    QPushButton* maxGainCutButton_{};
    QPushButton* exportImageButton_{};
    QPushButton* exportDataButton_{};
    RadiationPolarWidget* plot_{};
    CutOrientation orientation_{CutOrientation::Vertical};
    QString runContext_;
    // Shown once after Show ... Cut switches dataset; any later change clears it.
    QString datasetSwitchNote_;
    SettingsChangedCallback settingsChangedCallback_;
    bool updatingSettings_{};
    std::optional<Cut> compactCut_;
};

class Radiation3DView final : public QWidget {
public:
    using SettingsChangedCallback = std::function<void(const analysis::RadiationDisplaySettings&)>;
    explicit Radiation3DView(QWidget* parent = nullptr);
    void setResults(ResultData::Pointer data, const QString& runDirectory);
    void setReferenceResults(ResultData::Pointer data, QString label);
    void clearReferenceResults();
    void setReferenceVisible(bool visible);
    void setReferenceControlVisible(bool visible);
    void setModel(const model::AntennaModel& model);
    void setSelectedFrequency(double frequencyMHz);
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings);
    void setComponent(analysis::RadiationComponent component);
    void setSettingsChangedCallback(SettingsChangedCallback callback);
    void setForwardDirectionCallback(ForwardDirectionCallback callback);
    void setOverviewMode(bool enabled);
    void setFrequencyControlVisible(bool visible);

private:
    void refresh();
    void refreshDatasets();
    void settingsChanged();
    void exportImage();
    void exportData();
    ResultData::Pointer data_{ResultData::empty()};
    ResultData::Pointer referenceData_;
    QString referenceLabel_;
    model::AntennaModel model_;
    QLabel* frequencyLabel_{};
    QComboBox* frequency_{};
    QComboBox* dataset_{};
    QComboBox* component_{};
    QComboBox* scale_{};
    QComboBox* floor_{};
    QWidget* controls_{};
    QLabel* summary_{};
    QLabel* hoverReadout_{};
    QCheckBox* antennaControl_{};
    QCheckBox* currentControl_{};
    QCheckBox* radiationControl_{};
    QCheckBox* referenceControl_{};
    QPushButton* exportImageButton_{};
    QPushButton* exportDataButton_{};
    RadiationSurfaceWidget* surface_{};
    QString runContext_;
    SettingsChangedCallback settingsChangedCallback_;
    bool updatingSettings_{};
};

class RadiationPerformanceView final : public QWidget {
public:
    using ComponentChangedCallback = std::function<void(analysis::RadiationComponent)>;
    explicit RadiationPerformanceView(QWidget* parent = nullptr);
    void setResults(ResultData::Pointer data, const QString& runDirectory);
    void setReferenceResults(ResultData::Pointer data, QString label);
    void clearReferenceResults();
    void setReferenceVisible(bool visible);
    void setReferenceControlVisible(bool visible);
    void setSelectedFrequency(double frequencyMHz);
    void setComponent(analysis::RadiationComponent component);
    void setComponentChangedCallback(ComponentChangedCallback callback);
    // Uses an NEC direction picked on another radiation view as the forward direction.
    void setForwardDirection(double thetaDegrees, double phiDegrees);
    void setFrequencyActivatedCallback(std::function<void(double)> callback);

private:
    void seedDirectionFromPatternPeak();
    void refresh();
    ResultData::Pointer data_{ResultData::empty()};
    ResultData::Pointer referenceData_;
    QString referenceLabel_;
    QComboBox* component_{};
    QDoubleSpinBox* forwardElevation_{};
    QDoubleSpinBox* forwardAzimuth_{};
    QPushButton* usePatternPeak_{};
    QLabel* readout_{};
    QLabel* summary_{};
    QCheckBox* referenceControl_{};
    DirectionalMetricsView* plot_{};
    QString runContext_;
    std::optional<double> selectedFrequencyMHz_;
    ComponentChangedCallback componentChangedCallback_;
    bool directionCustomized_{};
    bool updatingDirection_{};
};

}
