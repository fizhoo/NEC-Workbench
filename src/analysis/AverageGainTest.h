#pragma once

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

[[nodiscard]] auto prepareAverageGainTestInput(std::string_view source,
    double frequencyMHz, AverageGainEnvironment environment) -> std::string;
[[nodiscard]] auto assessAverageGain(double averagePowerGain, double expectedGain)
    -> AverageGainAssessment;

}
