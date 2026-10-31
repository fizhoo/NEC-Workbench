#include "analysis/SegmentCurrentDisplay.h"

#include "model/WireGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace necwb::analysis {

auto wireRunsAgainstMainAxis(const model::Wire& wire) noexcept -> bool
{
    if (wire.geometryKind != model::WireGeometryKind::Straight) return false;
    const std::array components{wire.end.x - wire.start.x, wire.end.y - wire.start.y,
        wire.end.z - wire.start.z};
    // The first component that is (nearly) the largest decides, so parallel diagonal wires agree.
    const auto largest = std::ranges::max(components, {}, [](double value) { return std::abs(value); });
    const auto tolerance = std::abs(largest) * 1.0e-9;
    for (const auto component : components) {
        if (std::abs(component) >= std::abs(largest) - tolerance) return component < 0.0;
    }
    return false;
}

WireSegmentIndex::WireSegmentIndex(const model::AntennaModel& model)
    : model_(&model), blocks_(model.segmentBlocks())
{
    firstSegments_.reserve(blocks_.size());
    auto next = 1;
    for (const auto& block : blocks_) {
        firstSegments_.push_back(next);
        next += std::max(block.segments, 0);
    }
}

auto WireSegmentIndex::locate(int wireTag, int necSegment) const
    -> std::optional<WireSegmentLocation>
{
    const auto after = std::ranges::upper_bound(firstSegments_, necSegment);
    if (necSegment < 1 || after == firstSegments_.begin()) return std::nullopt;
    const auto index = static_cast<std::size_t>(after - firstSegments_.begin() - 1);
    const auto& block = blocks_[index];
    const auto segment = necSegment - firstSegments_[index] + 1;
    if (block.wire == nullptr || block.tag != wireTag || segment > block.segments)
        return std::nullopt;
    return WireSegmentLocation{block.wire, block.wireIndex, segment};
}

auto WireSegmentIndex::locate(const SegmentCurrentResult& current) const
    -> std::optional<WireSegmentLocation>
{
    if (current.necSegment > 0) return locate(current.wireTag, current.necSegment);
    const auto* wire = model_->wireByTag(current.wireTag);
    if (wire == nullptr || current.segment < 1 || current.segment > wire->segments)
        return std::nullopt;
    return WireSegmentLocation{wire,
        static_cast<std::size_t>(wire - model_->wires().data()), current.segment};
}

auto displayedCurrents(std::span<const SegmentCurrentResult> currents,
    std::span<const FeedpointResult> feedpoints, const model::AntennaModel& model)
    -> DisplayedCurrents
{
    DisplayedCurrents result;
    const WireSegmentIndex index(model);
    auto referencePhase = 0.0;
    if (!feedpoints.empty()) {
        const auto& feed = feedpoints.front();
        if (std::abs(feed.current) > 0.0) {
            // The feed table reports NEC's absolute segment number. When it does not match
            // the model, no wire is guessed: the first wire with a shared tag may be the wrong one.
            const auto location = index.locate(feed.wireTag, feed.segment);
            const auto* wire = location ? location->wire : nullptr;
            referencePhase = std::arg(feed.current) * 180.0 / std::numbers::pi
                + (wire != nullptr && wireRunsAgainstMainAxis(*wire) ? 180.0 : 0.0);
            result.feedReferenced = true;
        }
    }
    result.segments.reserve(currents.size());
    for (const auto& current : currents) {
        const auto location = index.locate(current);
        if (!location) continue;
        const auto& wire = *location->wire;
        const auto endpoints = model::wireSegmentEndpoints(wire, location->segment);
        if (!endpoints) continue;
        const auto reversed = wireRunsAgainstMainAxis(wire);
        DisplayedSegmentCurrent shown;
        shown.wireTag = current.wireTag;
        shown.segment = location->segment;
        shown.wireIndex = location->wireIndex;
        shown.necSegment = current.necSegment;
        shown.start = reversed ? endpoints->second : endpoints->first;
        shown.end = reversed ? endpoints->first : endpoints->second;
        shown.magnitude = current.magnitude;
        shown.relativePhaseDegrees = std::remainder(
            current.phaseDegrees + (reversed ? 180.0 : 0.0) - referencePhase, 360.0);
        shown.reversed = reversed;
        result.maximumMagnitude = std::max(result.maximumMagnitude, shown.magnitude);
        result.segments.push_back(shown);
    }
    return result;
}

auto instantaneousCurrent(const DisplayedSegmentCurrent& current, double cycleDegrees) noexcept
    -> double
{
    return current.magnitude
        * std::cos((current.relativePhaseDegrees + cycleDegrees) * std::numbers::pi / 180.0);
}

auto dominantProjectionPlane(std::span<const DisplayedSegmentCurrent> segments)
    -> geometry::ProjectionPlane
{
    // Segment length each view shows, weighted by current, so the strongest-carrying wires
    // stay visible: a vertical over wide radials gets a side view, not a top view.
    auto xy = 0.0;
    auto xz = 0.0;
    auto yz = 0.0;
    for (const auto& segment : segments) {
        const auto dx = segment.end.x - segment.start.x;
        const auto dy = segment.end.y - segment.start.y;
        const auto dz = segment.end.z - segment.start.z;
        xy += segment.magnitude * std::hypot(dx, dy);
        xz += segment.magnitude * std::hypot(dx, dz);
        yz += segment.magnitude * std::hypot(dy, dz);
    }
    // Near-ties prefer the top view, then the front view.
    constexpr auto tieTolerance = 1.0 - 1.0e-9;
    if (xy >= std::max(xz, yz) * tieTolerance) return geometry::ProjectionPlane::XY;
    return xz >= yz * tieTolerance ? geometry::ProjectionPlane::XZ : geometry::ProjectionPlane::YZ;
}

}
