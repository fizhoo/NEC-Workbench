#include "analysis/SegmentCurrentDisplay.h"

#include "model/WireGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

auto displayedCurrents(std::span<const SegmentCurrentResult> currents,
    std::span<const FeedpointResult> feedpoints, const model::AntennaModel& model)
    -> DisplayedCurrents
{
    DisplayedCurrents result;
    auto referencePhase = 0.0;
    if (!feedpoints.empty()) {
        const auto& feed = feedpoints.front();
        if (std::abs(feed.current) > 0.0) {
            const auto* wire = model.wireByTag(feed.wireTag);
            referencePhase = std::arg(feed.current) * 180.0 / std::numbers::pi
                + (wire != nullptr && wireRunsAgainstMainAxis(*wire) ? 180.0 : 0.0);
            result.feedReferenced = true;
        }
    }
    result.segments.reserve(currents.size());
    for (const auto& current : currents) {
        const auto* wire = model.wireByTag(current.wireTag);
        if (wire == nullptr) continue;
        const auto endpoints = model::wireSegmentEndpoints(*wire, current.segment);
        if (!endpoints) continue;
        const auto reversed = wireRunsAgainstMainAxis(*wire);
        DisplayedSegmentCurrent shown;
        shown.wireTag = current.wireTag;
        shown.segment = current.segment;
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

auto dominantProjectionPlane(const model::AntennaModel& model) -> geometry::ProjectionPlane
{
    constexpr auto infinity = std::numeric_limits<double>::infinity();
    model::Point3D minimum{infinity, infinity, infinity};
    model::Point3D maximum{-infinity, -infinity, -infinity};
    const auto include = [&](const model::Point3D& point) {
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y),
            std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y),
            std::max(maximum.z, point.z)};
    };
    for (const auto& wire : model.wires()) {
        for (auto index = std::size_t{}; index < model::wirePathPointCount(wire); ++index)
            include(model::wirePathPoint(wire, index));
    }
    if (minimum.x > maximum.x) return geometry::ProjectionPlane::XY;
    const auto x = maximum.x - minimum.x;
    const auto y = maximum.y - minimum.y;
    const auto z = maximum.z - minimum.z;
    if (z <= std::min(x, y)) return geometry::ProjectionPlane::XY;
    if (y <= x) return geometry::ProjectionPlane::XZ;
    return geometry::ProjectionPlane::YZ;
}

}
