#include "nec/NecModelChecker.h"

#include "nec/NecCardCatalog.h"
#include "nec/NecModelConverter.h"
#include "nec/NecSetupConverter.h"
#include "nec/ParseNumber.h"
#include "model/WireGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace necwb::nec {
namespace {

template<typename Value>
auto isNumber(std::string_view text) -> bool
{
    Value value{};
    return parseNumber(text, value);
}

void addError(ModelCheckResult& result, const NecCard& card, std::string message)
{
    result.diagnostics.push_back(
        {DiagnosticSeverity::Error, card.lineNumber, std::move(message), "Card validation"});
}

void addSequenceDiagnostic(ModelCheckResult& result, DiagnosticSeverity severity,
    const NecCard& card, std::string message)
{
    result.diagnostics.push_back(
        {severity, card.lineNumber, std::move(message), "Control sequence"});
}

void addAdequacyDiagnostic(ModelCheckResult& result, DiagnosticSeverity severity,
    std::size_t lineNumber, std::string message)
{
    result.diagnostics.push_back({severity, lineNumber, std::move(message), "Model adequacy"});
}

void addAdequacyWarning(ModelCheckResult& result, std::size_t lineNumber, std::string message)
{
    addAdequacyDiagnostic(result, DiagnosticSeverity::Warning, lineNumber, std::move(message));
}

// NEC-2 CONECT treats a segment end as touching another end, or the ground at z = 0, within
// this fraction of the segment's own length (SMIN), measured as |dx| + |dy| + |dz|.
constexpr auto kNecContactFraction = 1.0e-3;

// NEC applies each segment's own tolerance, so a long segment can reach a short one that does
// not reach back. Ends count as joined only when both segments agree; a one-way reach stops
// NEC-2 and is reported by checkNecJunctions().
auto necTouching(const model::Point3D& first, double firstSegmentLength,
    const model::Point3D& second, double secondSegmentLength) -> bool
{
    const auto separation = std::abs(first.x - second.x) + std::abs(first.y - second.y)
        + std::abs(first.z - second.z);
    return separation <= kNecContactFraction * std::min(firstSegmentLength, secondSegmentLength);
}

// Where CONECT places a segment end relative to the ground plane under GE 1.
auto necOnGround(double z, double segmentLength) -> bool
{
    return std::abs(z) <= kNecContactFraction * segmentLength;
}

auto necBelowGround(double z, double segmentLength) -> bool
{
    return z < -kNecContactFraction * segmentLength;
}

auto concise(double value) -> std::string
{
    std::ostringstream stream;
    stream << std::setprecision(4) << value;
    return stream.str();
}

auto location(const model::Point3D& point) -> std::string
{
    // Rounding to centimeters; avoid printing -0.00.
    const auto rounded = [](double value) { return std::abs(value) < 0.005 ? 0.0 : value; };
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << '(' << rounded(point.x) << ", "
           << rounded(point.y) << ", " << rounded(point.z) << ") m";
    return stream.str();
}

// Static-check thresholds. These are NEC-2 modeling guidance, not hard solver limits.

// GN 0 approximates the ground with reflection coefficients, which loses accuracy for wires
// near the ground, especially horizontal ones. Below this height GN 2 (Sommerfeld-Norton) is
// the safer choice.
constexpr auto kReflectionGroundMinimumHeightWavelengths = 0.1;
// Segments within this elevation angle of the ground plane count as near-horizontal.
constexpr auto kNearHorizontalDegrees = 30.0;
// Wires within this angle of each other count as parallel.
constexpr auto kParallelAngleDegrees = 5.0;
// Parallel wires count as closely spaced below the larger of these two separations.
constexpr auto kCloseSpacingRadii = 10.0;
constexpr auto kCloseSpacingWavelengths = 0.01;
// Closely spaced parallel segment junctions may be offset by this fraction of a segment.
constexpr auto kSegmentAlignmentFraction = 0.1;
// Radius ratio at a junction above which a source, load, or TL end counts as beside a step.
constexpr auto kRadiusStepRatio = 1.1;
// A TL length may fall this fraction short of the straight-line gap before it is flagged.
constexpr auto kTransmissionLineLengthTolerance = 0.01;

// Buried wires and offset parallel segments are NEC-2 limitations that NEC-4 and later
// handle. Every backend Workbench drives today uses a NEC-2 kernel.
auto usesNec2Kernel(CompatibilityProfile profile) noexcept -> bool
{
    switch (profile) {
    case CompatibilityProfile::OriginalNec2:
    case CompatibilityProfile::FourNec2:
    case CompatibilityProfile::OpenNec:
        return true;
    }
    return true;
}

auto difference(const model::Point3D& first, const model::Point3D& second) noexcept
    -> model::Point3D
{
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

auto dot(const model::Point3D& first, const model::Point3D& second) noexcept -> double
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

auto norm(const model::Point3D& vector) noexcept -> double
{
    return std::hypot(vector.x, vector.y, vector.z);
}

auto along(const model::Point3D& origin, const model::Point3D& direction, double amount) noexcept
    -> model::Point3D
{
    return {origin.x + direction.x * amount, origin.y + direction.y * amount,
        origin.z + direction.z * amount};
}

struct ClosestApproach {
    double distance{};
    // Position along each segment, 0 at its start and 1 at its end.
    double first{};
    double second{};
    model::Point3D midpoint;
};

// Closest points between two line segments (Ericson, Real-Time Collision Detection, 5.1.9).
auto closestApproach(const model::Point3D& firstStart, const model::Point3D& firstEnd,
    const model::Point3D& secondStart, const model::Point3D& secondEnd) noexcept
    -> ClosestApproach
{
    const auto firstDirection = difference(firstEnd, firstStart);
    const auto secondDirection = difference(secondEnd, secondStart);
    const auto offset = difference(firstStart, secondStart);
    const auto a = dot(firstDirection, firstDirection);
    const auto e = dot(secondDirection, secondDirection);
    const auto f = dot(secondDirection, offset);
    auto s = 0.0;
    auto t = 0.0;
    if (a > 0.0 && e > 0.0) {
        const auto b = dot(firstDirection, secondDirection);
        const auto c = dot(firstDirection, offset);
        const auto denominator = a * e - b * b;
        s = denominator > 0.0 ? std::clamp((b * f - c * e) / denominator, 0.0, 1.0) : 0.0;
        t = (b * s + f) / e;
        if (t < 0.0) {
            t = 0.0;
            s = std::clamp(-c / a, 0.0, 1.0);
        } else if (t > 1.0) {
            t = 1.0;
            s = std::clamp((b - c) / a, 0.0, 1.0);
        }
    } else if (a > 0.0) {
        s = std::clamp(-dot(firstDirection, offset) / a, 0.0, 1.0);
    } else if (e > 0.0) {
        t = std::clamp(f / e, 0.0, 1.0);
    }
    const auto firstPoint = along(firstStart, firstDirection, s);
    const auto secondPoint = along(secondStart, secondDirection, t);
    return {norm(difference(firstPoint, secondPoint)), s, t,
        along(firstPoint, difference(secondPoint, firstPoint), 0.5)};
}

auto radiusAt(const model::Wire& wire, double fraction) noexcept -> double
{
    if (wire.endRadius <= 0.0) return wire.radius;
    return wire.radius + (wire.endRadius - wire.radius) * fraction;
}

struct Cell {
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t z{};
    auto operator==(const Cell&) const -> bool = default;
};

struct CellHash {
    auto operator()(const Cell& cell) const noexcept -> std::size_t
    {
        auto hash = static_cast<std::size_t>(cell.x) * 73856093u;
        hash ^= static_cast<std::size_t>(cell.y) * 19349663u;
        hash ^= static_cast<std::size_t>(cell.z) * 83492791u;
        return hash;
    }
};

// Points hashed into cubes of a fixed size, for finding points within that distance of each
// other without scanning a whole row of wires that share one coordinate.
class PointGrid {
public:
    PointGrid(const std::vector<model::Point3D>& points, double cellSize)
        : cellSize_(cellSize > 0.0 ? cellSize : 1.0)
    {
        for (std::size_t index = 0; index < points.size(); ++index)
            cells_[cellOf(points[index])].push_back(index);
    }

    // Calls visit(index) for every point in the cubes around point, a superset of the points
    // within one cell size of it.
    template <typename Visit>
    void forEachNear(const model::Point3D& point, Visit visit) const
    {
        const auto center = cellOf(point);
        for (auto x = center.x - 1; x <= center.x + 1; ++x)
            for (auto y = center.y - 1; y <= center.y + 1; ++y)
                for (auto z = center.z - 1; z <= center.z + 1; ++z) {
                    const auto found = cells_.find({x, y, z});
                    if (found == cells_.end()) continue;
                    for (const auto index : found->second) visit(index);
                }
    }

private:
    [[nodiscard]] auto cellOf(const model::Point3D& point) const -> Cell
    {
        const auto index = [this](double value) {
            return static_cast<std::int64_t>(std::floor(value / cellSize_));
        };
        return {index(point.x), index(point.y), index(point.z)};
    }

    double cellSize_;
    std::unordered_map<Cell, std::vector<std::size_t>, CellHash> cells_;
};

// Segment-level view of the drawn wires. NEC connects segments wherever their ends coincide,
// including at interior segment boundaries of another wire, so connection checks work on
// segments rather than wire ends.
struct GraphSegment {
    const model::Wire* wire{};
    std::size_t wireIndex{};
    int segment{};
    model::Point3D start;
    model::Point3D end;
    double radius{};
    std::size_t startNode{};
    std::size_t endNode{};
};

struct GraphNode {
    model::Point3D point;
    int segmentCount{};
    double minimumRadius{std::numeric_limits<double>::infinity()};
    double maximumRadius{};
    double minimumSegmentLength{std::numeric_limits<double>::infinity()};
};

struct SegmentGraph {
    std::vector<GraphSegment> segments;
    std::vector<GraphNode> nodes;
    // Index of each wire's first segment, or npos for wires without segments or radius.
    std::vector<std::size_t> firstSegment;
    // Node pairs joined by a single segment.
    std::unordered_set<std::uint64_t> links;
    // Rounding allowance at the largest coordinate in the model, for purely geometric tests.
    // Connections use NEC's own tolerance instead.
    double tolerance{};

    static constexpr auto npos = std::numeric_limits<std::size_t>::max();

    static auto linkKey(std::size_t first, std::size_t second) noexcept -> std::uint64_t
    {
        return (static_cast<std::uint64_t>(std::min(first, second)) << 32)
            | static_cast<std::uint64_t>(std::max(first, second));
    }

    [[nodiscard]] auto find(const model::AntennaModel& model, const model::Wire& wire,
        int segment) const -> const GraphSegment*
    {
        const auto wireIndex = static_cast<std::size_t>(&wire - model.wires().data());
        if (wireIndex >= firstSegment.size() || firstSegment[wireIndex] == npos
            || segment < 1 || segment > wire.segments) return nullptr;
        return &segments[firstSegment[wireIndex] + static_cast<std::size_t>(segment - 1)];
    }

    // True when the segments share an end or are one segment apart, so closeness is expected.
    [[nodiscard]] auto adjacent(const GraphSegment& first, const GraphSegment& second) const
        -> bool
    {
        for (const auto a : {first.startNode, first.endNode}) {
            for (const auto b : {second.startNode, second.endNode}) {
                if (a == b || links.contains(linkKey(a, b))) return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto grounded(const GraphNode& node, bool groundsEnds) const noexcept -> bool
    {
        return groundsEnds && necOnGround(node.point.z, node.minimumSegmentLength);
    }
};

auto buildSegmentGraph(const model::AntennaModel& model) -> SegmentGraph
{
    SegmentGraph graph;
    const auto wires = model.wires();
    graph.firstSegment.assign(wires.size(), SegmentGraph::npos);
    struct End {
        model::Point3D point;
        double radius{};
        double segmentLength{};
    };
    std::vector<End> ends;
    auto scale = 1.0;
    auto longestSegment = 0.0;
    for (std::size_t wireIndex = 0; wireIndex < wires.size(); ++wireIndex) {
        const auto& wire = wires[wireIndex];
        if (wire.segments <= 0 || wire.radius <= 0.0) continue;
        graph.firstSegment[wireIndex] = graph.segments.size();
        for (auto segment = 1; segment <= wire.segments; ++segment) {
            const auto [start, end] = model::wireSegmentEndpoints(wire, segment).value();
            const auto startRadius = radiusAt(wire, double(segment - 1) / wire.segments);
            const auto endRadius = radiusAt(wire, double(segment) / wire.segments);
            const auto length = norm(difference(end, start));
            graph.segments.push_back({&wire, wireIndex, segment, start, end,
                std::max(startRadius, endRadius)});
            ends.push_back({start, startRadius, length});
            ends.push_back({end, endRadius, length});
            scale = std::max({scale, std::abs(start.x), std::abs(start.y), std::abs(start.z),
                std::abs(end.x), std::abs(end.y), std::abs(end.z)});
            longestSegment = std::max(longestSegment, length);
        }
    }
    graph.tolerance = scale * 1.0e-9;

    // Group touching ends; necTouching() decides each match. No end reaches farther than NEC's
    // tolerance for the longest segment, so that is the grid's cell size.
    std::vector<model::Point3D> points;
    points.reserve(ends.size());
    for (const auto& end : ends) points.push_back(end.point);
    const PointGrid grid(points, kNecContactFraction * longestSegment);
    std::vector<std::size_t> nodeOf(ends.size(), SegmentGraph::npos);
    for (std::size_t index = 0; index < ends.size(); ++index) {
        const auto& first = ends[index];
        if (nodeOf[index] != SegmentGraph::npos) continue;
        const auto nodeIndex = graph.nodes.size();
        graph.nodes.push_back({first.point});
        auto& node = graph.nodes.back();
        grid.forEachNear(first.point, [&](std::size_t next) {
            const auto& candidate = ends[next];
            if (nodeOf[next] != SegmentGraph::npos
                || !necTouching(first.point, first.segmentLength, candidate.point,
                    candidate.segmentLength))
                return;
            nodeOf[next] = nodeIndex;
            ++node.segmentCount;
            node.minimumRadius = std::min(node.minimumRadius, candidate.radius);
            node.maximumRadius = std::max(node.maximumRadius, candidate.radius);
            node.minimumSegmentLength = std::min(node.minimumSegmentLength, candidate.segmentLength);
        });
    }
    for (std::size_t index = 0; index < graph.segments.size(); ++index) {
        auto& segment = graph.segments[index];
        segment.startNode = nodeOf[2 * index];
        segment.endNode = nodeOf[2 * index + 1];
        graph.links.insert(SegmentGraph::linkKey(segment.startNode, segment.endNode));
    }
    return graph;
}

using WirePairs = std::set<std::pair<std::size_t, std::size_t>>;

// Replays NEC-2's CONECT and TRIO. CONECT gives each segment end one pointer: the first other
// segment end, searching onward from the next segment number, within that end's own tolerance
// (negative when it is the other segment's first end). TRIO follows those pointers around each
// junction and stops NEC-2 if it meets a zero or does not come back within 30 steps. With
// mixed segment lengths, a long segment can reach a short one that does not reach back.
// Returns the wire pairs reported, so the contact check does not report them again.
auto checkNecJunctions(const SegmentGraph& graph, bool groundsEnds, ModelCheckResult& result)
    -> WirePairs
{
    WirePairs reported;
    const auto& segments = graph.segments;
    const auto count = static_cast<std::int64_t>(segments.size());
    if (count < 2) return reported;

    // End 2 * i is the start of segment i, 2 * i + 1 its end.
    const auto endPoint = [&segments](std::size_t end) -> const model::Point3D& {
        return end % 2 == 0 ? segments[end / 2].start : segments[end / 2].end;
    };
    std::vector<double> lengths(segments.size());
    auto longestSegment = 0.0;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        lengths[index] = norm(difference(segments[index].end, segments[index].start));
        longestSegment = std::max(longestSegment, lengths[index]);
    }
    std::vector<model::Point3D> points;
    points.reserve(2 * segments.size());
    for (std::size_t end = 0; end < 2 * segments.size(); ++end) points.push_back(endPoint(end));
    const PointGrid grid(points, kNecContactFraction * longestSegment);

    // Pointers use NEC's 1-based segment numbers; 0 means a free end.
    std::vector<std::int64_t> pointer(points.size());
    for (std::size_t end = 0; end < points.size(); ++end) {
        const auto self = static_cast<std::int64_t>(end / 2);
        const auto& point = endPoint(end);
        const auto tolerance = kNecContactFraction * lengths[end / 2];
        if (groundsEnds && necOnGround(point.z, lengths[end / 2])) {
            pointer[end] = self + 1;
            continue;
        }
        auto best = std::numeric_limits<std::int64_t>::max();
        auto bestEnd = std::size_t{};
        grid.forEachNear(point, [&](std::size_t candidateEnd) {
            const auto other = static_cast<std::int64_t>(candidateEnd / 2);
            if (other == self) return;
            const auto& candidate = points[candidateEnd];
            const auto separation = std::abs(point.x - candidate.x) + std::abs(point.y - candidate.y)
                + std::abs(point.z - candidate.z);
            if (separation > tolerance) return;
            // Search order: next segment number first, and a segment's first end before its last.
            const auto rank = 2 * ((other - self + count) % count)
                + static_cast<std::int64_t>(candidateEnd % 2);
            if (rank < best) {
                best = rank;
                bestEnd = candidateEnd;
            }
        });
        if (best == std::numeric_limits<std::int64_t>::max()) continue;
        // Ends of the same kind (first to first, last to last) give a negative pointer.
        const auto next = static_cast<std::int64_t>(bestEnd / 2) + 1;
        pointer[end] = (end % 2) == (bestEnd % 2) ? -next : next;
    }

    constexpr auto kTrioLimit = 30;
    const auto report = [&](std::size_t fromEnd, std::size_t toEnd, std::string message) {
        const auto& from = segments[fromEnd / 2];
        const auto& to = segments[toEnd / 2];
        const auto wires = std::minmax(from.wireIndex, to.wireIndex);
        if (!reported.insert(wires).second) return;
        addAdequacyDiagnostic(result, DiagnosticSeverity::Error,
            std::max(from.wire->sourceLine, to.wire->sourceLine), std::move(message));
    };
    const auto name = [&segments](std::size_t end) {
        const auto& segment = segments[end / 2];
        return "wire tag " + std::to_string(segment.wire->tag) + " segment "
            + std::to_string(segment.segment);
    };
    for (std::size_t start = 0; start < pointer.size(); ++start) {
        // TRIO's 30-step limit covers both ends of a segment together; per end is enough to
        // tell a broken ring from a closed one.
        if (pointer[start] == 0) continue;
        const auto self = start / 2;
        auto steps = 0;
        auto current = start;
        std::vector<std::size_t> visited;
        while (true) {
            const auto link = pointer[current];
            const auto next = static_cast<std::size_t>(std::abs(link) - 1);
            if (next == self) break;
            if (++steps >= kTrioLimit) {
                // A walk that repeats segments without coming back is a ring broken by a
                // one-sided link; a plain overflow is left to the junction-size warning.
                if (std::ranges::find(visited, next) != visited.end()) {
                    report(start, 2 * next, "Segment ends at about " + location(endPoint(start))
                            + " are joined from some sides only, so NEC-2's junction walk from "
                            + name(start) + " never returns to it. NEC-2 stops with \"TRIO - "
                              "SEGMENT CONNECTION ERROR\" (nec2c runs out of memory instead). "
                              "Make the ends meet exactly, or separate them beyond 0.1% of the "
                              "longest segment length.");
                    for (const auto segment : visited) {
                        reported.insert(std::minmax(segments[self].wireIndex,
                            segments[segment].wireIndex));
                    }
                }
                break;
            }
            visited.push_back(next);
            // A negative pointer joins ends of the same kind (first to first, last to last).
            const auto arrival = 2 * next + (link < 0 ? current % 2 : 1 - current % 2);
            if (pointer[arrival] == 0) {
                const auto& from = endPoint(current);
                const auto& to = endPoint(arrival);
                const auto separation = std::abs(from.x - to.x) + std::abs(from.y - to.y)
                    + std::abs(from.z - to.z);
                report(current, arrival, "The end of " + name(current) + " reaches " + name(arrival)
                        + " at about " + location(to) + ", but not the other way: the ends are "
                        + concise(separation * 1000.0) + " mm apart (|dx| + |dy| + |dz|), within "
                          "NEC's 0.1% of the first segment's length but not the second's. NEC-2 "
                          "stops with \"TRIO - SEGMENT CONNECTION ERROR\". Make the ends meet "
                          "exactly, or move them more than "
                        + concise(kNecContactFraction * lengths[current / 2] * 1000.0)
                        + " mm apart.");
                break;
            }
            current = arrival;
        }
    }
    return reported;
}

// Segments that come within their combined radii without sharing an end. A uniform grid keeps
// large radial fields fast: each padded segment box goes into the cells it covers, and a pair
// is tested only in the cell that holds the low corner of their box overlap.
void checkUnconnectedContacts(const SegmentGraph& graph, WirePairs reportedWires,
    ModelCheckResult& result)
{
    const auto& segments = graph.segments;
    if (segments.size() < 2) return;

    struct Box {
        std::array<double, 3> low;
        std::array<double, 3> high;
    };
    std::vector<Box> boxes;
    boxes.reserve(segments.size());
    auto averageExtent = 0.0;
    for (const auto& segment : segments) {
        const auto pad = segment.radius + graph.tolerance;
        Box box{{std::min(segment.start.x, segment.end.x) - pad,
                    std::min(segment.start.y, segment.end.y) - pad,
                    std::min(segment.start.z, segment.end.z) - pad},
            {std::max(segment.start.x, segment.end.x) + pad,
                std::max(segment.start.y, segment.end.y) + pad,
                std::max(segment.start.z, segment.end.z) + pad}};
        averageExtent += std::max({box.high[0] - box.low[0], box.high[1] - box.low[1],
            box.high[2] - box.low[2]});
        boxes.push_back(box);
    }
    const auto cellSize = averageExtent / static_cast<double>(segments.size());

    const auto cellIndex = [cellSize](double value) {
        return static_cast<std::int64_t>(std::floor(value / cellSize));
    };
    const auto cellOf = [&cellIndex](const std::array<double, 3>& point) {
        return Cell{cellIndex(point[0]), cellIndex(point[1]), cellIndex(point[2])};
    };

    // A segment far longer than the cell size would fill too many cells; test it directly.
    constexpr auto kMaximumCellsPerSegment = std::int64_t{512};
    std::unordered_map<Cell, std::vector<std::size_t>, CellHash> grid;
    std::vector<std::size_t> oversized;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto low = cellOf(boxes[index].low);
        const auto high = cellOf(boxes[index].high);
        const auto cellCount = (high.x - low.x + 1) * (high.y - low.y + 1) * (high.z - low.z + 1);
        if (!(cellSize > 0.0) || cellCount > kMaximumCellsPerSegment) {
            oversized.push_back(index);
            continue;
        }
        for (auto x = low.x; x <= high.x; ++x)
            for (auto y = low.y; y <= high.y; ++y)
                for (auto z = low.z; z <= high.z; ++z) grid[{x, y, z}].push_back(index);
    }

    const auto test = [&](std::size_t firstIndex, std::size_t secondIndex) {
        const auto& firstBox = boxes[firstIndex];
        const auto& secondBox = boxes[secondIndex];
        for (auto axis = 0; axis < 3; ++axis) {
            if (firstBox.high[axis] < secondBox.low[axis]
                || secondBox.high[axis] < firstBox.low[axis]) return;
        }
        const auto& first = segments[firstIndex];
        const auto& second = segments[secondIndex];
        const auto wires = std::minmax(first.wireIndex, second.wireIndex);
        if (reportedWires.contains(wires) || graph.adjacent(first, second)) return;
        const auto approach = closestApproach(first.start, first.end, second.start, second.end);
        if (approach.distance > first.radius + second.radius + graph.tolerance) return;
        reportedWires.insert(wires);

        const auto lineNumber = std::max(first.wire->sourceLine, second.wire->sourceLine);
        // A free wire end resting on the other segment's interior is a T that NEC leaves open.
        const auto freeEndOn = [&graph](const GraphSegment& segment, double position) {
            const auto node = position <= 0.0 ? segment.startNode
                : position >= 1.0               ? segment.endNode
                                                : SegmentGraph::npos;
            return node != SegmentGraph::npos && graph.nodes[node].segmentCount == 1;
        };
        const auto interior = [](double position) { return position > 0.0 && position < 1.0; };
        const auto* ending = freeEndOn(first, approach.first) && interior(approach.second) ? &first
            : freeEndOn(second, approach.second) && interior(approach.first)             ? &second
                                                                                         : nullptr;
        if (ending != nullptr && first.wireIndex != second.wireIndex) {
            const auto& other = ending == &first ? second : first;
            addAdequacyWarning(result, lineNumber,
                "Wire tag " + std::to_string(ending->wire->tag) + " ends on wire tag "
                    + std::to_string(other.wire->tag) + " at about "
                    + location(approach.midpoint) + ", but not at one of its segment ends, so "
                    "NEC does not connect them. Move the end to a segment junction on wire tag "
                    + std::to_string(other.wire->tag) + " or split that wire there.");
        } else if (first.wireIndex == second.wireIndex) {
            addAdequacyWarning(result, lineNumber,
                "Wire tag " + std::to_string(first.wire->tag) + " crosses itself at about "
                    + location(approach.midpoint)
                    + "; NEC joins segments only at shared segment endpoints. Reshape the wire "
                      "so its segments stay apart.");
        } else {
            const auto verb = approach.distance <= graph.tolerance ? " cross" : " touch";
            addAdequacyWarning(result, lineNumber,
                "Wire tags " + std::to_string(first.wire->tag) + " and "
                    + std::to_string(second.wire->tag) + verb + " at about "
                    + location(approach.midpoint)
                    + " but are not connected; NEC joins wires only at shared segment "
                      "endpoints. Split one wire at the crossing or move it.");
        }
    };

    for (const auto& [cell, members] : grid) {
        for (std::size_t i = 0; i < members.size(); ++i) {
            for (std::size_t j = i + 1; j < members.size(); ++j) {
                const auto& firstBox = boxes[members[i]];
                const auto& secondBox = boxes[members[j]];
                const auto owner = cellOf({std::max(firstBox.low[0], secondBox.low[0]),
                    std::max(firstBox.low[1], secondBox.low[1]),
                    std::max(firstBox.low[2], secondBox.low[2])});
                if (owner == cell) test(members[i], members[j]);
            }
        }
    }
    for (std::size_t i = 0; i < oversized.size(); ++i) {
        for (std::size_t other = 0; other < segments.size(); ++other) {
            const auto alsoOversized = std::ranges::find(oversized, other) != oversized.end();
            if (other == oversized[i] || (alsoOversized && other < oversized[i])) continue;
            test(oversized[i], other);
        }
    }
}

struct GroundState {
    model::GroundType type{model::GroundType::FreeSpace};
    std::size_t sourceLine{};
};

// The grounds NEC-2 solves with: the GN in effect at each XQ, RP, NE, or NH request, or the
// last GN when there is none. A later GN replaces an earlier one.
auto executedGrounds(const NecDocument& document) -> std::vector<GroundState>
{
    std::vector<GroundState> executed;
    GroundState current;
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Ground) {
            int type{};
            if (!card.fields.empty() && parseNumber(card.fields[0], type)
                && type >= static_cast<int>(model::GroundType::FreeSpace)
                && type <= static_cast<int>(model::GroundType::SommerfeldNorton))
                current = {static_cast<model::GroundType>(type), card.lineNumber};
        } else if (card.mnemonic == "XQ" || card.mnemonic == "RP" || card.mnemonic == "NE"
            || card.mnemonic == "NH") {
            if (executed.empty() || executed.back().sourceLine != current.sourceLine)
                executed.push_back(current);
        }
    }
    if (executed.empty()) executed.push_back(current);
    return executed;
}

void checkGroundFit(const model::ModelSetup& setup, const std::vector<GroundState>& grounds,
    const SegmentGraph& graph, double wavelengthMeters, CompatibilityProfile profile,
    ModelCheckResult& result)
{
    // In NEC-2, GN alone defines the ground, and GE alone decides whether wire ends at z = 0
    // attach to it: CONECT ties them to the ground image only for GE 1. With GE 0 or -1 such
    // an end stays open and its current is forced to zero. A deck that solves over ground in
    // any execution request is checked against that ground.
    const auto groundPresent = std::ranges::any_of(grounds,
        [](const GroundState& ground) { return ground.type != model::GroundType::FreeSpace; });
    const auto groundFlag = setup.ground ? setup.ground->geometryGroundFlag : 0;
    const auto groundsEnds = groundFlag > 0;
    const auto tolerance = graph.tolerance;
    const auto wires = result.model.wires();
    std::vector<const model::Wire*> openAtGround;

    for (std::size_t wireIndex = 0; wireIndex < wires.size(); ++wireIndex) {
        const auto& wire = wires[wireIndex];
        if (wire.segments <= 0) continue;
        auto lowest = std::numeric_limits<double>::infinity();
        auto highest = -lowest;
        for (std::size_t point = 0; point < model::wirePathPointCount(wire); ++point) {
            lowest = std::min(lowest, model::wirePathPoint(wire, point).z);
            highest = std::max(highest, model::wirePathPoint(wire, point).z);
        }
        const auto tag = std::to_string(wire.tag);
        const auto first = graph.firstSegment[wireIndex];
        if (first == SegmentGraph::npos) continue;
        // Classify segment ends the way CONECT does, each against its own segment length.
        auto below = false;
        auto above = false;
        auto inGroundPlane = false;
        for (auto index = first; index < first + static_cast<std::size_t>(wire.segments); ++index) {
            const auto& segment = graph.segments[index];
            const auto length = norm(difference(segment.end, segment.start));
            below = below || necBelowGround(segment.start.z, length)
                || necBelowGround(segment.end.z, length);
            above = above || (segment.start.z > 0.0 && !necOnGround(segment.start.z, length))
                || (segment.end.z > 0.0 && !necOnGround(segment.end.z, length));
            inGroundPlane = inGroundPlane
                || (necOnGround(segment.start.z, length) && necOnGround(segment.end.z, length));
        }
        // GE 1 also makes NEC-2 stop on these, even without GN.
        if ((groundPresent || groundsEnds) && usesNec2Kernel(profile)) {
            if (groundsEnds && inGroundPlane) {
                addAdequacyDiagnostic(result, DiagnosticSeverity::Error, wire.sourceLine,
                    "Wire tag " + tag + " has segments lying in the ground plane at z = 0; "
                        "with GE 1, NEC-2 stops on this. Raise the wire above ground.");
            }
            if (below && above) {
                addAdequacyDiagnostic(result, DiagnosticSeverity::Error, wire.sourceLine,
                    "Wire tag " + tag + " runs from z = " + concise(lowest) + " m to z = "
                        + concise(highest) + " m through the ground at z = 0; NEC-2 cannot "
                        "model a wire that passes into the ground. End it at z = 0 or keep it "
                        "above ground.");
            } else if (below) {
                addAdequacyDiagnostic(result, DiagnosticSeverity::Error, wire.sourceLine,
                    "Wire tag " + tag + " is below ground (lowest point z = " + concise(lowest)
                        + " m); NEC-2 cannot model buried wires. Raise it to z = 0 or above, "
                          "or use a NEC-4 class solver.");
            }
        }
        // Wires rising from z = 0 with an unconnected end there; wires lying flat are skipped.
        if (below || above) {
            const auto& lastSegment = graph.segments[first + static_cast<std::size_t>(wire.segments - 1)];
            const auto freeAtGround = [&graph](std::size_t nodeIndex) {
                const auto& node = graph.nodes[nodeIndex];
                return node.segmentCount == 1 && necOnGround(node.point.z, node.minimumSegmentLength);
            };
            if (freeAtGround(graph.segments[first].startNode) || freeAtGround(lastSegment.endNode))
                openAtGround.push_back(&wire);
        }
    }
    if (!openAtGround.empty() && !(groundPresent && groundsEnds)) {
        // One finding on GE, where the fix goes, rather than one per wire of an array.
        const auto geometryEnd = setup.ground ? setup.ground->geometryEndSourceLine : 0;
        const auto single = openAtGround.size() == 1;
        auto tags = std::string(single ? "Wire tag " : "Wire tags ");
        for (std::size_t index = 0; index < openAtGround.size() && index < 5; ++index)
            tags += (index == 0 ? "" : ", ") + std::to_string(openAtGround[index]->tag);
        if (openAtGround.size() > 5)
            tags += " and " + std::to_string(openAtGround.size() - 5) + " more";
        tags += single ? " ends at z = 0" : " end at z = 0";
        const auto lineNumber = geometryEnd != 0 ? geometryEnd : openAtGround.front()->sourceLine;
        if (groundsEnds) {
            addAdequacyWarning(result, lineNumber,
                tags + ", and GE 1 ties wire ends there to ground, but no GN card defines a "
                       "ground, so NEC solves free space without the ground image. Add a GN "
                       "card, or use GE 0 for a free-space model.");
        } else if (groundPresent) {
            addAdequacyWarning(result, lineNumber,
                tags + " over the GN ground, but GE " + std::to_string(groundFlag)
                    + " does not connect wire ends to ground, so NEC forces the current to zero "
                      "there. Use GE 1 to connect them.");
        } else {
            addAdequacyWarning(result, lineNumber,
                tags + ", but the model has no ground, so NEC treats each such end as open in "
                       "free space. To connect to ground, add a GN card and use GE 1.");
        }
    }

    const auto reflection = std::ranges::find(grounds,
        model::GroundType::ReflectionApproximation, &GroundState::type);
    if (reflection == grounds.end()) return;
    const auto heightLimit = kReflectionGroundMinimumHeightWavelengths * wavelengthMeters;
    const auto horizontalLimit = std::sin(kNearHorizontalDegrees * std::numbers::pi / 180.0);
    std::set<std::size_t> lowWires;
    const GraphSegment* lowestSegment{};
    auto lowestHeight = std::numeric_limits<double>::infinity();
    for (const auto& segment : graph.segments) {
        const auto direction = difference(segment.end, segment.start);
        const auto height = (segment.start.z + segment.end.z) / 2.0;
        if (height < -tolerance || height >= heightLimit
            || std::abs(direction.z) > horizontalLimit * norm(direction)) continue;
        lowWires.insert(segment.wireIndex);
        if (height < lowestHeight) {
            lowestHeight = height;
            lowestSegment = &segment;
        }
    }
    if (lowestSegment == nullptr) return;
    addAdequacyWarning(result, reflection->sourceLine,
        "GN 0 uses the reflection-coefficient ground approximation, but "
            + std::to_string(lowWires.size())
            + (lowWires.size() == 1 ? " wire has" : " wires have")
            + " near-horizontal segments below " + concise(kReflectionGroundMinimumHeightWavelengths)
            + " wavelength (lowest: wire tag " + std::to_string(lowestSegment->wire->tag) + " at "
            + concise(lowestHeight) + " m, " + concise(lowestHeight / wavelengthMeters)
            + " wavelength). Use GN 2 (Sommerfeld-Norton) for wires this close to ground.");
}

// Closely spaced parallel wires whose segment junctions do not line up.
void checkParallelAlignment(const SegmentGraph& graph, double wavelengthMeters,
    ModelCheckResult& result)
{
    struct Line {
        const model::Wire* wire{};
        model::Point3D direction;
        double radius{};
        double minimumSegment{};
        std::vector<model::Point3D> junctions;
    };
    std::vector<Line> lines;
    const auto wires = result.model.wires();
    for (std::size_t wireIndex = 0; wireIndex < wires.size(); ++wireIndex) {
        const auto& wire = wires[wireIndex];
        const auto first = graph.firstSegment[wireIndex];
        if (first == SegmentGraph::npos || wire.geometryKind != model::WireGeometryKind::Straight)
            continue;
        const auto direction = difference(wire.end, wire.start);
        const auto length = norm(direction);
        if (length <= 0.0) continue;
        Line line{&wire, along({}, direction, 1.0 / length), model::wireMaximumRadius(wire),
            model::wireMinimumSegmentLength(wire), {wire.start}};
        for (auto segment = 0; segment < wire.segments; ++segment)
            line.junctions.push_back(graph.segments[first + static_cast<std::size_t>(segment)].end);
        lines.push_back(std::move(line));
    }

    const auto parallelCosine = std::cos(kParallelAngleDegrees * std::numbers::pi / 180.0);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        for (std::size_t j = i + 1; j < lines.size(); ++j) {
            const auto& first = lines[i];
            const auto& second = lines[j];
            const auto cosine = dot(first.direction, second.direction);
            if (std::abs(cosine) < parallelCosine) continue;
            // Measure both wires along their mean direction so neither is foreshortened.
            const auto mean = along(first.direction, second.direction, cosine < 0.0 ? -1.0 : 1.0);
            const auto axis = along({}, mean, 1.0 / norm(mean));
            const auto origin = first.junctions.front();
            const auto project = [&axis, &origin](const std::vector<model::Point3D>& points) {
                std::vector<double> positions;
                positions.reserve(points.size());
                for (const auto& point : points) positions.push_back(dot(difference(point, origin), axis));
                std::ranges::sort(positions);
                return positions;
            };
            const auto firstPositions = project(first.junctions);
            const auto secondPositions = project(second.junctions);
            const auto low = std::max(firstPositions.front(), secondPositions.front());
            const auto high = std::min(firstPositions.back(), secondPositions.back());
            const auto segmentLength = std::min(first.minimumSegment, second.minimumSegment);
            // Require at least one segment side by side; this also skips end-to-end wires.
            if (high - low < segmentLength) continue;

            const auto middle = along(origin, axis, (low + high) / 2.0);
            const auto fromSecond = difference(middle, second.junctions.front());
            const auto separation = norm(difference(fromSecond,
                along({}, axis, dot(fromSecond, axis))));
            // The NEC-2 guide asks for aligned segments only when wires are "very close"; an
            // offset of at most half a segment matters only within about a segment length.
            const auto closeLimit = std::min(segmentLength,
                std::max(kCloseSpacingRadii * std::max(first.radius, second.radius),
                    kCloseSpacingWavelengths * wavelengthMeters));
            // Contact is reported by the crossing check.
            if (separation <= first.radius + second.radius || separation >= closeLimit) continue;

            const auto slack = segmentLength * 1.0e-6;
            const auto largestOffset = [low, high, slack](const std::vector<double>& positions,
                                           const std::vector<double>& reference) {
                auto largest = 0.0;
                for (const auto position : positions) {
                    if (position < low - slack || position > high + slack) continue;
                    const auto next = std::ranges::lower_bound(reference, position);
                    auto nearest = std::numeric_limits<double>::infinity();
                    if (next != reference.end()) nearest = *next - position;
                    if (next != reference.begin()) nearest = std::min(nearest, position - *(next - 1));
                    largest = std::max(largest, nearest);
                }
                return largest;
            };
            const auto offset = std::max(largestOffset(firstPositions, secondPositions),
                largestOffset(secondPositions, firstPositions));
            if (offset <= kSegmentAlignmentFraction * segmentLength) continue;
            addAdequacyWarning(result, std::max(first.wire->sourceLine, second.wire->sourceLine),
                "Wire tags " + std::to_string(first.wire->tag) + " and "
                    + std::to_string(second.wire->tag) + " run parallel " + concise(separation)
                    + " m apart, but their segment junctions are offset by up to "
                    + concise(offset) + " m (" + concise(100.0 * offset / segmentLength)
                    + "% of a segment); NEC-2 is most accurate when closely spaced parallel "
                      "segments line up. Segment the overlapping lengths to match.");
        }
    }
}

