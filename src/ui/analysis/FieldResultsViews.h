#pragma once

#include "analysis/AnalysisResult.h"
#include "model/AntennaModel.h"

#include <QPointF>
#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QCheckBox;
class QLabel;
class QTableWidget;
class QPushButton;

namespace necwb::ui {

class CurrentPlotWidget;
class RadiationPolarWidget;
class RadiationSurfaceWidget;

[[nodiscard]] auto resultModelExtentFromOrigin(const model::AntennaModel& model) -> double;
[[nodiscard]] auto radiationAnglesCoverCircle(const std::vector<QPointF>& samples) -> bool;
using CurrentPlotPath = std::vector<analysis::SegmentCurrentResult>;
[[nodiscard]] auto connectedCurrentPaths(
    const std::vector<analysis::SegmentCurrentResult>& samples,
    const model::AntennaModel& model) -> std::vector<CurrentPlotPath>;

class CurrentDistributionView final : public QWidget {
public:
    explicit CurrentDistributionView(QWidget* parent = nullptr);
    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);
    void setModel(const model::AntennaModel& model);
    void setSelectedFrequency(double frequencyMHz);

private:
    void refresh();
    analysis::AnalysisResult result_;
    model::AntennaModel model_;
    QComboBox* frequency_{};
    QLabel* summary_{};
    CurrentPlotWidget* plot_{};
    QTableWidget* table_{};
    QString runContext_;
};

class RadiationPatternView final : public QWidget {
public:
    using SettingsChangedCallback = std::function<void(const analysis::RadiationDisplaySettings&)>;
    explicit RadiationPatternView(QWidget* parent = nullptr);
    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);
    void setSelectedFrequency(double frequencyMHz);
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings);
    void setSettingsChangedCallback(SettingsChangedCallback callback);

private:
    enum class CutOrientation { Vertical, Horizontal };
    [[nodiscard]] auto availableCutPlanes(CutOrientation orientation) const -> std::vector<double>;
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
    analysis::AnalysisResult result_;
    QComboBox* frequency_{};
    QComboBox* dataset_{};
    QComboBox* phi_{};
    QComboBox* component_{};
    QComboBox* scale_{};
    QComboBox* floor_{};
    QLabel* cutLabel_{};
    QLabel* summary_{};
    QPushButton* orientationButton_{};
    QPushButton* maxGainCutButton_{};
    QPushButton* exportImageButton_{};
    QPushButton* exportDataButton_{};
    RadiationPolarWidget* plot_{};
    CutOrientation orientation_{CutOrientation::Vertical};
    QString runContext_;
    SettingsChangedCallback settingsChangedCallback_;
    bool updatingSettings_{};
};

class Radiation3DView final : public QWidget {
public:
    using SettingsChangedCallback = std::function<void(const analysis::RadiationDisplaySettings&)>;
    explicit Radiation3DView(QWidget* parent = nullptr);
    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);
    void setModel(const model::AntennaModel& model);
    void setSelectedFrequency(double frequencyMHz);
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings);
    void setSettingsChangedCallback(SettingsChangedCallback callback);
    void setOverviewMode(bool enabled);

private:
    void refresh();
    void refreshDatasets();
    void settingsChanged();
    void exportImage();
    void exportData();
    analysis::AnalysisResult result_;
    model::AntennaModel model_;
    QComboBox* frequency_{};
    QComboBox* dataset_{};
    QComboBox* component_{};
    QComboBox* scale_{};
    QComboBox* floor_{};
    QWidget* controls_{};
    QLabel* summary_{};
    QCheckBox* antennaControl_{};
    QCheckBox* currentControl_{};
    QCheckBox* radiationControl_{};
    QPushButton* exportImageButton_{};
    QPushButton* exportDataButton_{};
    RadiationSurfaceWidget* surface_{};
    QString runContext_;
    SettingsChangedCallback settingsChangedCallback_;
    bool updatingSettings_{};
};

}
