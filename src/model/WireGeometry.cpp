#include "model/WireGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace necwb::model {
namespace {

auto distance(const Point3D& first, const Point3D& second) noexcept -> double
{
    return std::hypot(first.x - second.x, first.y - second.y, first.z - second.z);
}

auto interpolate(const Point3D& first, const Point3D& second, double amount) noexcept -> Point3D
{
    return {first.x + (second.x - first.x) * amount,
        first.y + (second.y - first.y) * amount,
        first.z + (second.z - first.z) * amount};
}

}

auto wireSegmentEndpoints(const Wire& wire, int segment)
    -> std::optional<std::pair<Point3D, Point3D>>
{
    if (segment < 1 || segment > wire.segments) return std::nullopt;
    if (wire.path.size() == static_cast<std::size_t>(wire.segments + 1)) {
        return std::pair{wire.path[static_cast<std::size_t>(segment - 1)],
            wire.path[static_cast<std::size_t>(segment)]};
    }
    const auto start = static_cast<double>(segment - 1) / wire.segments;
    const auto end = static_cast<double>(segment) / wire.segments;
    return std::pair{interpolate(wire.start, wire.end, start),
        interpolate(wire.start, wire.end, end)};
}

auto wirePathPointCount(const Wire& wire) noexcept -> std::size_t
{
    return wire.path.size() >= 2 ? wire.path.size() : 2;
}

auto wirePathPoint(const Wire& wire, std::size_t index) noexcept -> const Point3D&
{
    if (wire.path.size() >= 2) return wire.path[index];
    return index == 0 ? wire.start : wire.end;
}

auto wireSegmentCenter(const Wire& wire, int segment) -> std::optional<Point3D>
{
    const auto endpoints = wireSegmentEndpoints(wire, segment);
    if (!endpoints) return std::nullopt;
    return interpolate(endpoints->first, endpoints->second, 0.5);
}

auto wireLength(const Wire& wire) noexcept -> double
{
    if (wire.path.size() >= 2) {
        auto length = 0.0;
        for (auto index = std::size_t{1}; index < wire.path.size(); ++index)
            length += distance(wire.path[index - 1], wire.path[index]);
        return length;
    }
    return distance(wire.start, wire.end);
}

auto wireMinimumSegmentLength(const Wire& wire) noexcept -> double
{
    if (wire.segments <= 0) return 0.0;
    if (wire.path.size() == static_cast<std::size_t>(wire.segments + 1)) {
        auto minimum = std::numeric_limits<double>::infinity();
        for (auto index = std::size_t{1}; index < wire.path.size(); ++index)
            minimum = std::min(minimum, distance(wire.path[index - 1], wire.path[index]));
        return std::isfinite(minimum) ? minimum : 0.0;
    }
    return wireLength(wire) / wire.segments;
}

auto wireMaximumRadius(const Wire& wire) noexcept -> double
{
    return std::max(wire.radius, wire.endRadius > 0.0 ? wire.endRadius : wire.radius);
}

}
