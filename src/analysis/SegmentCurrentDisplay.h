#pragma once

#include "analysis/AnalysisResult.h"
#include "geometry/OrthographicProjection.h"
#include "model/AntennaModel.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace necwb::analysis {

// A segment current prepared for drawing on the antenna: positioned, signed for a common
// physical direction, and phased relative to the feed.
struct DisplayedSegmentCurrent {
    int wireTag{};
    // Segment within its wire, counted from end 1.
    int segment{};
    // Position of the wire in GW order, which tells apart wires that share a tag.
    std::size_t wireIndex{};
    // NEC's absolute segment number, 0 when the run did not report one.
    int necSegment{};
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

struct WireSegmentLocation {
    const model::Wire* wire{};
    std::size_t wireIndex{};
    // Segment within the wire, counted from end 1.
    int segment{};
};

// Maps solver segment numbers to model wires. NEC numbers segments consecutively through the
// wires in the order they are generated, which is the model's wire order, so the absolute
// number identifies the wire even when several wires share a tag (tag 0 is common). The
// located wire must also carry the reported tag, so a model that no longer matches the
// solved deck finds nothing rather than the wrong wire. The model must outlive the index.
class WireSegmentIndex {
public:
    explicit WireSegmentIndex(const model::AntennaModel& model);

    [[nodiscard]] auto locate(int wireTag, int necSegment) const -> std::optional<WireSegmentLocation>;
    // Uses the absolute segment number when the current has one, otherwise the segment
    // within the first wire with that tag.
    [[nodiscard]] auto locate(const SegmentCurrentResult& current) const
        -> std::optional<WireSegmentLocation>;

private:
    const model::AntennaModel* model_{};
    // Wires and unrendered geometry in NEC order, with each block's first NEC segment number.
    std::vector<model::SegmentBlock> blocks_;
    std::vector<int> firstSegments_;
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

// The plane that shows the most current: segment lengths as seen in each view, weighted by
// current magnitude. Ties prefer a top view, so flat horizontal antennas are seen from above.
[[nodiscard]] auto dominantProjectionPlane(std::span<const DisplayedSegmentCurrent> segments)
    -> geometry::ProjectionPlane;

}
