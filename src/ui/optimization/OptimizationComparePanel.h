#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/OptimizationObjective.h"

#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QCheckBox;
class QTabWidget;

namespace necwb::ui {

class RadiationPatternView;
class DirectionalMetricsView;
class SweepPlotsView;

class OptimizationComparePanel final : public QWidget {
public:
    explicit OptimizationComparePanel(QWidget* parent = nullptr);

    void setBaseline(const analysis::AnalysisResult& result, QString label);
    void clearBaseline();
    void setBaselineStatus(QString status, bool compatible);
    void setCandidate(const analysis::AnalysisResult& result, QString label,
        QString breakdown = {}, QString comparisonBreakdown = {},
        std::optional<analysis::OptimizationObjectiveResult> directionalMetrics = {},
        std::optional<analysis::OptimizationObjectiveResult> baselineDirectionalMetrics = {});
    void clearCandidate();

private:
    void refresh();

    QLabel* status_{};
    QCheckBox* compareWithStartingModel_{};
    QLabel* breakdown_{};
    QTabWidget* tabs_{};
    SweepPlotsView* sweepPlots_{};
    RadiationPatternView* pattern_{};
    DirectionalMetricsView* directional_{};
    std::optional<analysis::AnalysisResult> baseline_;
    std::optional<analysis::AnalysisResult> candidate_;
    std::optional<analysis::OptimizationObjectiveResult> directionalMetrics_;
    std::optional<analysis::OptimizationObjectiveResult> baselineDirectionalMetrics_;
    QString baselineLabel_;
    QString candidateLabel_;
    QString candidateBreakdown_;
    QString comparisonBreakdown_;
    QString statusText_;
    bool baselineCompatible_{};
};

}
