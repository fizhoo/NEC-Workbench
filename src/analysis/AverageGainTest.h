#pragma once

#include "analysis/AnalysisResult.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace necwb::analysis {

enum class AverageGainEnvironment {
    FreeSpace,
    PerfectGround
};

enum class AverageGainClassification {
    Pass,
    Usable,
    Caution,
    Questionable
};

struct AverageGainAssessment {
    double averagePowerGain{};
    double expectedGain{1.0};
    double normalizedGain{};
    double gainAdjustmentDb{};
    AverageGainClassification classification{AverageGainClassification::Questionable};
};

struct IntegratedAverageGain {
    double averagePowerGain{};
    double solidAnglePi{};
};

[[nodiscard]] auto prepareAverageGainTestInput(std::string_view source,
    double frequencyMHz, AverageGainEnvironment environment) -> std::string;
[[nodiscard]] auto assessAverageGain(double averagePowerGain, double expectedGain)
    -> AverageGainAssessment;
[[nodiscard]] auto integrateAverageGain(std::span<const RadiationSample> samples,
    AverageGainEnvironment environment) -> std::optional<IntegratedAverageGain>;

}
