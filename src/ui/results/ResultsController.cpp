#include "ui/results/ResultsController.h"

#include "analysis/FrequencyComparison.h"
#include "analysis/ResultSummary.h"
#include "ui/analysis/HistoricalOverlayControl.h"
#include "ui/results/KeyNumberStrip.h"
#include "ui/results/ResultFullView.h"
#include "ui/results/ResultsColumn.h"

#include <algorithm>

namespace necwb::ui {
namespace {

auto resultFrequencies(const analysis::AnalysisResult& result) -> std::vector<double>
{
    std::vector<double> frequencies;
    const auto add = [&frequencies](const auto& values) {
        for (const auto& value : values) frequencies.push_back(value.frequencyMHz);
    };
    add(result.feedpoints);
    add(result.currents);
    add(result.radiation);
    std::ranges::sort(frequencies);
    const auto duplicates = std::ranges::unique(frequencies, nearlyEqual);
    frequencies.erase(duplicates.begin(), duplicates.end());
    return frequencies;
}

// Puts one frequency's values from a live solve in place of the run's own.
template <typename Values>
void replaceAt(Values& values, const Values& replacement, double frequencyMHz)
{
    std::erase_if(values, [frequencyMHz](const auto& value) {
        return nearlyEqual(value.frequencyMHz, frequencyMHz);
    });
    for (auto value : replacement) {
        // The solver prints the frequency rounded; keep the one asked for.
        value.frequencyMHz = frequencyMHz;
        values.push_back(std::move(value));
    }
    std::ranges::stable_sort(values, std::less{},
        [](const auto& value) { return value.frequencyMHz; });
}

}

ResultsController::ResultsController(QWidget* parent)
{
    column_ = new ResultsColumn(columnState_, parent);
    fullView_ = new ResultFullView(state_, parent);
    const auto open = [this](ResultView view) { showView(view); };
    column_->setOpenViewCallback(open);
    column_->strip()->setOpenViewCallback(open);
    column_->strip()->setFrequencyChangedCallback([this](double frequency) {
        setFrequency(frequency);
        if (frequencySelected_) frequencySelected_(frequency);
    });
    column_->overlay()->setVisibilityChangedCallback([this](bool visible) {
        state_.referenceVisible = visible;
        ++state_.referenceVersion;
        refresh();
    });
    state_.referenceVisible = column_->overlay()->isChecked();
    fullView_->setFrequencyActivatedCallback([this](double frequency) {
        setFrequency(frequency);
    });
    fullView_->setBackCallback([this] { if (showFullView_) showFullView_(false); });
    fullView_->setExportedCallback([this] { if (hasLivePoints() && exported_) exported_(); });
}

void ResultsController::setResults(ResultData::Pointer data, QString context,
    model::AntennaModel model)
{
    state_.data = data ? std::move(data) : ResultData::empty();
    state_.context = std::move(context);
    state_.model = std::move(model);
    state_.referenceImpedanceOhms = state_.data->result().referenceImpedanceOhms;
    state_.stale = false;
    ++state_.dataVersion;
    state_.liveFrequenciesMHz.clear();
    state_.liveOnly = false;
    ++columnVersion_;
    frequencies_ = resultFrequencies(state_.data->result());
    const auto kept = state_.frequencyMHz && std::ranges::any_of(frequencies_,
        [selected = *state_.frequencyMHz](double value) { return nearlyEqual(value, selected); });
    if (!kept) {
        const auto summary = frequencies_.empty() ? analysis::ResultSummary{}
            : analysis::summarizeResults(state_.data->result(), frequencies_.front());
        state_.frequencyMHz = summary.minimumSwr ? std::optional{summary.minimumSwr->frequencyMHz}
            : frequencies_.empty() ? std::nullopt : std::optional{frequencies_.front()};
    }
    refresh();
}

void ResultsController::clear()
{
    state_.frequencyMHz.reset();
    setResults(nullptr, {}, {});
}

void ResultsController::setStale(bool stale)
{
    if (state_.stale == stale) return;
    state_.stale = stale;
    refresh();
}

void ResultsController::setLivePoint(const analysis::AnalysisResult& point, double frequencyMHz)
{
    if (state_.liveFrequenciesMHz.empty()) {
        merged_ = state_.data->result();
        state_.liveOnly = merged_.feedpoints.empty() && merged_.currents.empty()
            && merged_.radiation.empty();
    }
    replaceAt(merged_.feedpoints, point.feedpoints, frequencyMHz);
    replaceAt(merged_.currents, point.currents, frequencyMHz);
    replaceAt(merged_.radiation, point.radiation, frequencyMHz);
    mergedData_ = ResultData::make(merged_);
    if (std::ranges::none_of(state_.liveFrequenciesMHz,
            [frequencyMHz](double live) { return nearlyEqual(live, frequencyMHz); })) {
        state_.liveFrequenciesMHz.push_back(frequencyMHz);
    }
    if (!state_.frequencyMHz) state_.frequencyMHz = frequencyMHz;
    ++columnVersion_;
    refresh();
}

void ResultsController::clearLivePoints()
{
    if (state_.liveFrequenciesMHz.empty()) return;
    state_.liveFrequenciesMHz.clear();
    state_.liveOnly = false;
    mergedData_.reset();
    ++columnVersion_;
    refresh();
}

auto ResultsController::isStaleAt(double frequencyMHz) const -> bool
{
    const auto live = std::ranges::any_of(state_.liveFrequenciesMHz,
        [frequencyMHz](double value) { return nearlyEqual(value, frequencyMHz); });
    return !live && state_.stale;
}

void ResultsController::setFrequencySelectedCallback(std::function<void(double)> callback)
{
    frequencySelected_ = std::move(callback);
}

void ResultsController::setExportedCallback(std::function<void()> callback)
{
    exported_ = std::move(callback);
}

void ResultsController::setReference(ResultData::Pointer data, QString label,
    QString compatibility)
{
    column_->overlay()->setSelection(label, std::move(compatibility));
    if (state_.reference == data && state_.referenceLabel == label) return;
    state_.reference = std::move(data);
    state_.referenceLabel = std::move(label);
    ++state_.referenceVersion;
    refresh();
}

void ResultsController::clearReference()
{
    column_->overlay()->clearSelection();
    if (!state_.reference) return;
    state_.reference.reset();
    state_.referenceLabel.clear();
    ++state_.referenceVersion;
    refresh();
}

void ResultsController::setReferenceVisible(bool visible)
{
    column_->overlay()->setChecked(visible);
}

void ResultsController::setReferenceImpedance(double ohms)
{
    state_.referenceImpedanceOhms = ohms;
    refresh();
}

void ResultsController::setFrequency(double frequencyMHz)
{
    if (state_.frequencyMHz && nearlyEqual(*state_.frequencyMHz, frequencyMHz)) return;
    state_.frequencyMHz = frequencyMHz;
    refresh();
}

void ResultsController::showView(ResultView view)
{
    fullView_->showView(view);
    if (showFullView_) showFullView_(true);
}

void ResultsController::setFrequenciesChangedCallback(
    std::function<void(const std::vector<double>&, std::optional<double>)> callback)
{
    frequenciesChanged_ = std::move(callback);
}

void ResultsController::setShowFullViewCallback(std::function<void(bool)> callback)
{
    showFullView_ = std::move(callback);
}

void ResultsController::setChooseReferenceCallback(std::function<void(QWidget*)> callback)
{
    column_->overlay()->setChooseRequestedCallback(std::move(callback));
}

void ResultsController::refresh()
{
    columnState_ = state_;
    if (mergedData_ && !state_.liveFrequenciesMHz.empty()) columnState_.data = mergedData_;
    columnState_.dataVersion = columnVersion_;
    columnState_.fullRunData = state_.data;
    // Its own numbering, apart from the column's, so a view never mistakes one for the other.
    columnState_.fullRunVersion = -1 - state_.dataVersion;
    frequencies_ = resultFrequencies(columnState_.data->result());
    refreshStrip();
    column_->refresh();
    fullView_->refresh();
    if (frequenciesChanged_) frequenciesChanged_(frequencies_, state_.frequencyMHz);
}

void ResultsController::refreshStrip()
{
    auto* strip = column_->strip();
    strip->setFrequencies(frequencies_, state_.frequencyMHz);
    auto summary = state_.frequencyMHz
        ? analysis::summarizeResults(columnState_.data->result(), *state_.frequencyMHz,
              std::nullopt, state_.referenceImpedanceOhms)
        : analysis::ResultSummary{};
    // Band minimum and peak are over the sweep, so they stay the last full run's.
    if (columnState_.data != state_.data && !state_.liveOnly && state_.frequencyMHz) {
        const auto full = analysis::summarizeResults(state_.data->result(), *state_.frequencyMHz,
            std::nullopt, state_.referenceImpedanceOhms);
        summary.minimumSwr = full.minimumSwr;
        summary.peakGain = full.peakGain;
    } else if (state_.liveOnly) {
        summary.minimumSwr.reset();
        summary.peakGain.reset();
    }
    strip->setSummary(summary, state_.referenceImpedanceOhms);
    strip->setStale(state_.frequencyMHz && isStaleAt(*state_.frequencyMHz));
}

}
