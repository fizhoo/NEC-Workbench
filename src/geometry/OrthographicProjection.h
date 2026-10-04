#pragma once

#include "model/Point3D.h"
#include "model/Wire.h"

namespace necwb::model { class AntennaModel; }

#include <optional>
#include <span>

namespace necwb::geometry {

enum class ProjectionPlane {
    XY,
    XZ,
    YZ
};

struct Point2D {
    double horizontal{};
    double vertical{};

    auto operator==(const Point2D&) const -> bool = default;
};

struct Bounds2D {
    double minimumHorizontal{};
    double maximumHorizontal{};
    double minimumVertical{};
    double maximumVertical{};

    [[nodiscard]] auto width() const noexcept -> double;
    [[nodiscard]] auto height() const noexcept -> double;
    [[nodiscard]] auto center() const noexcept -> Point2D;
};

[[nodiscard]] auto project(const model::Point3D& point, ProjectionPlane plane) noexcept -> Point2D;
[[nodiscard]] auto withProjectedCoordinates(const model::Point3D& original, const Point2D& projected,
    ProjectionPlane plane) noexcept -> model::Point3D;
[[nodiscard]] auto projectedBounds(std::span<const model::Wire> wires, ProjectionPlane plane)
    -> std::optional<Bounds2D>;
[[nodiscard]] auto projectedBounds(const model::AntennaModel& model, ProjectionPlane plane)
    -> std::optional<Bounds2D>;
[[nodiscard]] auto distanceToSegment(const Point2D& point, const Point2D& start, const Point2D& end) noexcept
    -> double;
[[nodiscard]] auto closestSegmentParameter(const Point2D& point, const Point2D& start,
    const Point2D& end) noexcept -> double;

}