enum class InsertionRisk {
    None,
    Junction,
    RadiusStep,
    FreeEnd
};

struct InsertionFinding {
    InsertionRisk risk{InsertionRisk::None};
    const GraphNode* node{};
};

// Why a segment is a poor place for a source, load, or transmission-line end, if it is.
auto insertionRisk(const SegmentGraph& graph, const model::AntennaModel& model,
    const model::SegmentReference& reference, bool groundsEnds) -> InsertionFinding
{
    if (reference.wire == nullptr) return {};
    const auto* segment = graph.find(model, *reference.wire, reference.segment);
    if (segment == nullptr) return {};
    InsertionFinding freeEnd;
    for (const auto nodeIndex : {segment->startNode, segment->endNode}) {
        const auto& node = graph.nodes[nodeIndex];
        if (node.segmentCount > 2) return {InsertionRisk::Junction, &node};
        if (node.maximumRadius > node.minimumRadius * kRadiusStepRatio)
            return {InsertionRisk::RadiusStep, &node};
        // A one-segment wire is the usual stub for a TL or NT terminal; its ends are expected.
        if (node.segmentCount == 1 && reference.wire->segments > 1
            && !graph.grounded(node, groundsEnds))
            freeEnd = {InsertionRisk::FreeEnd, &node};
    }
    return freeEnd;
}

