#pragma once

#include "model/Wire.h"

#include <optional>
#include <cstddef>
#include <utility>

namespace necwb::model {

[[nodiscard]] auto wireSegmentEndpoints(const Wire& wire, int segment)
    -> std::optional<std::pair<Point3D, Point3D>>;
[[nodiscard]] auto wirePathPointCount(const Wire& wire) noexcept -> std::size_t;
[[nodiscard]] auto wirePathPoint(const Wire& wire, std::size_t index) noexcept -> const Point3D&;
[[nodiscard]] auto wireSegmentCenter(const Wire& wire, int segment) -> std::optional<Point3D>;
[[nodiscard]] auto wireLength(const Wire& wire) noexcept -> double;
[[nodiscard]] auto wireMinimumSegmentLength(const Wire& wire) noexcept -> double;
[[nodiscard]] auto wireMaximumRadius(const Wire& wire) noexcept -> double;

}
