#pragma once

#include "analysis/AnalysisResult.h"
#include "geometry/OrthographicProjection.h"
#include "model/AntennaModel.h"

#include <span>
#include <vector>

namespace necwb::analysis {

// A segment current prepared for drawing on the antenna: positioned, signed for a common
// physical direction, and phased relative to the feed.
struct DisplayedSegmentCurrent {
    int wireTag{};
    int segment{};
    // Segment endpoints ordered in the displayed current direction.
    model::Point3D start;
    model::Point3D end;
    double magnitude{};
    // Relative to the feed current when the run has one, otherwise NEC's absolute phase.
    double relativePhaseDegrees{};
    // The wire runs against its main axis, so its NEC current is shown negated.
    bool reversed{};
};

struct DisplayedCurrents {
    std::vector<DisplayedSegmentCurrent> segments;
    double maximumMagnitude{};
    bool feedReferenced{};
};

// NEC reports current flowing from end 1 to end 2, so two wires entered in opposite
// directions appear 180° apart even when their physical currents agree. Straight wires are
// therefore shown flowing toward the positive end of their main axis (+X, +Y or +Z); curved
// wires keep NEC's direction so the current stays continuous around the curve.
[[nodiscard]] auto wireRunsAgainstMainAxis(const model::Wire& wire) noexcept -> bool;

// Currents and feedpoints must already be filtered to one frequency. Segments whose wire is
// missing from the model are skipped.
[[nodiscard]] auto displayedCurrents(std::span<const SegmentCurrentResult> currents,
    std::span<const FeedpointResult> feedpoints, const model::AntennaModel& model)
    -> DisplayedCurrents;

// Current at one moment of the RF cycle: |I| cos(phase + cycle angle). At 0° the feed
// current is at its positive peak.
[[nodiscard]] auto instantaneousCurrent(const DisplayedSegmentCurrent& current,
    double cycleDegrees) noexcept -> double;

// The plane the antenna mostly lies in: the one perpendicular to its smallest extent,
// preferring a top view for flat horizontal antennas.
[[nodiscard]] auto dominantProjectionPlane(const model::AntennaModel& model)
    -> geometry::ProjectionPlane;

}
