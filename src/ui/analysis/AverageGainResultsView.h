#pragma once

#include "analysis/AverageGainTest.h"

#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QTableWidget;

namespace necwb::ui {

class AverageGainResultsView final : public QWidget {
public:
    explicit AverageGainResultsView(QWidget* parent = nullptr);

    void clear();
    void setRunning(double frequencyMHz, analysis::AverageGainEnvironment environment,
        const QString& context);
    void setResult(const analysis::AverageGainAssessment& assessment, double frequencyMHz,
        analysis::AverageGainEnvironment environment, std::optional<double> solidAnglePi,
        const QString& context);
    void setFailure(const QString& message, const QString& context);
    void markStale();

private:
    void setValue(int row, const QString& value);

    QLabel* status_{};
    QTableWidget* details_{};
    bool hasResult_{};
};

}
