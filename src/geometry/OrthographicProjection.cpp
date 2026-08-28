#include "geometry/OrthographicProjection.h"

#include <algorithm>
#include <cmath>

namespace necwb::geometry {

auto Bounds2D::width() const noexcept -> double
{
    return maximumHorizontal - minimumHorizontal;
}

auto Bounds2D::height() const noexcept -> double
{
    return maximumVertical - minimumVertical;
}

auto Bounds2D::center() const noexcept -> Point2D
{
    return {(minimumHorizontal + maximumHorizontal) / 2.0, (minimumVertical + maximumVertical) / 2.0};
}

auto project(const model::Point3D& point, ProjectionPlane plane) noexcept -> Point2D
{
    switch (plane) {
    case ProjectionPlane::XY:
        return {point.x, point.y};
    case ProjectionPlane::XZ:
        return {point.x, point.z};
    case ProjectionPlane::YZ:
        return {point.y, point.z};
    }
    return {};
}

auto withProjectedCoordinates(const model::Point3D& original, const Point2D& projected,
    ProjectionPlane plane) noexcept -> model::Point3D
{
    auto result = original;
    switch (plane) {
    case ProjectionPlane::XY:
        result.x = projected.horizontal;
        result.y = projected.vertical;
        break;
    case ProjectionPlane::XZ:
        result.x = projected.horizontal;
        result.z = projected.vertical;
        break;
    case ProjectionPlane::YZ:
        result.y = projected.horizontal;
        result.z = projected.vertical;
        break;
    }
    return result;
}

auto projectedBounds(std::span<const model::Wire> wires, ProjectionPlane plane) -> std::optional<Bounds2D>
{
    if (wires.empty()) {
        return std::nullopt;
    }

    const auto first = project(wires.front().start, plane);
    Bounds2D bounds{first.horizontal, first.horizontal, first.vertical, first.vertical};
    for (const auto& wire : wires) {
        for (const auto& point : {project(wire.start, plane), project(wire.end, plane)}) {
            bounds.minimumHorizontal = std::min(bounds.minimumHorizontal, point.horizontal);
            bounds.maximumHorizontal = std::max(bounds.maximumHorizontal, point.horizontal);
            bounds.minimumVertical = std::min(bounds.minimumVertical, point.vertical);
            bounds.maximumVertical = std::max(bounds.maximumVertical, point.vertical);
        }
    }
    return bounds;
}

auto distanceToSegment(const Point2D& point, const Point2D& start, const Point2D& end) noexcept -> double
{
    const auto deltaHorizontal = end.horizontal - start.horizontal;
    const auto deltaVertical = end.vertical - start.vertical;
    const auto lengthSquared = deltaHorizontal * deltaHorizontal + deltaVertical * deltaVertical;
    if (lengthSquared == 0.0) {
        return std::hypot(point.horizontal - start.horizontal, point.vertical - start.vertical);
    }

    const auto clamped = closestSegmentParameter(point, start, end);
    const Point2D closest{
        start.horizontal + clamped * deltaHorizontal,
        start.vertical + clamped * deltaVertical};
    return std::hypot(point.horizontal - closest.horizontal, point.vertical - closest.vertical);
}

auto closestSegmentParameter(const Point2D& point, const Point2D& start, const Point2D& end) noexcept
    -> double
{
    const auto deltaHorizontal = end.horizontal - start.horizontal;
    const auto deltaVertical = end.vertical - start.vertical;
    const auto lengthSquared = deltaHorizontal * deltaHorizontal + deltaVertical * deltaVertical;
    if (lengthSquared == 0.0) {
        return 0.0;
    }
    const auto projection = ((point.horizontal - start.horizontal) * deltaHorizontal
        + (point.vertical - start.vertical) * deltaVertical) / lengthSquared;
    return std::clamp(projection, 0.0, 1.0);
}

}
