#pragma once

#include "model/ModelSetup.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace necwb::model { class AntennaModel; }
namespace necwb::nec { class NecDocument; }

namespace necwb::model {

struct FrequencySummary {
    double startMHz{};
    double endMHz{};
    int pointCount{};
};

struct SourceSummary {
    int wireTag{};
    int segment{};
};

struct RadiationRequestSummary {
    int thetaCount{};
    int phiCount{};
};

struct ModelSummary {
    std::size_t wireCount{};
    std::size_t segmentCount{};
    std::size_t surfacePatchCount{};
    std::size_t loadCount{};
    std::size_t transmissionLineCount{};
    std::size_t symbolCount{};
    std::size_t executionRequestCount{};
    std::size_t cardCount{};
    std::vector<SourceSummary> sources;
    std::vector<RadiationRequestSummary> radiationRequests;
    std::optional<FrequencySummary> frequency;
    GroundType groundType{GroundType::FreeSpace};
};

[[nodiscard]] auto summarizeModel(const AntennaModel& model, const ModelSetup& setup,
    const nec::NecDocument& document) -> ModelSummary;

}
