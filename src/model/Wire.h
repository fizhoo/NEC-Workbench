#pragma once

#include "model/Point3D.h"

#include <cstddef>
#include <vector>

namespace necwb::model {

enum class WireEndpoint {
    Start,
    End
};

enum class WireGeometryKind {
    Straight,
    Arc,
    Helix
};

struct Wire {
    Wire() = default;
    Wire(int tagValue, Point3D startValue, Point3D endValue, int segmentCount,
        double radiusValue, std::size_t lineNumber)
        : tag(tagValue), start(startValue), end(endValue), segments(segmentCount),
          radius(radiusValue), sourceLine(lineNumber)
    {
    }

    int tag{};
    Point3D start;
    Point3D end;
    int segments{};
    double radius{};
    std::size_t sourceLine{};
    std::vector<Point3D> path;
    double endRadius{};
    WireGeometryKind geometryKind{WireGeometryKind::Straight};
    bool editable{true};

    auto operator==(const Wire&) const -> bool = default;
};

}
