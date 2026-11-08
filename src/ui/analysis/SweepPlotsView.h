#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/OptimizationObjective.h"
#include "ui/analysis/ResultData.h"

#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QComboBox;
class QCheckBox;
class QLabel;
class QSplitter;

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

    void setResults(ResultData::Pointer data, const QString& runDirectory);
    void setReferenceResults(ResultData::Pointer data, QString label);
    void clearReferenceResults();
    void setReferenceVisible(bool visible);
    void setReferenceControlVisible(bool visible);
    void setSelectedFrequency(double frequencyMHz);
    void setReferenceImpedance(double ohms);
    void configureForCompactReview();
    // Compact shows only the SWR plot, with no controls or labels around it.
    void setCompact(bool compact);
    // Called with a frequency double-clicked on either plot.
    void setFrequencyActivatedCallback(std::function<void(double)> callback);

private:
    void refreshPlots();

    QWidget* heading_{};
    QLabel* summary_{};
    QCheckBox* referenceControl_{};
    QComboBox* swrScaleControl_{};
    SweepPlotWidget* impedancePlot_{};
    SweepPlotWidget* swrPlot_{};
    QSplitter* splitter_{};
    ResultData::Pointer data_;
    ResultData::Pointer referenceData_;
    // Follows Setup's Z0, which can change after the run without a new one.
    double referenceImpedanceOhms_{50.0};
    QString runDirectory_;
    QString referenceLabel_;
};

class DirectionalMetricsView final : public QWidget {
public:
    explicit DirectionalMetricsView(QWidget* parent = nullptr);

    void setResults(const analysis::OptimizationObjectiveResult& result);
    void setReferenceResults(const analysis::OptimizationObjectiveResult& result,
        QString label);
    void setMetrics(const std::vector<DirectionalPlotPoint>& metrics);
    void setReferenceMetrics(const std::vector<DirectionalPlotPoint>& metrics, QString label);
    void clearReferenceMetrics();
    void setSelectedFrequency(double frequencyMHz);
    // Called with a frequency double-clicked on the plot.
    void setFrequencyActivatedCallback(std::function<void(double)> callback);

private:
    void refresh();
    QLabel* summary_{};
    SweepPlotWidget* plot_{};
    std::vector<DirectionalPlotPoint> metrics_;
    std::vector<DirectionalPlotPoint> referenceMetrics_;
    QString referenceLabel_;
};

}