void reportInsertionRisk(ModelCheckResult& result, const SegmentGraph& graph,
    bool groundsEnds, std::string_view card, int wireTag, int segment, std::size_t lineNumber)
{
    const auto reference = model::referencedSegment(result.model, wireTag, segment);
    if (!reference) return;
    const auto finding = insertionRisk(graph, result.model, *reference, groundsEnds);
    const auto isLine = card == "TL";
    const auto subject = std::string(isLine ? "TL end" : card) + " on wire tag "
        + std::to_string(wireTag) + " segment " + std::to_string(segment);
    const auto objects = card == "EX" ? "sources" : card == "LD" ? "loads" : "line connections";
    switch (finding.risk) {
    case InsertionRisk::Junction:
        // NEC-2 allows this (only EX 5 needs a two-segment junction), so it is a note.
        addAdequacyDiagnostic(result, DiagnosticSeverity::Information, lineNumber,
            subject + " touches a junction of " + std::to_string(finding.node->segmentCount)
                + " segments, so the result includes the junction; move it one segment away if "
                  "you want the wire on its own.");
        break;
    case InsertionRisk::RadiusStep:
        addAdequacyWarning(result, lineNumber,
            subject + " adjoins a radius step from " + concise(finding.node->minimumRadius)
                + " m to " + concise(finding.node->maximumRadius) + " m; NEC-2 is less accurate "
                + "for " + objects + " there. Move it at least one segment from the step.");
        break;
    case InsertionRisk::FreeEnd:
        if (isLine) break;
        addAdequacyDiagnostic(result, DiagnosticSeverity::Information, lineNumber,
            subject + " is on the end segment of a free wire end, where NEC forces the current "
                      "toward zero; move it inward unless that is intended.");
        break;
    case InsertionRisk::None:
        break;
    }
}

