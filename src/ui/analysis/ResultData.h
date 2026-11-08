#pragma once

#include "analysis/AnalysisResult.h"

#include <map>
#include <memory>
#include <span>
#include <vector>

namespace necwb::ui {

struct PatternDatasetStats {
    int samples{};
    std::vector<double> theta;
    std::vector<double> phi;
};

// One run's results, shared by every view that shows them. The radiation samples are
// grouped by frequency once, so a view looks at one frequency without rescanning the run.
class ResultData final {
public:
    using Pointer = std::shared_ptr<const ResultData>;

    explicit ResultData(analysis::AnalysisResult result);

    [[nodiscard]] static auto make(analysis::AnalysisResult result) -> Pointer;
    // Shared by every view with nothing to show, so views never hold a null pointer.
    [[nodiscard]] static auto empty() -> const Pointer&;

    [[nodiscard]] auto result() const -> const analysis::AnalysisResult& { return result_; }
    // Sorted and distinct.
    [[nodiscard]] auto radiationFrequencies() const -> const std::vector<double>&
    {
        return radiationFrequencies_;
    }
    // In run order; empty when the run has no radiation at that frequency.
    [[nodiscard]] auto radiationAt(double frequencyMHz) const
        -> std::span<const analysis::RadiationSample>;
    // Keyed by RP dataset index.
    [[nodiscard]] auto patternDatasetsAt(double frequencyMHz) const
        -> const std::map<int, PatternDatasetStats>&;

private:
    struct FrequencyGroup {
        double frequencyMHz{};
        std::vector<analysis::RadiationSample> samples;
        std::map<int, PatternDatasetStats> datasets;
    };
    [[nodiscard]] auto groupAt(double frequencyMHz) const -> const FrequencyGroup*;

    analysis::AnalysisResult result_;
    std::vector<double> radiationFrequencies_;
    std::vector<FrequencyGroup> radiationGroups_;
};

}
