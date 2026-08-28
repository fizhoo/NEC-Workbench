#pragma once

#include "analysis/AnalysisResult.h"
#include "model/AntennaModel.h"

#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QTableWidget;
class QPushButton;

namespace necwb::ui {

class CurrentPlotWidget;
class RadiationPolarWidget;
class RadiationSurfaceWidget;

class CurrentDistributionView final : public QWidget {
public:
    explicit CurrentDistributionView(QWidget* parent = nullptr);
    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);

private:
    void refresh();
    analysis::AnalysisResult result_;
    QComboBox* frequency_{};
    QLabel* summary_{};
    CurrentPlotWidget* plot_{};
    QTableWidget* table_{};
};

class RadiationPatternView final : public QWidget {
public:
    explicit RadiationPatternView(QWidget* parent = nullptr);
    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);

private:
    enum class CutOrientation { Vertical, Horizontal };
    void refreshSelectors();
    void refresh();
    void toggleOrientation();
    void stepAngle(int offset);
    analysis::AnalysisResult result_;
    QComboBox* frequency_{};
    QComboBox* phi_{};
    QLabel* cutLabel_{};
    QLabel* summary_{};
    QPushButton* orientationButton_{};
    RadiationPolarWidget* plot_{};
    CutOrientation orientation_{CutOrientation::Vertical};
};

class Radiation3DView final : public QWidget {
public:
    explicit Radiation3DView(QWidget* parent = nullptr);
    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);
    void setModel(const model::AntennaModel& model);

private:
    void refresh();
    analysis::AnalysisResult result_;
    model::AntennaModel model_;
    QComboBox* frequency_{};
    QLabel* summary_{};
    QCheckBox* antennaControl_{};
    QCheckBox* currentControl_{};
    QCheckBox* radiationControl_{};
    RadiationSurfaceWidget* surface_{};
};

}
