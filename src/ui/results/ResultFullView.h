#pragma once

#include "ui/results/ResultState.h"

#include <QWidget>

#include <functional>

class QLabel;
class QStackedWidget;

namespace necwb::ui {

class CurrentDistributionView;
class ElidedLabel;
class ImpedanceResultsView;
class Radiation3DView;
class RadiationPatternView;
class RadiationPerformanceView;
class SweepPlotsView;

// One result view with all its controls, shown in the center of the main window in place of
// the geometry. Only the view on screen is kept up to date; the others catch up when shown.
class ResultFullView final : public QWidget {
public:
    ResultFullView(const ResultState& state, QWidget* parent = nullptr);

    void showView(ResultView view);
    [[nodiscard]] auto currentView() const -> ResultView { return view_; }
    // Brings the view on screen up to date with the state.
    void refresh();
    void setBackCallback(std::function<void()> callback);
    // Called with a frequency picked on a plot or in a table.
    void setFrequencyActivatedCallback(std::function<void(double)> callback);

private:
    struct Page {
        QWidget* widget{};
        AppliedResultState applied;
    };
    [[nodiscard]] auto page(ResultView view) -> Page&;
    void apply();
    void refreshHeader();

    const ResultState& state_;
    ResultView view_{ResultView::Swr};
    ElidedLabel* title_{};
    QLabel* frequency_{};
    QStackedWidget* pages_{};
    SweepPlotsView* sweep_{};
    RadiationPatternView* pattern_{};
    CurrentDistributionView* currents_{};
    ImpedanceResultsView* impedance_{};
    Radiation3DView* pattern3D_{};
    RadiationPerformanceView* performance_{};
    Page sweepPage_;
    Page patternPage_;
    Page currentsPage_;
    Page impedancePage_;
    Page pattern3DPage_;
    Page performancePage_;
    std::function<void()> back_;
};

}
