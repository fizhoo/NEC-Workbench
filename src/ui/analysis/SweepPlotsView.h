#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/OptimizationObjective.h"

#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QComboBox;
class QCheckBox;
class QLabel;

namespace necwb::ui {

class SweepPlotWidget;

struct CandidatePlotPoint {
    int row{};
    double value{};
    analysis::OptimizationObjectiveResult evaluation;
};

struct DirectionalPlotPoint {
    double frequencyMHz{};
    std::optional<double> forwardGainDb;
    std::optional<double> frontToBackDb;
    std::optional<double> frontToRearDb;
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
    void setReferenceResults(const analysis::AnalysisResult& result, QString label);
    void clearReferenceResults();
    void setReferenceControlVisible(bool visible);
    void setSelectedFrequency(double frequencyMHz);

private:
    void refreshPlots();

    QLabel* summary_{};
    QCheckBox* referenceControl_{};
    QComboBox* swrScaleControl_{};
    SweepPlotWidget* impedancePlot_{};
    SweepPlotWidget* swrPlot_{};
    std::optional<analysis::AnalysisResult> result_;
    std::optional<analysis::AnalysisResult> referenceResult_;
    QString runDirectory_;
    QString referenceLabel_;
};

class DirectionalMetricsView final : public QWidget {
public:
    explicit DirectionalMetricsView(QWidget* parent = nullptr);

    void setResults(const analysis::OptimizationObjectiveResult& result);
    void setMetrics(const std::vector<DirectionalPlotPoint>& metrics);
    void setReferenceMetrics(const std::vector<DirectionalPlotPoint>& metrics, QString label);
    void clearReferenceMetrics();
    void setSelectedFrequency(double frequencyMHz);

private:
    void refresh();
    QLabel* summary_{};
    SweepPlotWidget* plot_{};
    std::vector<DirectionalPlotPoint> metrics_;
    std::vector<DirectionalPlotPoint> referenceMetrics_;
    QString referenceLabel_;
};

}
