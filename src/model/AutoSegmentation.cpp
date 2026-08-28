#include "model/AutoSegmentation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace necwb::model {
namespace {

constexpr auto SpeedOfLightMetersPerSecond = 299792458.0;

auto wireLength(const Wire& wire) -> double
{
    return std::sqrt(
        std::pow(wire.end.x - wire.start.x, 2)
        + std::pow(wire.end.y - wire.start.y, 2)
        + std::pow(wire.end.z - wire.start.z, 2));
}

auto remappedSegment(int oldSegment, int oldCount, int newCount) -> int
{
    const auto position = (std::clamp(oldSegment, 1, oldCount) - 0.5) / oldCount;
    return std::clamp(static_cast<int>(std::floor(position * newCount)) + 1, 1, newCount);
}

}

auto proposeSegmentation(const AntennaModel& model, const std::vector<Excitation>& excitations,
    double maximumFrequencyMHz, const SegmentationSettings& settings) -> SegmentationProposal
{
    SegmentationProposal proposal;
    if (maximumFrequencyMHz <= 0.0 || settings.segmentsPerWavelength <= 0) {
        return proposal;
    }
    const auto wavelength = SpeedOfLightMetersPerSecond / (maximumFrequencyMHz * 1.0e6);
    for (const auto& wire : model.wires()) {
        const auto length = wireLength(wire);
        const auto exactCount = std::ceil(length / wavelength * settings.segmentsPerWavelength);
        auto newSegments = static_cast<int>(std::clamp(exactCount, 1.0,
            static_cast<double>(std::numeric_limits<int>::max())));
        const auto excited = std::ranges::any_of(excitations,
            [tag = wire.tag](const auto& excitation) { return excitation.wireTag == tag; });
        if (settings.oddForExcitedWires && excited && newSegments % 2 == 0
            && newSegments < std::numeric_limits<int>::max()) {
            ++newSegments;
        }
        proposal.wires.push_back({wire.tag, wire.segments, newSegments, length,
            length / newSegments});
    }
    proposal.remappedExcitations = excitations;
    for (auto& excitation : proposal.remappedExcitations) {
        const auto* wire = model.wireByTag(excitation.wireTag);
        const auto found = std::ranges::find(proposal.wires, excitation.wireTag,
            &WireSegmentationProposal::wireTag);
        if (wire != nullptr && found != proposal.wires.end()) {
            excitation.segment = remappedSegment(excitation.segment, wire->segments,
                found->newSegments);
        }
    }
    return proposal;
}

}
