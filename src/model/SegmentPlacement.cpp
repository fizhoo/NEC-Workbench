#include "model/SegmentPlacement.h"

#include "model/WireGeometry.h"

#include <cmath>
#include <vector>

namespace necwb::model {
namespace {

// Distance from End 1 to the end of each segment, or none for a wire without length.
auto segmentEnds(const Wire& wire) -> std::optional<std::vector<double>>
{
    if (wire.segments < 1) return std::nullopt;
    std::vector<double> ends;
    ends.reserve(static_cast<std::size_t>(wire.segments));
    auto travelled = 0.0;
    for (auto segment = 1; segment <= wire.segments; ++segment) {
        const auto endpoints = wireSegmentEndpoints(wire, segment);
        if (!endpoints) return std::nullopt;
        travelled += std::hypot(endpoints->second.x - endpoints->first.x,
            endpoints->second.y - endpoints->first.y, endpoints->second.z - endpoints->first.z);
        ends.push_back(travelled);
    }
    if (!(travelled > 0.0)) return std::nullopt;
    return ends;
}

auto middle(const std::vector<double>& ends, int segment) -> double
{
    const auto start = segment == 1 ? 0.0 : ends[static_cast<std::size_t>(segment - 2)];
    return (start + ends[static_cast<std::size_t>(segment - 1)]) / 2.0 / ends.back();
}

}

auto placeAlongWire(const Wire& wire, double fraction) -> std::optional<AlongWirePlacement>
{
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction > 1.0) return std::nullopt;
    const auto ends = segmentEnds(wire);
    if (!ends) return std::nullopt;
    const auto target = fraction * ends->back();
    const auto tolerance = 1e-9 * ends->back();
    AlongWirePlacement placement{wire.segments, wire.segments, fraction, 0.0, false};
    for (auto index = 0; index < wire.segments; ++index) {
        const auto end = (*ends)[static_cast<std::size_t>(index)];
        if (target <= end + tolerance) {
            placement.segment = index + 1;
            placement.onJoint = index + 1 < wire.segments && std::abs(target - end) <= tolerance;
            break;
        }
    }
    placement.actualFraction = middle(*ends, placement.segment);
    return placement;
}

auto segmentMiddleFraction(const Wire& wire, int segment) -> std::optional<double>
{
    const auto ends = segmentEnds(wire);
    if (!ends || segment < 1 || segment > wire.segments) return std::nullopt;
    return middle(*ends, segment);
}

}
