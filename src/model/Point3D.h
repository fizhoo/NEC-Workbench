#pragma once

namespace necwb::model {

struct Point3D {
    double x{};
    double y{};
    double z{};

    auto operator==(const Point3D&) const -> bool = default;
};

}