void checkStaticAdequacy(const NecDocument& document, const model::ModelSetup& setup,
    CompatibilityProfile profile, ModelCheckResult& result)
{
    if (!setup.frequency || result.model.empty()) return;
    const auto highestFrequencyMHz = std::max(setup.frequency->startMHz,
        model::frequencyEndMHz(*setup.frequency));
    if (!std::isfinite(highestFrequencyMHz) || highestFrequencyMHz <= 0.0) return;
    const auto wavelengthMeters = 299.792458 / highestFrequencyMHz;
    // Ground height is most demanding at the lowest frequency of a sweep.
    const auto lowestFrequencyMHz = std::min(setup.frequency->startMHz,
        model::frequencyEndMHz(*setup.frequency));
    const auto longestWavelengthMeters = lowestFrequencyMHz > 0.0
        ? 299.792458 / lowestFrequencyMHz : wavelengthMeters;
    const auto graph = buildSegmentGraph(result.model);
    // Only GE 1 ties wire ends at z = 0 to ground; otherwise they are free ends.
    const auto groundsEnds = setup.ground
        && setup.ground->geometryGroundFlag > 0;

    struct Endpoint {
        model::Point3D point;
        const model::Wire* wire{};
        double segmentLength{};
    };
    std::vector<Endpoint> endpoints;
    endpoints.reserve(result.model.wireCount() * 2);

    for (const auto& wire : result.model.wires()) {
        if (wire.segments <= 0 || wire.radius <= 0.0) continue;
        const auto segmentLength = model::wireMinimumSegmentLength(wire);
        const auto wavelengthRatio = segmentLength / wavelengthMeters;
        const auto diameterRatio = segmentLength / (2.0 * model::wireMaximumRadius(wire));
        if (wavelengthRatio > 0.1) {
            addAdequacyWarning(result, wire.sourceLine,
                "Wire tag " + std::to_string(wire.tag) + " segment length is "
                    + concise(wavelengthRatio) + " wavelength at "
                    + concise(highestFrequencyMHz)
                    + " MHz; keep segments below 0.1 wavelength and prefer about 0.05");
        } else if (wavelengthRatio < 0.001) {
            addAdequacyWarning(result, wire.sourceLine,
                "Wire tag " + std::to_string(wire.tag) + " segment length is "
                    + concise(wavelengthRatio)
                    + " wavelength; very short segments can reduce NEC numerical reliability");
        }
        if (diameterRatio <= 4.0) {
            addAdequacyWarning(result, wire.sourceLine,
                "Wire tag " + std::to_string(wire.tag) + " segment-length/diameter ratio is "
                    + concise(diameterRatio)
                    + "; values above 4 are the conservative NEC-2 thin-wire guideline");
        }
        endpoints.push_back({wire.start, &wire, segmentLength});
        endpoints.push_back({wire.end, &wire, segmentLength});
    }

    std::vector<bool> grouped(endpoints.size());
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        if (grouped[index]) continue;
        std::vector<const Endpoint*> junction;
        for (std::size_t candidate = index; candidate < endpoints.size(); ++candidate) {
            if (necTouching(endpoints[index].point, endpoints[index].segmentLength,
                    endpoints[candidate].point, endpoints[candidate].segmentLength)) {
                grouped[candidate] = true;
                junction.push_back(&endpoints[candidate]);
            }
        }
        if (junction.size() < 2) continue;
        const auto [shortest, longest] = std::ranges::minmax_element(junction,
            {}, [](const Endpoint* endpoint) { return endpoint->segmentLength; });
        if ((*shortest)->segmentLength > 0.0
            && (*longest)->segmentLength / (*shortest)->segmentLength > 2.0) {
            addAdequacyWarning(result, (*longest)->wire->sourceLine,
                "Connected wires " + std::to_string((*shortest)->wire->tag) + " and "
                    + std::to_string((*longest)->wire->tag)
                    + " have adjoining segment lengths differing by more than 2:1");
        }
        if (junction.size() > 30) {
            addAdequacyWarning(result, junction.front()->wire->sourceLine,
                "Junction contains " + std::to_string(junction.size())
                    + " wires; NEC-2 recommends no more than 30 wires at one junction");
        }
    }

    for (const auto& excitation : setup.excitations) {
        if (excitation.type != 0) continue;
        const auto reference = model::referencedSegment(result.model, excitation.wireTag,
            excitation.segment);
        if (!reference || reference->wire == nullptr) continue;
        reportInsertionRisk(result, graph, groundsEnds, "EX", excitation.wireTag,
            excitation.segment, excitation.sourceLine);
        const auto* wire = reference->wire;
        if (wire->segments % 2 != 0) continue;
        if (reference->segment == wire->segments / 2
            || reference->segment == wire->segments / 2 + 1) {
            addAdequacyWarning(result, excitation.sourceLine,
                "Center-fed wire tag " + std::to_string(wire->tag)
                    + " has an even segment count; no segment lies exactly at its center");
        }
    }
    for (const auto& load : setup.loads) {
        // Only single-segment lumped loads sit at one place; LD 5 and ranges are distributed.
        if (load.type == 5 || load.firstSegment < 1 || load.lastSegment != load.firstSegment)
            continue;
        reportInsertionRisk(result, graph, groundsEnds, "LD", load.wireTag, load.firstSegment,
            load.sourceLine);
    }
    for (const auto& line : setup.transmissionLines) {
        reportInsertionRisk(result, graph, groundsEnds, "TL", line.wireTag1, line.segment1,
            line.sourceLine);
        reportInsertionRisk(result, graph, groundsEnds, "TL", line.wireTag2, line.segment2,
            line.sourceLine);
        // A zero length tells NEC to use the straight-line distance.
        if (line.lengthMeters <= 0.0) continue;
        const auto firstCenter = model::wireSegmentPosition(result.model, line.wireTag1,
            line.segment1);
        const auto secondCenter = model::wireSegmentPosition(result.model, line.wireTag2,
            line.segment2);
        if (!firstCenter || !secondCenter) continue;
        const auto gap = norm(difference(*firstCenter, *secondCenter));
        if (line.lengthMeters >= gap * (1.0 - kTransmissionLineLengthTolerance)) continue;
        addAdequacyWarning(result, line.sourceLine,
            "TL from wire tag " + std::to_string(line.wireTag1) + " segment "
                + std::to_string(line.segment1) + " to wire tag " + std::to_string(line.wireTag2)
                + " segment " + std::to_string(line.segment2) + " is " + concise(line.lengthMeters)
                + " m long, but those segments are " + concise(gap)
                + " m apart; a real line cannot be shorter than the gap it spans. Check the "
                  "length, which NEC reads in meters even when GS scales the geometry.");
    }

    auto reportedWires = WirePairs{};
    if (usesNec2Kernel(profile)) reportedWires = checkNecJunctions(graph, groundsEnds, result);
    checkUnconnectedContacts(graph, std::move(reportedWires), result);
    checkGroundFit(setup, executedGrounds(document), graph, longestWavelengthMeters, profile,
        result);
    if (usesNec2Kernel(profile)) checkParallelAlignment(graph, wavelengthMeters, result);
}

