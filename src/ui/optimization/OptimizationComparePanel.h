#pragma once

#include "analysis/AnalysisResult.h"

#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QTabWidget;

namespace necwb::ui {

class RadiationPatternView;
class SweepPlotsView;

class OptimizationComparePanel final : public QWidget {
public:
    explicit OptimizationComparePanel(QWidget* parent = nullptr);

    void setBaseline(const analysis::AnalysisResult& result, QString label);
    void clearBaseline();
    void setBaselineStatus(QString status, bool compatible);
    void setCandidate(const analysis::AnalysisResult& result, QString label,
        QString breakdown = {});
    void clearCandidate();

private:
    void refresh();

    QLabel* status_{};
    QLabel* comparison_{};
    QLabel* breakdown_{};
    QTabWidget* tabs_{};
    SweepPlotsView* sweepPlots_{};
    RadiationPatternView* pattern_{};
    std::optional<analysis::AnalysisResult> baseline_;
    std::optional<analysis::AnalysisResult> candidate_;
    QString baselineLabel_;
    QString candidateLabel_;
    QString statusText_;
    bool baselineCompatible_{};
};

}
