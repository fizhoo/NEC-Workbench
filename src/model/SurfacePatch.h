#pragma once

#include "model/Point3D.h"

#include <cstddef>
#include <vector>

namespace necwb::model {

enum class SurfacePatchKind {
    Arbitrary,
    Rectangular,
    Triangular,
    Quadrilateral,
    GridCell,
};

struct SurfacePatch {
    SurfacePatchKind kind{SurfacePatchKind::Arbitrary};
    std::vector<Point3D> corners;
    std::size_t sourceLine{};
};

}
