#pragma once

#include "analysis/AnalysisResult.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace necwb::ui {

class ImpedanceResultsView final : public QWidget {
public:
    explicit ImpedanceResultsView(QWidget* parent = nullptr);

    void setResults(const analysis::AnalysisResult& result, const QString& runDirectory);

private:
    QLabel* summary_{};
    QTableWidget* table_{};
};

}
