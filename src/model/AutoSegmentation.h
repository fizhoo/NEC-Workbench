#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <vector>

namespace necwb::model {

struct SegmentationSettings {
    int segmentsPerWavelength{20};
    bool oddForExcitedWires{true};
};

struct WireSegmentationProposal {
    int wireTag{};
    int oldSegments{};
    int newSegments{};
    double wireLengthMeters{};
    double segmentLengthMeters{};
};

struct SegmentationProposal {
    std::vector<WireSegmentationProposal> wires;
    std::vector<Excitation> remappedExcitations;
};

[[nodiscard]] auto proposeSegmentation(const AntennaModel& model,
    const std::vector<Excitation>& excitations, double maximumFrequencyMHz,
    const SegmentationSettings& settings) -> SegmentationProposal;

}