void checkExcitation(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 5) {
        addError(result, card, "EX requires four integer fields and at least one numeric field");
        return;
    }

    const bool integerFieldsValid = std::ranges::all_of(card.fields.begin(), card.fields.begin() + 4,
        [](const auto& field) { return isNumber<int>(field); });
    const bool numericFieldsValid = std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
        [](const auto& field) { return isNumber<double>(field); });
    if (!integerFieldsValid || !numericFieldsValid) {
        addError(result, card, "EX contains a value with the wrong numeric type");
        return;
    }

    int excitationType{};
    int wireTag{};
    int segment{};
    parseNumber(card.fields[0], excitationType);
    parseNumber(card.fields[1], wireTag);
    parseNumber(card.fields[2], segment);
    if (excitationType == 0) {
        // Tag 0 selects an absolute segment number; other tags count through every wire
        // that carries them.
        const auto count = model::referencedSegmentCount(result.model, wireTag);
        if (wireTag != 0 && count == 0) {
            addError(result, card, "EX references a wire tag that does not exist");
        } else if (segment < 1 || segment > count) {
            addError(result, card, wireTag == 0
                ? "EX absolute segment number is outside the structure"
                : "EX segment is outside the referenced wire");
        }
    }
}

void checkFrequency(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 5) {
        addError(result, card, "FR requires four integer fields and a start frequency");
        return;
    }

    int frequencyCount{};
    int steppingMode{};
    double startFrequency{};
    double frequencyStep{};
    const bool integerFieldsValid = std::ranges::all_of(card.fields.begin(), card.fields.begin() + 4,
        [](const auto& field) { return isNumber<int>(field); });
    const bool numericFieldsValid = std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
        [](const auto& field) { return isNumber<double>(field); });
    if (!integerFieldsValid || !numericFieldsValid) {
        addError(result, card, "FR contains a value with the wrong numeric type");
        return;
    }

    parseNumber(card.fields[0], steppingMode);
    parseNumber(card.fields[1], frequencyCount);
    parseNumber(card.fields[4], startFrequency);
    if (card.fields.size() >= 6)
        parseNumber(card.fields[5], frequencyStep);
    if (steppingMode != 0 && steppingMode != 1) {
        addError(result, card, "FR stepping mode must be 0 (linear) or 1 (multiplicative)");
    }
    if (frequencyCount < 0) {
        addError(result, card, "FR frequency count cannot be negative");
    }
    if (startFrequency <= 0.0) {
        addError(result, card, "FR start frequency must be positive");
    }
    if (frequencyCount > 1 && card.fields.size() < 6) {
        addError(result, card, "FR increment is required when multiple frequencies are requested");
    } else if (frequencyCount > 1 && frequencyStep <= 0.0) {
        addError(result, card, "FR step must be positive when multiple frequencies are requested");
    }
}

