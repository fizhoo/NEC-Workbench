#pragma once

#include "analysis/AnalysisResult.h"

#include <QWidget>

class QLabel;
class QComboBox;

namespace necwb::ui {

class SweepPlotWidget;

class SweepPlotsView final : public QWidget {
public:
    explicit SweepPlotsView(QWidget* parent = nullptr);

    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);
    void setSelectedFrequency(double frequencyMHz);

private:
    QLabel* summary_{};
    QComboBox* impedanceScaleControl_{};
    QComboBox* swrScaleControl_{};
    SweepPlotWidget* impedancePlot_{};
    SweepPlotWidget* swrPlot_{};
};

}
