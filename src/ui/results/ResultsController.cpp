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

}

ResultsController::ResultsController(QWidget* parent)
{
    column_ = new ResultsColumn(state_, parent);
    fullView_ = new ResultFullView(state_, parent);
    const auto open = [this](ResultView view) { showView(view); };
    column_->setOpenViewCallback(open);
    column_->strip()->setOpenViewCallback(open);
    column_->strip()->setFrequencyChangedCallback([this](double frequency) {
        setFrequency(frequency);
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
    column_->strip()->setStale(stale);
    column_->refresh();
    fullView_->refresh();
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
    refreshStrip();
    column_->refresh();
    fullView_->refresh();
    if (frequenciesChanged_) frequenciesChanged_(frequencies_, state_.frequencyMHz);
}

void ResultsController::refreshStrip()
{
    auto* strip = column_->strip();
    strip->setFrequencies(frequencies_, state_.frequencyMHz);
    strip->setSummary(state_.frequencyMHz
            ? analysis::summarizeResults(state_.data->result(), *state_.frequencyMHz,
                  std::nullopt, state_.referenceImpedanceOhms)
            : analysis::ResultSummary{},
        state_.referenceImpedanceOhms);
    strip->setStale(state_.stale);
}

}
