#pragma once

#include "model/AntennaModel.h"
#include "ui/analysis/ResultData.h"

#include <QString>

#include <optional>

namespace necwb::ui {

// The result views a run can open, as a thumbnail or in the center of the main window.
enum class ResultView {
    Swr,
    Azimuth,
    Elevation,
    Currents,
    ImpedanceTable,
    CurrentsAlongWire,
    CurrentsTable,
    Pattern3D,
    Performance
};

// What every result view shows. The versions change with the run and with the overlay, so a
// view that was off screen can tell what it missed.
struct ResultState {
    ResultData::Pointer data{ResultData::empty()};
    QString context;
    model::AntennaModel model;
    std::optional<double> frequencyMHz;
    ResultData::Pointer reference;
    QString referenceLabel;
    bool referenceVisible{};
    double referenceImpedanceOhms{50.0};
    bool stale{};
    int dataVersion{};
    int referenceVersion{};
};

// What one view was last given.
struct AppliedResultState {
    int dataVersion{-1};
    int referenceVersion{-1};
    std::optional<double> frequencyMHz;
    std::optional<double> referenceImpedanceOhms;
};

// Gives a view only what changed since it was last brought up to date.
template <typename View>
void applyResultState(View& view, AppliedResultState& applied, const ResultState& state)
{
    if (applied.dataVersion != state.dataVersion) {
        if constexpr (requires { view.setModel(state.model); }) view.setModel(state.model);
        view.setResults(state.data, state.context);
        // New results reset what the view selected and its Z0.
        applied.dataVersion = state.dataVersion;
        applied.frequencyMHz.reset();
        applied.referenceImpedanceOhms.reset();
    }
    if constexpr (requires { view.setReferenceResults(state.reference, state.referenceLabel); }) {
        if (applied.referenceVersion != state.referenceVersion) {
            if (state.reference) view.setReferenceResults(state.reference, state.referenceLabel);
            else view.clearReferenceResults();
            view.setReferenceVisible(state.referenceVisible);
            applied.referenceVersion = state.referenceVersion;
        }
    }
    if constexpr (requires { view.setReferenceImpedance(state.referenceImpedanceOhms); }) {
        if (applied.referenceImpedanceOhms != state.referenceImpedanceOhms) {
            view.setReferenceImpedance(state.referenceImpedanceOhms);
            applied.referenceImpedanceOhms = state.referenceImpedanceOhms;
        }
    }
    if (state.frequencyMHz && applied.frequencyMHz != state.frequencyMHz) {
        view.setSelectedFrequency(*state.frequencyMHz);
        applied.frequencyMHz = state.frequencyMHz;
    }
}

}
