#pragma once

#include "model/Point3D.h"

#include <cstddef>

namespace necwb::model {

enum class WireEndpoint {
    Start,
    End
};

struct Wire {
    int tag{};
    Point3D start;
    Point3D end;
    int segments{};
    double radius{};
    std::size_t sourceLine{};

    auto operator==(const Wire&) const -> bool = default;
};

}