void checkGround(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.empty() || !isNumber<int>(card.fields[0])) {
        addError(result, card, "GN requires a ground-type integer");
        return;
    }
    int groundType{};
    parseNumber(card.fields[0], groundType);
    if (groundType < -1 || groundType > 2) {
        addError(result, card, "GN ground type must be -1, 0, 1, or 2");
        return;
    }
    if (groundType == 0 || groundType == 2) {
        if (card.fields.size() < 6) {
            addError(result, card, "Finite-ground GN requires four integer and two numeric fields");
            return;
        }
        double relativePermittivity{};
        double conductivity{};
        if (!isNumber<double>(card.fields[4]) || !isNumber<double>(card.fields[5])) {
            addError(result, card, "GN dielectric constant and conductivity must be numeric");
            return;
        }
        parseNumber(card.fields[4], relativePermittivity);
        parseNumber(card.fields[5], conductivity);
        if (relativePermittivity <= 0.0 || conductivity < 0.0) {
            addError(result, card, "GN dielectric constant must be positive and conductivity cannot be negative");
        }
    }
}

void checkRadiationPattern(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 8) {
        addError(result, card,
            "RP requires four integer and four numeric stepping fields; distance and normalization are optional");
        return;
    }
    const bool integerFieldsValid = std::ranges::all_of(card.fields.begin(), card.fields.begin() + 4,
        [](const auto& field) { return isNumber<int>(field); });
    const bool numericFieldsValid = std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
        [](const auto& field) { return isNumber<double>(field); });
    if (!integerFieldsValid || !numericFieldsValid) {
        addError(result, card, "RP contains a value with the wrong numeric type");
        return;
    }
    int thetaCount{};
    int phiCount{};
    double thetaStep{};
    double phiStep{};
    parseNumber(card.fields[1], thetaCount);
    parseNumber(card.fields[2], phiCount);
    parseNumber(card.fields[6], thetaStep);
    parseNumber(card.fields[7], phiStep);
    if (thetaCount <= 0 || phiCount <= 0) {
        addError(result, card, "RP theta and phi counts must be positive");
    }
    if ((thetaCount > 1 && thetaStep <= 0.0) || (phiCount > 1 && phiStep <= 0.0)) {
        addError(result, card, "RP angular steps must be positive when their count exceeds one");
    }
}

