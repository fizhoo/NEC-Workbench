#pragma once

#include <string_view>
#include <vector>

namespace necwb::analysis {

enum class FrequencyPlanMode {
    ModelSweep,
    Explicit
};

struct FrequencyRange {
    double startMHz{};
    double endMHz{};
    double stepMHz{};
};

struct FrequencyPlan {
    FrequencyPlanMode mode{FrequencyPlanMode::ModelSweep};
    std::vector<double> pointsMHz;
    std::vector<FrequencyRange> ranges;
};

struct AmateurBandPreset {
    std::string_view name;
    double startMHz{};
    double endMHz{};
    double stepMHz{};
};

[[nodiscard]] auto frequencyPlanPoints(const FrequencyPlan& plan) -> std::vector<double>;
[[nodiscard]] auto amateurBandPresets() -> const std::vector<AmateurBandPreset>&;

}
