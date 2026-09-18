#include "model/AutoSegmentation.h"

#include "model/WireGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace necwb::model {
namespace {

constexpr auto SpeedOfLightMetersPerSecond = 299792458.0;

auto remappedSegment(int oldSegment, int oldCount, int newCount) -> int
{
    const auto position = (std::clamp(oldSegment, 1, oldCount) - 0.5) / oldCount;
    return std::clamp(static_cast<int>(std::floor(position * newCount)) + 1, 1, newCount);
}

}

auto proposeSegmentation(const AntennaModel& model, const std::vector<Excitation>& excitations,
    double maximumFrequencyMHz, const SegmentationSettings& settings,
    const std::vector<LoadDefinition>& loads,
    const std::vector<TransmissionLineDefinition>& transmissionLines) -> SegmentationProposal
{
    SegmentationProposal proposal;
    if (maximumFrequencyMHz <= 0.0 || settings.segmentsPerWavelength <= 0) {
        return proposal;
    }
    const auto wavelength = SpeedOfLightMetersPerSecond / (maximumFrequencyMHz * 1.0e6);
    for (const auto& wire : model.wires()) {
        if (!wire.editable) continue;
        const auto length = wireLength(wire);
        const auto exactCount = std::ceil(length / wavelength * settings.segmentsPerWavelength);
        auto newSegments = static_cast<int>(std::clamp(exactCount, 1.0,
            static_cast<double>(std::numeric_limits<int>::max())));
        const auto excited = std::ranges::any_of(excitations,
                [tag = wire.tag](const auto& excitation) { return excitation.wireTag == tag; })
            || std::ranges::any_of(transmissionLines, [tag = wire.tag](const auto& line) {
                return line.wireTag1 == tag || line.wireTag2 == tag;
            });
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
    const auto remap = [&model, &proposal](int wireTag, int segment) {
        if (segment == 0) return 0;
        const auto* wire = model.wireByTag(wireTag);
        const auto found = std::ranges::find(proposal.wires, wireTag,
            &WireSegmentationProposal::wireTag);
        return wire == nullptr || found == proposal.wires.end()
            ? segment : remappedSegment(segment, wire->segments, found->newSegments);
    };
    proposal.remappedLoads = loads;
    for (auto& load : proposal.remappedLoads) {
        load.firstSegment = remap(load.wireTag, load.firstSegment);
        load.lastSegment = remap(load.wireTag, load.lastSegment);
        if (load.firstSegment != 0 && load.lastSegment != 0
            && load.firstSegment > load.lastSegment) {
            std::swap(load.firstSegment, load.lastSegment);
        }
    }
    proposal.remappedTransmissionLines = transmissionLines;
    for (auto& line : proposal.remappedTransmissionLines) {
        line.segment1 = remap(line.wireTag1, line.segment1);
        line.segment2 = remap(line.wireTag2, line.segment2);
    }
    return proposal;
}

}
