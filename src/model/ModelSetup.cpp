#include "model/ModelSetup.h"

#include "model/AntennaModel.h"
#include "model/WireGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace necwb::model {

auto referenceImpedanceOhms(const ModelSetup& setup) -> double
{
    return setup.referenceImpedance ? setup.referenceImpedance->ohms : 50.0;
}

auto frequencyEndMHz(const FrequencyDefinition& frequency) -> double
{
    if (frequency.count <= 1) {
        return frequency.startMHz;
    }
    if (frequency.steppingMode == 1) {
        return frequency.startMHz * std::pow(frequency.step, frequency.count - 1);
    }
    return frequency.startMHz + (frequency.count - 1) * frequency.step;
}

auto frequencyPointCount(int steppingMode, double startMHz, double endMHz, double step)
    -> std::optional<int>
{
    if (startMHz <= 0.0 || endMHz < startMHz || (steppingMode != 0 && steppingMode != 1)) {
        return std::nullopt;
    }
    if (endMHz == startMHz) {
        return 1;
    }
    if ((steppingMode == 0 && step <= 0.0) || (steppingMode == 1 && step <= 1.0)) {
        return std::nullopt;
    }
    const auto intervals = steppingMode == 1
        ? std::log(endMHz / startMHz) / std::log(step)
        : (endMHz - startMHz) / step;
    const auto count = std::floor(intervals + 1.0e-10) + 1.0;
    if (!std::isfinite(count) || count > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return std::max(1, static_cast<int>(count));
}

auto excitationPosition(const AntennaModel& model, const Excitation& excitation)
    -> std::optional<Point3D>
{
    return wireSegmentPosition(model, excitation.wireTag, excitation.segment);
}

auto referencedSegment(const AntennaModel& model, int wireTag, int segment)
    -> std::optional<SegmentReference>
{
    if (segment < 1) return std::nullopt;
    auto remaining = segment;
    for (const auto& block : model.segmentBlocks()) {
        if (wireTag != 0 && block.tag != wireTag) continue;
        if (remaining <= block.segments) return SegmentReference{block.wire, remaining};
        remaining -= std::max(block.segments, 0);
    }
    return std::nullopt;
}

auto referencedSegmentCount(const AntennaModel& model, int wireTag) -> int
{
    auto count = 0;
    for (const auto& block : model.segmentBlocks())
        if (wireTag == 0 || block.tag == wireTag) count += std::max(block.segments, 0);
    return count;
}

auto hasSharedWireTags(const AntennaModel& model) -> bool
{
    std::set<int> tags;
    return std::ranges::any_of(model.segmentBlocks(),
        [&tags](const auto& block) { return !tags.insert(block.tag).second; });
}

auto hasCrossWireSegmentReferences(const AntennaModel& model, const ModelSetup& setup) -> bool
{
    return hasSharedWireTags(model)
        || std::ranges::any_of(setup.excitations, [](const auto& value) { return value.wireTag == 0; })
        || std::ranges::any_of(setup.loads, [](const auto& value) {
               return value.wireTag == 0 && (value.firstSegment > 0 || value.lastSegment > 0);
           })
        || std::ranges::any_of(setup.transmissionLines,
            [](const auto& value) { return value.wireTag1 == 0 || value.wireTag2 == 0; });
}

auto wireSegmentPosition(const AntennaModel& model, int wireTag, int segment)
    -> std::optional<Point3D>
{
    const auto reference = referencedSegment(model, wireTag, segment);
    if (!reference || reference->wire == nullptr) return std::nullopt;
    return wireSegmentCenter(*reference->wire, reference->segment);
}

auto loadPosition(const AntennaModel& model, const LoadDefinition& load)
    -> std::optional<Point3D>
{
    // A tag-0 load without a range covers the whole structure and has no single position.
    if (load.wireTag == 0 && load.firstSegment == 0 && load.lastSegment == 0) return std::nullopt;
    const auto count = referencedSegmentCount(model, load.wireTag);
    const auto first = load.firstSegment == 0 ? 1 : load.firstSegment;
    const auto last = load.lastSegment == 0 ? count : load.lastSegment;
    if (first < 1 || last < first || last > count) return std::nullopt;
    const auto total = first + last;
    if (total % 2 == 0) return wireSegmentPosition(model, load.wireTag, total / 2);
    const auto lower = referencedSegment(model, load.wireTag, total / 2);
    if (!lower || lower->wire == nullptr) return std::nullopt;
    const auto endpoints = wireSegmentEndpoints(*lower->wire, lower->segment);
    return endpoints ? std::optional{endpoints->second} : std::nullopt;
}

}
