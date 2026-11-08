#include "ui/analysis/ResultData.h"

#include "analysis/FrequencyComparison.h"

#include <algorithm>
#include <numeric>

namespace necwb::ui {

ResultData::ResultData(analysis::AnalysisResult result) : result_(std::move(result))
{
    const auto& radiation = result_.radiation;
    std::vector<std::size_t> order(radiation.size());
    std::iota(order.begin(), order.end(), std::size_t{});
    std::ranges::stable_sort(order, {},
        [&radiation](std::size_t index) { return radiation[index].frequencyMHz; });
    for (const auto index : order) {
        const auto& sample = radiation[index];
        if (radiationGroups_.empty()
            || !nearlyEqual(radiationGroups_.back().frequencyMHz, sample.frequencyMHz)) {
            radiationGroups_.push_back({sample.frequencyMHz, {}, {}});
            radiationFrequencies_.push_back(sample.frequencyMHz);
        }
        auto& group = radiationGroups_.back();
        group.samples.push_back(sample);
        auto& stats = group.datasets[sample.patternIndex];
        ++stats.samples;
        if (std::ranges::find(stats.theta, sample.thetaDegrees) == stats.theta.end())
            stats.theta.push_back(sample.thetaDegrees);
        if (std::ranges::find(stats.phi, sample.phiDegrees) == stats.phi.end())
            stats.phi.push_back(sample.phiDegrees);
    }
}

auto ResultData::make(analysis::AnalysisResult result) -> Pointer
{
    return std::make_shared<const ResultData>(std::move(result));
}

auto ResultData::empty() -> const Pointer&
{
    static const auto none = make({});
    return none;
}

auto ResultData::groupAt(double frequencyMHz) const -> const FrequencyGroup*
{
    const auto group = std::ranges::lower_bound(radiationGroups_, frequencyMHz,
        [](double left, double right) { return left < right && !nearlyEqual(left, right); },
        &FrequencyGroup::frequencyMHz);
    return group != radiationGroups_.end() && nearlyEqual(group->frequencyMHz, frequencyMHz)
        ? &*group : nullptr;
}

auto ResultData::radiationAt(double frequencyMHz) const
    -> std::span<const analysis::RadiationSample>
{
    const auto* group = groupAt(frequencyMHz);
    return group != nullptr ? std::span{group->samples} : std::span<const analysis::RadiationSample>{};
}

auto ResultData::patternDatasetsAt(double frequencyMHz) const
    -> const std::map<int, PatternDatasetStats>&
{
    static const std::map<int, PatternDatasetStats> none;
    const auto* group = groupAt(frequencyMHz);
    return group != nullptr ? group->datasets : none;
}

}
