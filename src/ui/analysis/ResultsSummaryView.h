#pragma once

#include "analysis/AnalysisResult.h"

#include <QWidget>

class QLabel;

namespace necwb::ui {

class ResultsSummaryView final : public QWidget {
public:
    explicit ResultsSummaryView(QWidget* parent = nullptr);

    void setResults(const analysis::AnalysisResult& result, const QString& context);
    void setSelectedFrequency(double frequencyMHz);
    void clear();

private:
    void refresh();

    analysis::AnalysisResult result_;
    QString context_;
    double selectedFrequency_{};
    QLabel* contextLabel_{};
    QLabel* frequencyLabel_{};
    QLabel* impedanceLabel_{};
    QLabel* swrLabel_{};
    QLabel* radiationLabel_{};
    QLabel* availabilityLabel_{};
};

}