void checkExecute(const NecCard& card, ModelCheckResult& result)
{
    if (!card.fields.empty() && !isNumber<int>(card.fields[0])) {
        addError(result, card, "XQ option must be an integer");
    }
}

void checkReferenceImpedance(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.empty() || !isNumber<double>(card.fields[0])) {
        addError(result, card, "Z0/ZO reference impedance requires a numeric value in ohms");
        return;
    }
    double impedance{};
    parseNumber(card.fields[0], impedance);
    if (impedance <= 0.0)
        addError(result, card, "Z0/ZO reference impedance must be positive");
}

void checkLoad(const NecCard& card, CompatibilityProfile profile, ModelCheckResult& result)
{
    if (card.fields.size() < 5) { addError(result, card, "LD requires four integer fields and at least one numeric field"); return; }
    int type{}, tag{}, first{}, last{};
    if (!isNumber<int>(card.fields[0]) || !isNumber<int>(card.fields[1])
        || !isNumber<int>(card.fields[2]) || !isNumber<int>(card.fields[3])) {
        addError(result, card, "LD integer fields are invalid"); return;
    }
    parseNumber(card.fields[0], type);
    parseNumber(card.fields[1], tag);
    parseNumber(card.fields[2], first);
    parseNumber(card.fields[3], last);
    if (type < 0 || type > 6) {
        addError(result, card, "LD type must be 0 through 5 for NEC-2, or type 6 for a supported extended backend");
    } else if (type == 6) {
        if (profile == CompatibilityProfile::OriginalNec2) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, card.lineNumber,
                "LD type 6 is a 4nec2 LC-trap extension. Original NEC-2 and NEC2C support "
                "LD types 0 through 5 and cannot process this card as written.",
                "Compatibility"});
        } else if (profile == CompatibilityProfile::FourNec2) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, card.lineNumber,
                "LD type 6 is recognized as a 4nec2 LC-trap extension, but NEC Workbench does "
                "not yet convert it for direct NEC2dXS execution. Select OpenNEC or replace "
                "the trap with standard NEC-2 loading.",
                "Compatibility"});
        }
    }
    // LD counts segments through every wire with its tag, or absolute segments for tag 0.
    const auto count = model::referencedSegmentCount(result.model, tag);
    if (tag != 0 && count == 0) addError(result, card, "LD references a wire tag that does not exist");
    else if (first < 0 || last < 0 || first > count || last > count
        || (first != 0 && last != 0 && first > last)) addError(result, card, "LD segment range is invalid");
    if (!std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
            [](const auto& field) { return isNumber<double>(field); }))
        addError(result, card, "LD values must be numeric");
}

void checkTransmissionLine(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 5) { addError(result, card, "TL requires four integer fields and an impedance"); return; }
    int tag1{}, segment1{}, tag2{}, segment2{};
    int* integers[]{&tag1, &segment1, &tag2, &segment2};
    for (auto index = 0; index < 4; ++index) {
        if (!isNumber<int>(card.fields[index])) { addError(result, card, "TL integer fields are invalid"); return; }
        parseNumber(card.fields[index], *integers[index]);
    }
    const auto validEnd = [&result](int tag, int segment) {
        return model::referencedSegment(result.model, tag, segment).has_value();
    };
    if (!validEnd(tag1, segment1) || !validEnd(tag2, segment2))
        addError(result, card, "TL endpoint references an invalid wire or segment");
    if (!std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
            [](const auto& field) { return isNumber<double>(field); })) {
        addError(result, card, "TL values must be numeric");
        return;
    }
    double impedance{};
    parseNumber(card.fields[4], impedance);
    if (impedance == 0.0) addError(result, card, "TL characteristic impedance must be nonzero");
}

void checkGenericKnownCard(const NecCard& card, ModelCheckResult& result)
{
    const auto* spec = findNecCardSpec(card.mnemonic);
    if (spec == nullptr) return;
    const auto maximumFields = spec->integerFieldCount + spec->numericFieldCount;
    if (maximumFields > 0 && card.fields.size() > maximumFields) {
        addError(result, card, card.mnemonic + " has more fields than the NEC-2 card format allows");
        return;
    }
    for (std::size_t index = 0; index < card.fields.size(); ++index) {
        const auto valid = index < spec->integerFieldCount
            ? isNumber<int>(card.fields[index]) : isNumber<double>(card.fields[index]);
        if (!valid) {
            addError(result, card, card.mnemonic + " field " + std::to_string(index + 1)
                + " has the wrong numeric type");
            return;
        }
    }
}

void checkCardOrdering(const NecDocument& document, ModelCheckResult& result)
{
    const NecCard* lastGeometry{};
    auto geometryEnded = false;
    auto reportedMissingEnd = false;
    auto controlSeen = false;
    for (const auto& card : document.cards()) {
        if (isGeometryCard(card.kind)) {
            lastGeometry = &card;
            if (geometryEnded) {
                addError(result, card, card.mnemonic + " geometry cards must appear before GE");
            }
            if (controlSeen) {
                addError(result, card, card.mnemonic
                    + " geometry cards must precede GN, EX, FR, and other control cards");
            }
            continue;
        }
        if (card.kind == NecCardKind::GeometryEnd) {
            if (geometryEnded) {
                addError(result, card, "Only one GE geometry-end card is allowed");
            }
            geometryEnded = true;
            continue;
        }
        const auto controlCard = isControlCard(card.kind);
        if (controlCard && lastGeometry != nullptr && !geometryEnded && !reportedMissingEnd) {
            addError(result, card, card.mnemonic
                + " appears before GE; terminate GW geometry with a GE card before control cards");
            reportedMissingEnd = true;
        }
        controlSeen = controlSeen || controlCard;
    }
    if (lastGeometry != nullptr && !geometryEnded && !reportedMissingEnd) {
        addError(result, *lastGeometry, "Geometry section requires a GE card after the final GW card");
    }
}

