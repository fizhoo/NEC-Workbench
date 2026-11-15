#pragma once

#include "ui/results/LiveSolve.h"
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

    // Solves the deck at one frequency and shows the result as a live point.
    void solveLive(const std::string& generatedDeck, double frequencyMHz,
        const LiveSolve::Solver& solver);
    // A live solve at one frequency, for the results column only: the strip and thumbnails
    // show it, while data(), the full view, exports and overlays keep the last full run.
    void setLivePoint(const analysis::AnalysisResult& point, double frequencyMHz);
    // Cancels a live solve and drops the live points, as when the deck changes.
    void clearLivePoints();
    [[nodiscard]] auto liveSolve() const -> LiveSolve* { return liveSolve_; }
    [[nodiscard]] auto hasLivePoints() const -> bool { return !state_.liveFrequenciesMHz.empty(); }
    [[nodiscard]] auto selectedFrequency() const -> std::optional<double> { return state_.frequencyMHz; }
    // Whether the shown values at this frequency are out of date.
    [[nodiscard]] auto isStaleAt(double frequencyMHz) const -> bool;
    // Called when a frequency is picked in the strip.
    void setFrequencySelectedCallback(std::function<void(double)> callback);
    // Called with messages for the status bar: live solves, and exports made while live
    // points show, which export the last full run.
    void setStatusCallback(std::function<void(const QString&)> callback);

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
    // What the results column shows: the full run with any live points merged in.
    ResultState columnState_;
    // The full run with the live points in place of its own at their frequencies.
    analysis::AnalysisResult merged_;
    ResultData::Pointer mergedData_;
    int columnVersion_{};
    std::vector<double> frequencies_;
    ResultsColumn* column_{};
    ResultFullView* fullView_{};
    std::function<void(const std::vector<double>&, std::optional<double>)> frequenciesChanged_;
    std::function<void(bool)> showFullView_;
    std::function<void(double)> frequencySelected_;
    std::function<void(const QString&)> status_;
    LiveSolve* liveSolve_{};
};

}
