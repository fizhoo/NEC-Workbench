#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/OptimizationObjective.h"

#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QComboBox;

namespace necwb::ui {

class SweepPlotWidget;

struct CandidatePlotPoint {
    int row{};
    double value{};
    analysis::OptimizationObjectiveResult evaluation;
};

class CandidatePlotsView final : public QWidget {
public:
    explicit CandidatePlotsView(QWidget* parent = nullptr);

    void setCandidates(QString variableName, QString valueSuffix,
        const std::vector<CandidatePlotPoint>& candidates, int bestRow);
    void clear();
    void setCandidateActivatedCallback(std::function<void(int)> callback);

private:
    SweepPlotWidget* scorePlot_{};
    std::vector<CandidatePlotPoint> candidates_;
    std::function<void(int)> candidateActivatedCallback_;
};

class SweepPlotsView final : public QWidget {
public:
    explicit SweepPlotsView(QWidget* parent = nullptr);

    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);
    void setSelectedFrequency(double frequencyMHz);

private:
    QLabel* summary_{};
    QComboBox* swrScaleControl_{};
    SweepPlotWidget* impedancePlot_{};
    SweepPlotWidget* swrPlot_{};
};

}
