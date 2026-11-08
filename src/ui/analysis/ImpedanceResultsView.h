#pragma once

#include "ui/analysis/ResultData.h"

#include <QWidget>

#include <functional>

class QLabel;
class QTableWidget;

namespace necwb::ui {

class ImpedanceResultsView final : public QWidget {
public:
    explicit ImpedanceResultsView(QWidget* parent = nullptr);

    void setResults(ResultData::Pointer data, const QString& runDirectory);
    void setSelectedFrequency(double frequencyMHz);
    void setReferenceImpedance(double ohms);
    // Called with the frequency of a clicked row.
    void setFrequencyActivatedCallback(std::function<void(double)> callback);

private:
    void refresh();

    ResultData::Pointer data_{ResultData::empty()};
    // Follows Setup's Z0, which can change after the run without a new one.
    double referenceImpedanceOhms_{50.0};
    QString runDirectory_;
    QLabel* summary_{};
    QTableWidget* table_{};
};

}
