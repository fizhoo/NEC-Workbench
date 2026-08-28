#pragma once

#include "analysis/AnalysisResult.h"

#include <QWidget>

class QLabel;

namespace necwb::ui {

class SweepPlotWidget;

class SweepPlotsView final : public QWidget {
public:
    explicit SweepPlotsView(QWidget* parent = nullptr);

    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);

private:
    QLabel* summary_{};
    SweepPlotWidget* impedancePlot_{};
    SweepPlotWidget* swrPlot_{};
};

}
