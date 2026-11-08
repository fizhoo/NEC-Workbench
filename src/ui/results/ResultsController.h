#pragma once

#include "ui/results/ResultState.h"

#include <functional>
#include <vector>

class QWidget;

namespace necwb::ui {

class ResultFullView;
class ResultsColumn;

// Owns the displayed run and the one selected frequency, and keeps the results column, the
// full view and the status bar on them together.
class ResultsController final {
public:
    explicit ResultsController(QWidget* parent);

    [[nodiscard]] auto column() const -> ResultsColumn* { return column_; }
    [[nodiscard]] auto fullView() const -> ResultFullView* { return fullView_; }
    [[nodiscard]] auto data() const -> const ResultData::Pointer& { return state_.data; }
    [[nodiscard]] auto referenceImpedanceOhms() const -> double
    {
        return state_.referenceImpedanceOhms;
    }

    // Keeps the selected frequency when the new run has it; otherwise selects minimum SWR.
    void setResults(ResultData::Pointer data, QString context, model::AntennaModel model);
    // Also forgets the selected frequency, so the next run starts at its minimum SWR.
    void clear();
    void setStale(bool stale);
    void setReference(ResultData::Pointer data, QString label, QString compatibility);
    void clearReference();
    void setReferenceVisible(bool visible);
    void setReferenceImpedance(double ohms);
    void setFrequency(double frequencyMHz);
    void showView(ResultView view);

    // Called with every frequency in the run and the selected one.
    void setFrequenciesChangedCallback(
        std::function<void(const std::vector<double>&, std::optional<double>)> callback);
    // Called with true to show the full view in the center, false to return to geometry.
    void setShowFullViewCallback(std::function<void(bool)> callback);
    void setChooseReferenceCallback(std::function<void(QWidget*)> callback);

private:
    void refresh();
    void refreshStrip();

    ResultState state_;
    std::vector<double> frequencies_;
    ResultsColumn* column_{};
    ResultFullView* fullView_{};
    std::function<void(const std::vector<double>&, std::optional<double>)> frequenciesChanged_;
    std::function<void(bool)> showFullView_;
};

}
