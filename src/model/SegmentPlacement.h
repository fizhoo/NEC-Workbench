#pragma once

#include "model/Wire.h"

#include <optional>

namespace necwb::model {

// A point along a wire, as a fraction of its length from End 1, and the segment NEC
// Workbench puts there.
struct AlongWirePlacement {
    int segment{};
    int segmentCount{};
    double requestedFraction{};
    // The middle of the chosen segment, as a fraction of the wire's length from End 1.
    double actualFraction{};
    // The requested point is the joint between `segment` and `segment + 1`; the segment
    // nearer End 1 is used.
    bool onJoint{};
};

// The segment that contains the point `fraction` of the way along the wire from End 1,
// measured along the wire, so arcs, helices and tapered segments are placed by length.
// 0 gives segment 1 and 1 gives the last segment. With equal segments the one containing the
// point is also the one whose middle is nearest to it. A point on the joint between two
// segments, the only tie, takes the one nearer End 1; points within 1e-9 of the wire's length
// of a joint count as on it. None for a fraction outside 0 to 1 or a wire without length.
[[nodiscard]] auto placeAlongWire(const Wire& wire, double fraction)
    -> std::optional<AlongWirePlacement>;
// The middle of a segment, as a fraction of the wire's length from End 1, or none for a
// segment the wire does not have.
[[nodiscard]] auto segmentMiddleFraction(const Wire& wire, int segment) -> std::optional<double>;

}
