#pragma once

#include "analysis/ResultSummary.h"
#include "ui/results/ResultState.h"

#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QLabel;

namespace necwb::ui {

class ElidedLabel;

// The selected frequency and the numbers at it: R, X, SWR, gain with its direction, F/B and
// F/R, with the band's minimum SWR and peak gain on a second line. The frequency selector
// here is the only one; every result view follows it.
class KeyNumberStrip final : public QWidget {
public:
    explicit KeyNumberStrip(QWidget* parent = nullptr);

    void setFrequencies(const std::vector<double>& frequenciesMHz,
        std::optional<double> selectedMHz);
    void setSummary(const analysis::ResultSummary& summary, double referenceImpedanceOhms);
    void setStale(bool stale);
    void setFrequencyChangedCallback(std::function<void(double)> callback);
    // Clicking a number opens the view it comes from.
    void setOpenViewCallback(std::function<void(ResultView)> callback);

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    auto addValue(const QString& name, ResultView view) -> ElidedLabel*;

    QComboBox* frequency_{};
    QLabel* stale_{};
    QWidget* values_{};
    ElidedLabel* resistance_{};
    ElidedLabel* reactance_{};
    ElidedLabel* swr_{};
    ElidedLabel* gain_{};
    ElidedLabel* frontToBack_{};
    ElidedLabel* frontToRear_{};
    ElidedLabel* extrema_{};
    std::function<void(double)> frequencyChanged_;
    std::function<void(ResultView)> openView_;
};

}
