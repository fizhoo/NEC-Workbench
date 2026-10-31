#pragma once

#include "model/Wire.h"
#include "model/SurfacePatch.h"

#include <cstddef>
#include <span>
#include <vector>

namespace necwb::model {

// Geometry the solver numbers but Workbench does not draw, such as a flat-spiral GH.
struct UnrenderedSegments {
    // Index of the drawn wire that follows it in NEC's segment order.
    std::size_t beforeWire{};
    int tag{};
    int segments{};
    std::size_t sourceLine{};

    auto operator==(const UnrenderedSegments&) const -> bool = default;
};

// One run of consecutive NEC segment numbers: a drawn wire, or unrendered geometry when
// wire is null.
struct SegmentBlock {
    const Wire* wire{};
    std::size_t wireIndex{};
    int tag{};
    int segments{};
};

class AntennaModel {
public:
    void addWire(Wire wire);
    void addSurfacePatch(SurfacePatch patch);
    // Placed before the next wire added.
    void addUnrenderedSegments(int tag, int segments, std::size_t sourceLine);
    void scale(double factor) noexcept;
    [[nodiscard]] auto wireByTag(int tag) noexcept -> Wire*;
    [[nodiscard]] auto wireByTag(int tag) const noexcept -> const Wire*;
    [[nodiscard]] auto wires() const noexcept -> std::span<const Wire>;
    [[nodiscard]] auto surfacePatches() const noexcept -> std::span<const SurfacePatch>;
    [[nodiscard]] auto unrenderedSegments() const noexcept -> std::span<const UnrenderedSegments>;
    // Wires and unrendered geometry in NEC's segment numbering order.
    [[nodiscard]] auto segmentBlocks() const -> std::vector<SegmentBlock>;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto wireCount() const noexcept -> std::size_t;
    [[nodiscard]] auto surfacePatchCount() const noexcept -> std::size_t;

private:
    std::vector<Wire> wires_;
    std::vector<SurfacePatch> surfacePatches_;
    std::vector<UnrenderedSegments> unrenderedSegments_;
};

}