void checkDeckStructure(const NecDocument& document, ModelCheckResult& result)
{
    const NecCard* lastCard{};
    auto leadingSection = true;
    auto commentBlockOpen = false;
    auto endSeen = false;

    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Blank) continue;
        lastCard = &card;

        if (leadingSection) {
            if (card.mnemonic == "CM") {
                commentBlockOpen = true;
            } else if (card.mnemonic == "CE") {
                commentBlockOpen = false;
                leadingSection = false;
            } else {
                if (commentBlockOpen) {
                    addError(result, card, card.mnemonic
                        + " appears before CE; terminate the leading CM comment block with CE");
                    commentBlockOpen = false;
                }
                leadingSection = false;
            }
        }

        if (card.kind == NecCardKind::End) {
            if (endSeen) addError(result, card, "Only one EN end card is allowed");
            endSeen = true;
        } else if (endSeen) {
            addError(result, card, "EN must be the final nonblank card in the deck");
        }
    }

    if (!endSeen) {
        if (lastCard != nullptr) {
            addError(result, *lastCard, "Deck requires a final EN end card");
        } else {
            result.diagnostics.push_back({DiagnosticSeverity::Error, 0,
                "Deck requires a final EN end card", "Card validation"});
        }
    }
}

auto changesCalculationState(const NecCard& card) -> bool
{
    switch (card.kind) {
    case NecCardKind::Excitation:
    case NecCardKind::Load:
    case NecCardKind::Ground:
    case NecCardKind::Frequency:
    case NecCardKind::TransmissionLine:
    case NecCardKind::Network:
        return true;
    case NecCardKind::ControlOther:
        return card.mnemonic == "EK" || card.mnemonic == "KH" || card.mnemonic == "PQ"
            || card.mnemonic == "PT";
    default:
        return false;
    }
}

auto isNearFieldRequest(const NecCard& card) -> bool
{
    return card.kind == NecCardKind::ControlOther
        && (card.mnemonic == "NE" || card.mnemonic == "NH");
}

void checkControlSequence(const NecDocument& document, ModelCheckResult& result)
{
    auto excitationDefined = false;
    auto executionOccurred = false;
    auto frequencyCount = 1;
    auto multiFrequencyLoopCompleted = false;
    std::string lastExecution;
    std::vector<const NecCard*> settingsAwaitingExecution;
    std::vector<const NecCard*> radiationSettingsAwaitingPattern;
    std::vector<const NecCard*> nearFieldsAwaitingExecution;

    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Excitation) excitationDefined = true;
        if (card.kind == NecCardKind::Frequency) {
            frequencyCount = 1;
            if (card.fields.size() > 1 && isNumber<int>(card.fields[1])) {
                parseNumber(card.fields[1], frequencyCount);
                if (frequencyCount == 0) frequencyCount = 1;
            }
            multiFrequencyLoopCompleted = false;
        }

        if (changesCalculationState(card)) {
            settingsAwaitingExecution.push_back(&card);
        }
        if (card.kind == NecCardKind::ControlOther && card.mnemonic == "GD") {
            radiationSettingsAwaitingPattern.push_back(&card);
        }

        const auto nearField = isNearFieldRequest(card);
        if (nearField && frequencyCount > 1) {
            nearFieldsAwaitingExecution.push_back(&card);
        }
        const auto executionRequest = card.kind == NecCardKind::Execute
            || card.kind == NecCardKind::RadiationPattern
            || (nearField && frequencyCount == 1);
        if (!executionRequest) continue;

        if (!excitationDefined) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Error, card,
                card.mnemonic + " requests execution before an EX excitation is defined");
        }

        const auto dividerAfterFieldRequest = card.kind == NecCardKind::Execute
            && (lastExecution == "RP" || lastExecution == "NE" || lastExecution == "NH")
            && settingsAwaitingExecution.empty() && nearFieldsAwaitingExecution.empty();
        if (frequencyCount > 1 && multiFrequencyLoopCompleted && !dividerAfterFieldRequest) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Warning, card,
                card.mnemonic
                    + " follows a completed multi-frequency request without a repeated FR; "
                      "NEC-2 evaluates this request at the final frequency only");
        }

        settingsAwaitingExecution.clear();
        nearFieldsAwaitingExecution.clear();
        if (card.kind == NecCardKind::RadiationPattern) {
            radiationSettingsAwaitingPattern.clear();
        }
        executionOccurred = true;
        if (frequencyCount > 1) multiFrequencyLoopCompleted = true;
        lastExecution = card.mnemonic;
    }

    if (executionOccurred) {
        for (const auto* card : settingsAwaitingExecution) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Error, *card,
                card->mnemonic
                    + " changes calculation state after the final execution request; "
                      "move it before that request or add a later execution request");
        }
        for (const auto* card : radiationSettingsAwaitingPattern) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Error, *card,
                "GD changes radiated-field parameters but no later RP request uses them");
        }
    }
    for (const auto* card : nearFieldsAwaitingExecution) {
        addSequenceDiagnostic(result, DiagnosticSeverity::Error, *card,
            card->mnemonic
                + " does not execute during automatic frequency stepping; "
                  "follow it with XQ or RP");
    }
}

}

auto ModelCheckResult::errorCount() const noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    }));
}

auto ModelCheckResult::warningCount() const noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Warning;
    }));
}

auto ModelCheckResult::informationCount() const noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Information;
    }));
}

auto compatibilityProfileForBackend(std::string_view backendId) noexcept
    -> CompatibilityProfile
{
    if (backendId == "opennec") return CompatibilityProfile::OpenNec;
    if (backendId == "nec2dxs") return CompatibilityProfile::FourNec2;
    return CompatibilityProfile::OriginalNec2;
}

auto compatibilityProfileName(CompatibilityProfile profile) noexcept -> std::string_view
{
    switch (profile) {
    case CompatibilityProfile::FourNec2:
        return "4nec2 NEC2dXS";
    case CompatibilityProfile::OpenNec:
        return "OpenNEC";
    case CompatibilityProfile::OriginalNec2:
    default:
        return "Original NEC-2 / NEC2C";
    }
}

auto NecModelChecker::check(const NecDocument& document, ModelCheckOptions options) const
    -> ModelCheckResult
{
    auto conversion = NecModelConverter{}.convert(document);
    ModelCheckResult result{std::move(conversion.model), {}};
    for (auto& issue : conversion.issues) {
        result.diagnostics.push_back({DiagnosticSeverity::Error, issue.lineNumber,
            std::move(issue.message), "Geometry"});
    }

    checkDeckStructure(document, result);
    checkCardOrdering(document, result);
    checkControlSequence(document, result);

    for (const auto& card : document.cards()) {
        switch (card.kind) {
        case NecCardKind::Excitation:
            checkExcitation(card, result);
            break;
        case NecCardKind::Frequency:
            checkFrequency(card, result);
            break;
        case NecCardKind::Ground:
            checkGround(card, result);
            break;
        case NecCardKind::RadiationPattern:
            checkRadiationPattern(card, result);
            break;
        case NecCardKind::Execute:
            checkExecute(card, result);
            break;
        case NecCardKind::Load:
            checkLoad(card, options.compatibility, result);
            break;
        case NecCardKind::TransmissionLine:
            checkTransmissionLine(card, result);
            break;
        case NecCardKind::ReferenceImpedance:
            checkReferenceImpedance(card, result);
            break;
        case NecCardKind::GeometryOther:
        case NecCardKind::ControlOther:
        case NecCardKind::Network:
            if (std::ranges::none_of(result.diagnostics, [&card](const auto& diagnostic) {
                    return diagnostic.severity == DiagnosticSeverity::Error
                        && diagnostic.lineNumber == card.lineNumber;
                })) {
                checkGenericKnownCard(card, result);
            }
            break;
        case NecCardKind::Unknown:
            result.diagnostics.push_back({DiagnosticSeverity::Warning, card.lineNumber,
                "Unknown card " + card.mnemonic + "; the source line will be preserved",
                "Compatibility"});
            break;
        default:
            break;
        }
    }
    for (const auto& card : document.cards()) {
        if (card.mnemonic != "GH") continue;
        if (std::ranges::any_of(result.model.unrenderedSegments(),
                [&card](const auto& spiral) { return spiral.sourceLine == card.lineNumber; })) {
            result.diagnostics.push_back({DiagnosticSeverity::Warning, card.lineNumber,
                "Flat-spiral GH (zero axial length) is not drawn, and many NEC-2 solvers do not "
                "handle it reliably; check the results carefully",
                "Compatibility"});
        }
    }
    const auto setup = NecSetupConverter{}.convert(document);
    if (result.model.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete model: no valid geometry", "Readiness"});
    }
    if (!setup.frequency) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported FR frequency definition", "Readiness"});
    }
    if (setup.excitations.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported EX voltage source", "Readiness"});
    }
    if (options.expandedSymbolCount != 0) {
        result.diagnostics.push_back({DiagnosticSeverity::Information, 0,
            std::to_string(options.expandedSymbolCount)
                + (options.expandedSymbolCount == 1
                        ? " SY symbol was resolved and expanded in the generated solver deck"
                        : " SY symbols were resolved and expanded in the generated solver deck"),
            "Compatibility"});
    }
    checkStaticAdequacy(document, setup, options.compatibility, result);
    std::ranges::sort(result.diagnostics, {}, &ModelDiagnostic::lineNumber);
    return result;
}

}
