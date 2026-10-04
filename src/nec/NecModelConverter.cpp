#include "nec/NecModelConverter.h"

#include "nec/DeckGeometryUnits.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

namespace necwb::nec {
namespace {

template<typename Value>
auto parseNumber(std::string_view text, Value& value) -> bool
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

auto parseWireFields(const NecCard& card, model::Wire& wire) -> bool
{
    if (card.fields.size() != 9) return false;
    wire.sourceLine = card.lineNumber;
    return parseNumber(card.fields[0], wire.tag)
        && parseNumber(card.fields[1], wire.segments)
        && parseNumber(card.fields[2], wire.start.x)
        && parseNumber(card.fields[3], wire.start.y)
        && parseNumber(card.fields[4], wire.start.z)
        && parseNumber(card.fields[5], wire.end.x)
        && parseNumber(card.fields[6], wire.end.y)
        && parseNumber(card.fields[7], wire.end.z)
        && parseNumber(card.fields[8], wire.radius);
}

auto validWireBasics(const model::Wire& wire) noexcept -> bool
{
    return wire.segments > 0 && wire.start != wire.end;
}

auto applyTaper(const NecCard& card, model::Wire& wire) -> bool
{
    if (card.fields.size() < 5) return false;
    int firstPlaceholder{};
    int secondPlaceholder{};
    double lengthRatio{};
    double firstRadius{};
    double lastRadius{};
    if (!parseNumber(card.fields[0], firstPlaceholder)
        || !parseNumber(card.fields[1], secondPlaceholder)
        || !parseNumber(card.fields[2], lengthRatio)
        || !parseNumber(card.fields[3], firstRadius)
        || !parseNumber(card.fields[4], lastRadius)
        || lengthRatio <= 0.0 || firstRadius <= 0.0 || lastRadius <= 0.0) {
        return false;
    }

    wire.path.clear();
    wire.path.reserve(static_cast<std::size_t>(wire.segments + 1));
    wire.path.push_back(wire.start);
    auto accumulated = 0.0;
    auto denominator = 0.0;
    auto weight = 1.0;
    for (auto segment = 0; segment < wire.segments; ++segment) {
        denominator += weight;
        weight *= lengthRatio;
    }
    weight = 1.0;
    for (auto segment = 1; segment <= wire.segments; ++segment) {
        accumulated += weight / denominator;
        weight *= lengthRatio;
        wire.path.push_back({wire.start.x + (wire.end.x - wire.start.x) * accumulated,
            wire.start.y + (wire.end.y - wire.start.y) * accumulated,
            wire.start.z + (wire.end.z - wire.start.z) * accumulated});
    }
    wire.path.back() = wire.end;
    wire.radius = firstRadius;
    wire.endRadius = lastRadius;
    wire.editable = false;
    return true;
}

auto convertArc(const NecCard& card, model::Wire& wire) -> bool
{
    if (card.fields.size() < 6) return false;
    double arcRadius{};
    double firstAngle{};
    double secondAngle{};
    wire.sourceLine = card.lineNumber;
    wire.geometryKind = model::WireGeometryKind::Arc;
    wire.editable = false;
    if (!parseNumber(card.fields[0], wire.tag)
        || !parseNumber(card.fields[1], wire.segments)
        || !parseNumber(card.fields[2], arcRadius)
        || !parseNumber(card.fields[3], firstAngle)
        || !parseNumber(card.fields[4], secondAngle)
        || !parseNumber(card.fields[5], wire.radius)
        || wire.segments <= 0 || arcRadius <= 0.0 || wire.radius <= 0.0
        || firstAngle == secondAngle) {
        return false;
    }
    wire.path.reserve(static_cast<std::size_t>(wire.segments + 1));
    for (auto segment = 0; segment <= wire.segments; ++segment) {
        const auto fraction = static_cast<double>(segment) / wire.segments;
        const auto degrees = firstAngle + (secondAngle - firstAngle) * fraction;
        const auto angle = degrees * std::numbers::pi / 180.0;
        wire.path.push_back({arcRadius * std::cos(angle), 0.0, arcRadius * std::sin(angle)});
    }
    wire.start = wire.path.front();
    wire.end = wire.path.back();
    return true;
}

auto convertHelix(const NecCard& card, model::Wire& wire) -> bool
{
    if (card.fields.size() < 9) return false;
    double turnSpacing{};
    double totalHeight{};
    double startRadiusX{};
    double startRadiusY{};
    double endRadiusX{};
    double endRadiusY{};
    wire.sourceLine = card.lineNumber;
    wire.geometryKind = model::WireGeometryKind::Helix;
    wire.editable = false;
    if (!parseNumber(card.fields[0], wire.tag)
        || !parseNumber(card.fields[1], wire.segments)
        || !parseNumber(card.fields[2], turnSpacing)
        || !parseNumber(card.fields[3], totalHeight)
        || !parseNumber(card.fields[4], startRadiusX)
        || !parseNumber(card.fields[5], startRadiusY)
        || !parseNumber(card.fields[6], endRadiusX)
        || !parseNumber(card.fields[7], endRadiusY)
        || !parseNumber(card.fields[8], wire.radius)
        || wire.segments <= 0 || turnSpacing == 0.0 || totalHeight == 0.0
        || startRadiusX <= 0.0 || startRadiusY <= 0.0
        || endRadiusX <= 0.0 || endRadiusY <= 0.0 || wire.radius <= 0.0) {
        return false;
    }
    wire.path.reserve(static_cast<std::size_t>(wire.segments + 1));
    for (auto segment = 0; segment <= wire.segments; ++segment) {
        const auto fraction = static_cast<double>(segment) / wire.segments;
        const auto angle = 2.0 * std::numbers::pi * totalHeight * fraction / turnSpacing;
        const auto radiusX = startRadiusX + (endRadiusX - startRadiusX) * fraction;
        const auto radiusY = startRadiusY + (endRadiusY - startRadiusY) * fraction;
        wire.path.push_back({radiusX * std::cos(angle), radiusY * std::sin(angle),
            totalHeight * fraction});
    }
    wire.start = wire.path.front();
    wire.end = wire.path.back();
    return true;
}

auto validUnrenderedSpiral(const NecCard& card) -> bool
{
    if (card.fields.size() < 9) return false;
    int tag{};
    int segments{};
    double turnSpacing{};
    double totalHeight{};
    double startRadiusX{};
    double startRadiusY{};
    double endRadiusX{};
    double endRadiusY{};
    double wireRadius{};
    return parseNumber(card.fields[0], tag)
        && parseNumber(card.fields[1], segments)
        && parseNumber(card.fields[2], turnSpacing)
        && parseNumber(card.fields[3], totalHeight)
        && parseNumber(card.fields[4], startRadiusX)
        && parseNumber(card.fields[5], startRadiusY)
        && parseNumber(card.fields[6], endRadiusX)
        && parseNumber(card.fields[7], endRadiusY)
        && parseNumber(card.fields[8], wireRadius)
        && segments > 0 && turnSpacing != 0.0 && totalHeight == 0.0
        && startRadiusX > 0.0 && startRadiusY > 0.0
        && endRadiusX > 0.0 && endRadiusY > 0.0 && wireRadius > 0.0;
}

auto add(const model::Point3D& first, const model::Point3D& second) noexcept -> model::Point3D
{
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

auto subtract(const model::Point3D& first, const model::Point3D& second) noexcept
    -> model::Point3D
{
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

auto multiply(const model::Point3D& point, double factor) noexcept -> model::Point3D
{
    return {point.x * factor, point.y * factor, point.z * factor};
}

auto cross(const model::Point3D& first, const model::Point3D& second) noexcept
    -> model::Point3D
{
    return {first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x};
}

auto magnitude(const model::Point3D& point) noexcept -> double
{
    return std::sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
}

auto parsePoint(const NecCard& card, std::size_t firstField, model::Point3D& point) -> bool
{
    return card.fields.size() >= firstField + 3
        && parseNumber(card.fields[firstField], point.x)
        && parseNumber(card.fields[firstField + 1], point.y)
        && parseNumber(card.fields[firstField + 2], point.z);
}

auto validPatchCorners(const std::vector<model::Point3D>& corners) noexcept -> bool
{
    if (corners.size() < 3) return false;
    return magnitude(cross(subtract(corners[1], corners[0]),
               subtract(corners[2], corners[1]))) > 1.0e-12;
}

auto arbitraryPatch(const NecCard& card, model::SurfacePatch& patch) -> bool
{
    if (card.fields.size() < 8) return false;
    model::Point3D center;
    double elevationDegrees{};
    double azimuthDegrees{};
    double area{};
    if (!parsePoint(card, 2, center)
        || !parseNumber(card.fields[5], elevationDegrees)
        || !parseNumber(card.fields[6], azimuthDegrees)
        || !parseNumber(card.fields[7], area) || area <= 0.0) return false;

    const auto elevation = elevationDegrees * std::numbers::pi / 180.0;
    const auto azimuth = azimuthDegrees * std::numbers::pi / 180.0;
    const model::Point3D normal{std::cos(elevation) * std::cos(azimuth),
        std::cos(elevation) * std::sin(azimuth), std::sin(elevation)};
    const auto horizontal = std::hypot(normal.x, normal.y);
    const auto tangent1 = horizontal >= 1.0e-6
        ? model::Point3D{-normal.y / horizontal, normal.x / horizontal, 0.0}
        : model::Point3D{1.0, 0.0, 0.0};
    const auto tangent2 = cross(normal, tangent1);
    const auto halfSide = std::sqrt(area) / 2.0;
    const auto firstOffset = multiply(tangent1, halfSide);
    const auto secondOffset = multiply(tangent2, halfSide);
    patch.kind = model::SurfacePatchKind::Arbitrary;
    patch.sourceLine = card.lineNumber;
    patch.corners = {
        subtract(subtract(center, firstOffset), secondOffset),
        add(subtract(center, secondOffset), firstOffset),
        add(add(center, firstOffset), secondOffset),
        add(subtract(center, firstOffset), secondOffset),
    };
    return true;
}

auto shapedPatch(const NecCard& card, const NecCard& continuation, int shape,
    model::SurfacePatch& patch) -> bool
{
    model::Point3D first;
    model::Point3D second;
    model::Point3D third;
    model::Point3D fourth;
    if (!parsePoint(card, 2, first) || !parsePoint(card, 5, second)
        || !parsePoint(continuation, 2, third)) return false;
    patch.sourceLine = card.lineNumber;
    if (shape == 1) {
        fourth = add(first, subtract(third, second));
        patch.kind = model::SurfacePatchKind::Rectangular;
        patch.corners = {first, second, third, fourth};
    } else if (shape == 2) {
        patch.kind = model::SurfacePatchKind::Triangular;
        patch.corners = {first, second, third};
    } else if (shape == 3 && parsePoint(continuation, 5, fourth)) {
        patch.kind = model::SurfacePatchKind::Quadrilateral;
        patch.corners = {first, second, third, fourth};
    } else {
        return false;
    }
    return validPatchCorners(patch.corners);
}

auto appendPatchGrid(const NecCard& card, const NecCard& continuation,
    std::vector<model::SurfacePatch>& patches) -> bool
{
    if (card.fields.size() < 8) return false;
    int firstCount{};
    int secondCount{};
    model::Point3D first;
    model::Point3D second;
    model::Point3D third;
    if (!parseNumber(card.fields[0], firstCount)
        || !parseNumber(card.fields[1], secondCount)
        || firstCount <= 0 || secondCount <= 0
        || !parsePoint(card, 2, first) || !parsePoint(card, 5, second)
        || !parsePoint(continuation, 2, third)) return false;
    const auto firstStep = multiply(subtract(second, first), 1.0 / firstCount);
    const auto secondStep = multiply(subtract(third, second), 1.0 / secondCount);
    if (magnitude(cross(firstStep, secondStep)) <= 1.0e-12) return false;
    for (auto secondIndex = 0; secondIndex < secondCount; ++secondIndex) {
        for (auto firstIndex = 0; firstIndex < firstCount; ++firstIndex) {
            const auto corner = add(first, add(multiply(firstStep, firstIndex),
                multiply(secondStep, secondIndex)));
            model::SurfacePatch patch;
            patch.kind = model::SurfacePatchKind::GridCell;
            patch.sourceLine = card.lineNumber;
            patch.corners = {corner, add(corner, firstStep),
                add(add(corner, firstStep), secondStep), add(corner, secondStep)};
            patches.push_back(std::move(patch));
        }
    }
    return true;
}

auto scaleWire(model::Wire& wire, double factor) noexcept -> void
{
    const auto scalePoint = [factor](model::Point3D& point) {
        point.x *= factor;
        point.y *= factor;
        point.z *= factor;
    };
    scalePoint(wire.start);
    scalePoint(wire.end);
    for (auto& point : wire.path) scalePoint(point);
    wire.radius *= factor;
    wire.endRadius *= factor;
}

auto scalePatch(model::SurfacePatch& patch, double factor) noexcept -> void
{
    for (auto& point : patch.corners) point = multiply(point, factor);
}

struct CoordinateTransform {
    double xx{};
    double xy{};
    double xz{};
    double yx{};
    double yy{};
    double yz{};
    double zx{};
    double zy{};
    double zz{};
    model::Point3D translation;
};

auto coordinateTransform(double rotationXDegrees, double rotationYDegrees,
    double rotationZDegrees, model::Point3D translation) -> CoordinateTransform
{
    const auto rotationX = rotationXDegrees * std::numbers::pi / 180.0;
    const auto rotationY = rotationYDegrees * std::numbers::pi / 180.0;
    const auto rotationZ = rotationZDegrees * std::numbers::pi / 180.0;
    const auto sineX = std::sin(rotationX);
    const auto cosineX = std::cos(rotationX);
    const auto sineY = std::sin(rotationY);
    const auto cosineY = std::cos(rotationY);
    const auto sineZ = std::sin(rotationZ);
    const auto cosineZ = std::cos(rotationZ);
    return {
        cosineZ * cosineY,
        cosineZ * sineY * sineX - sineZ * cosineX,
        cosineZ * sineY * cosineX + sineZ * sineX,
        sineZ * cosineY,
        sineZ * sineY * sineX + cosineZ * cosineX,
        sineZ * sineY * cosineX - cosineZ * sineX,
        -sineY,
        cosineY * sineX,
        cosineY * cosineX,
        translation,
    };
}

auto transformPoint(const model::Point3D& point, const CoordinateTransform& transform)
    -> model::Point3D
{
    return {
        point.x * transform.xx + point.y * transform.xy + point.z * transform.xz
            + transform.translation.x,
        point.x * transform.yx + point.y * transform.yy + point.z * transform.yz
            + transform.translation.y,
        point.x * transform.zx + point.y * transform.zy + point.z * transform.zz
            + transform.translation.z,
    };
}

auto transformWire(model::Wire& wire, const CoordinateTransform& transform,
    int tagIncrement, std::size_t sourceLine) -> void
{
    wire.start = transformPoint(wire.start, transform);
    wire.end = transformPoint(wire.end, transform);
    for (auto& point : wire.path) point = transformPoint(point, transform);
    if (wire.tag != 0) wire.tag += tagIncrement;
    wire.sourceLine = sourceLine;
    wire.editable = false;
}

auto transformPatch(model::SurfacePatch& patch, const CoordinateTransform& transform,
    std::size_t sourceLine) -> void
{
    for (auto& point : patch.corners) point = transformPoint(point, transform);
    patch.sourceLine = sourceLine;
}

auto applyMove(const NecCard& card, std::vector<model::Wire>& wires,
    std::vector<model::SurfacePatch>& patches) -> bool
{
    if (card.fields.size() < 2 || card.fields.size() > 9) return false;
    int tagIncrement{};
    int repetitions{};
    if (!parseNumber(card.fields[0], tagIncrement)
        || !parseNumber(card.fields[1], repetitions) || repetitions < 0) {
        return false;
    }
    std::array<double, 7> values{};
    for (auto index = std::size_t{2}; index < card.fields.size(); ++index) {
        if (!parseNumber(card.fields[index], values[index - 2])) return false;
    }
    const auto firstTag = static_cast<int>(values[6] + 0.5);
    auto first = std::size_t{};
    if (firstTag > 0) {
        const auto found = std::ranges::find(wires, firstTag, &model::Wire::tag);
        if (found != wires.end()) first = static_cast<std::size_t>(found - wires.begin());
    }
    const auto transform = coordinateTransform(values[0], values[1], values[2],
        {values[3], values[4], values[5]});
    if (repetitions == 0) {
        for (auto index = first; index < wires.size(); ++index)
            transformWire(wires[index], transform, tagIncrement, card.lineNumber);
        for (auto& patch : patches) transformPatch(patch, transform, card.lineNumber);
        return true;
    }
    auto previous = std::vector<model::Wire>(wires.begin() + static_cast<std::ptrdiff_t>(first),
        wires.end());
    for (auto repetition = 0; repetition < repetitions; ++repetition) {
        for (auto& wire : previous)
            transformWire(wire, transform, tagIncrement, card.lineNumber);
        wires.insert(wires.end(), previous.begin(), previous.end());
    }
    auto previousPatches = patches;
    for (auto repetition = 0; repetition < repetitions; ++repetition) {
        for (auto& patch : previousPatches)
            transformPatch(patch, transform, card.lineNumber);
        patches.insert(patches.end(), previousPatches.begin(), previousPatches.end());
    }
    return true;
}

enum class ReflectionAxis { X, Y, Z };

auto coordinate(const model::Point3D& point, ReflectionAxis axis) noexcept -> double
{
    if (axis == ReflectionAxis::X) return point.x;
    if (axis == ReflectionAxis::Y) return point.y;
    return point.z;
}

auto crossesReflectionPlane(const model::Wire& wire, ReflectionAxis axis) noexcept -> bool
{
    const auto pointCount = wire.path.size() >= 2 ? wire.path.size() : std::size_t{2};
    for (auto index = std::size_t{1}; index < pointCount; ++index) {
        const auto& first = wire.path.size() >= 2 ? wire.path[index - 1] : wire.start;
        const auto& second = wire.path.size() >= 2 ? wire.path[index] : wire.end;
        const auto firstValue = coordinate(first, axis);
        const auto secondValue = coordinate(second, axis);
        if (std::abs(firstValue) + std::abs(secondValue) <= 1.0e-12
            || firstValue * secondValue < -1.0e-12) {
            return true;
        }
    }
    return false;
}

auto crossesReflectionPlane(const model::SurfacePatch& patch, ReflectionAxis axis) noexcept -> bool
{
    auto hasPositive = false;
    auto hasNegative = false;
    for (const auto& corner : patch.corners) {
        const auto value = coordinate(corner, axis);
        if (std::abs(value) <= 1.0e-12) return true;
        hasPositive = hasPositive || value > 0.0;
        hasNegative = hasNegative || value < 0.0;
    }
    return hasPositive && hasNegative;
}

auto reflectPoint(model::Point3D point, ReflectionAxis axis) noexcept -> model::Point3D
{
    if (axis == ReflectionAxis::X) point.x = -point.x;
    else if (axis == ReflectionAxis::Y) point.y = -point.y;
    else point.z = -point.z;
    return point;
}

auto applyReflection(const NecCard& card, std::vector<model::Wire>& wires,
    std::vector<model::SurfacePatch>& patches) -> bool
{
    if (card.fields.size() < 2) return false;
    int tagIncrement{};
    int flags{};
    if (!parseNumber(card.fields[0], tagIncrement)
        || !parseNumber(card.fields[1], flags) || flags <= 0) {
        return false;
    }
    const std::array axes{
        std::pair{ReflectionAxis::Z, flags % 10 != 0},
        std::pair{ReflectionAxis::Y, (flags / 10) % 10 != 0},
        std::pair{ReflectionAxis::X, (flags / 100) % 10 != 0},
    };
    auto currentIncrement = tagIncrement;
    for (const auto& [axis, enabled] : axes) {
        if (!enabled) continue;
        if (std::ranges::any_of(wires,
                [axis](const auto& wire) { return crossesReflectionPlane(wire, axis); })) {
            return false;
        }
        if (std::ranges::any_of(patches,
                [axis](const auto& patch) { return crossesReflectionPlane(patch, axis); }))
            return false;
        const auto sourceCount = wires.size();
        for (auto index = std::size_t{}; index < sourceCount; ++index) {
            auto reflected = wires[index];
            reflected.start = reflectPoint(reflected.start, axis);
            reflected.end = reflectPoint(reflected.end, axis);
            for (auto& point : reflected.path) point = reflectPoint(point, axis);
            if (reflected.tag != 0) reflected.tag += currentIncrement;
            reflected.sourceLine = card.lineNumber;
            reflected.editable = false;
            wires.push_back(std::move(reflected));
        }
        const auto patchCount = patches.size();
        for (auto index = std::size_t{}; index < patchCount; ++index) {
            auto reflected = patches[index];
            for (auto& point : reflected.corners) point = reflectPoint(point, axis);
            reflected.sourceLine = card.lineNumber;
            patches.push_back(std::move(reflected));
        }
        currentIncrement *= 2;
    }
    return true;
}

auto applyRotation(const NecCard& card, std::vector<model::Wire>& wires,
    std::vector<model::SurfacePatch>& patches) -> bool
{
    if (card.fields.size() < 2) return false;
    int tagIncrement{};
    int copies{};
    if (!parseNumber(card.fields[0], tagIncrement)
        || !parseNumber(card.fields[1], copies) || copies <= 0) {
        return false;
    }
    const auto originals = wires;
    const auto originalPatches = patches;
    for (auto copy = 1; copy < copies; ++copy) {
        const auto transform = coordinateTransform(0.0, 0.0,
            360.0 * static_cast<double>(copy) / copies, {});
        for (auto wire : originals) {
            transformWire(wire, transform, copy * tagIncrement, card.lineNumber);
            wires.push_back(std::move(wire));
        }
        for (auto patch : originalPatches) {
            transformPatch(patch, transform, card.lineNumber);
            patches.push_back(std::move(patch));
        }
    }
    return true;
}

}

auto NecModelConverter::convert(const NecDocument& document) const -> ModelConversionResult
{
    ModelConversionResult result;
    std::vector<model::Wire> wires;
    std::vector<model::SurfacePatch> patches;
    const auto cards = document.cards();
    for (auto index = std::size_t{}; index < cards.size(); ++index) {
        const auto& card = cards[index];
        if (card.kind == NecCardKind::GeometryScale) {
            if (const auto factor = geometryScaleFactor(card)) {
                for (auto& wire : wires) scaleWire(wire, *factor);
                for (auto& patch : patches) scalePatch(patch, *factor);
            }
            else result.issues.push_back({card.lineNumber,
                "GS requires two integer placeholders and a positive numeric scale factor"});
            continue;
        }
        if (card.mnemonic == "GM") {
            if (!applyMove(card, wires, patches)) result.issues.push_back({card.lineNumber,
                "GM requires a tag increment, nonnegative repetition count, and up to seven numeric transformation fields"});
            continue;
        }
        if (card.mnemonic == "GX") {
            if (!applyReflection(card, wires, patches)) result.issues.push_back({card.lineNumber,
                "GX requires a tag increment and nonzero XYZ reflection flags; reflected wires cannot lie in or cross a selected symmetry plane"});
            continue;
        }
        if (card.mnemonic == "GR") {
            if (!applyRotation(card, wires, patches)) result.issues.push_back({card.lineNumber,
                "GR requires a tag increment and a positive total copy count"});
            continue;
        }
        if (card.mnemonic == "SP") {
            int placeholder{};
            int shape{};
            if (card.fields.size() < 2 || !parseNumber(card.fields[0], placeholder)
                || !parseNumber(card.fields[1], shape) || placeholder != 0
                || shape < 0 || shape > 3) {
                result.issues.push_back({card.lineNumber,
                    "SP requires a zero I1 placeholder and a patch shape from 0 through 3"});
                continue;
            }
            if (shape == 0) {
                model::SurfacePatch patch;
                if (!arbitraryPatch(card, patch)) result.issues.push_back({card.lineNumber,
                    "SP arbitrary patch requires center XYZ, elevation, azimuth, and positive area"});
                else patches.push_back(std::move(patch));
                continue;
            }
            if (index + 1 >= cards.size() || cards[index + 1].mnemonic != "SC") {
                result.issues.push_back({card.lineNumber,
                    "SP rectangular, triangular, and quadrilateral patches require a following SC card"});
                continue;
            }
            model::SurfacePatch patch;
            if (!shapedPatch(card, cards[index + 1], shape, patch)) {
                result.issues.push_back({card.lineNumber,
                    "SP/SC patch corners must define a non-zero-area surface"});
                ++index;
                continue;
            }
            patches.push_back(std::move(patch));
            auto previousThird = patches.back().corners[2];
            auto previousFourth = patches.back().corners.size() == 4
                ? patches.back().corners[3] : model::Point3D{};
            ++index;
            while ((shape == 1 || shape == 3) && index + 1 < cards.size()
                && cards[index + 1].mnemonic == "SC") {
                const auto& continuation = cards[++index];
                int continuationShape{};
                model::Point3D third;
                model::Point3D fourth;
                if (continuation.fields.size() < 5
                    || !parseNumber(continuation.fields[1], continuationShape)
                    || (continuationShape != 1 && continuationShape != 3)
                    || !parsePoint(continuation, 2, third)) {
                    result.issues.push_back({continuation.lineNumber,
                        "Additional SC cards require rectangular or quadrilateral shape data"});
                    continue;
                }
                model::SurfacePatch linked;
                linked.kind = continuationShape == 1 ? model::SurfacePatchKind::Rectangular
                                                     : model::SurfacePatchKind::Quadrilateral;
                linked.sourceLine = continuation.lineNumber;
                if (continuationShape == 1) {
                    fourth = add(previousFourth, subtract(third, previousThird));
                } else if (!parsePoint(continuation, 5, fourth)) {
                    result.issues.push_back({continuation.lineNumber,
                        "Quadrilateral SC continuation requires both remaining corners"});
                    continue;
                }
                linked.corners = {previousFourth, previousThird, third, fourth};
                if (!validPatchCorners(linked.corners)) {
                    result.issues.push_back({continuation.lineNumber,
                        "SC continuation corners must define a non-zero-area surface"});
                    continue;
                }
                patches.push_back(std::move(linked));
                previousThird = third;
                previousFourth = fourth;
                shape = continuationShape;
            }
            continue;
        }
        if (card.mnemonic == "SM") {
            if (index + 1 >= cards.size() || cards[index + 1].mnemonic != "SC") {
                result.issues.push_back({card.lineNumber,
                    "SM requires a following SC card with the third surface corner"});
                continue;
            }
            if (!appendPatchGrid(card, cards[index + 1], patches)) {
                result.issues.push_back({card.lineNumber,
                    "SM requires positive patch counts and three non-collinear surface corners"});
            }
            ++index;
            continue;
        }
        if (card.mnemonic == "SC") {
            result.issues.push_back({card.lineNumber,
                "SC must immediately continue an SP or SM surface definition"});
            continue;
        }
        if (card.mnemonic == "GA") {
            model::Wire wire;
            if (!convertArc(card, wire)) {
                result.issues.push_back({card.lineNumber,
                    "GA requires a tag, positive segment count, arc radius, two distinct angles, and wire radius"});
            } else {
                wires.push_back(std::move(wire));
            }
            continue;
        }
        if (card.mnemonic == "GH") {
            model::Wire wire;
            if (!convertHelix(card, wire)) {
                if (!validUnrenderedSpiral(card)) {
                    result.issues.push_back({card.lineNumber,
                        "GH requires a tag, positive segment count, nonzero turn spacing, nonnegative axial length, positive endpoint radii, and wire radius"});
                }
            } else {
                wires.push_back(std::move(wire));
            }
            continue;
        }
        if (card.kind != NecCardKind::GeometryWire) continue;

        model::Wire wire;
        if (!parseWireFields(card, wire)) {
            result.issues.push_back({card.lineNumber,
                "GW requires two integers and seven numeric values"});
            continue;
        }
        if (!validWireBasics(wire)) {
            result.issues.push_back({card.lineNumber,
                wire.segments <= 0 ? "GW segment count must be positive"
                                   : "GW endpoints must define a non-zero-length wire"});
            continue;
        }
        if (wire.radius == 0.0) {
            if (index + 1 >= cards.size() || cards[index + 1].mnemonic != "GC"
                || !applyTaper(cards[index + 1], wire)) {
                result.issues.push_back({card.lineNumber,
                    "GW with zero radius requires a following valid GC taper card"});
                continue;
            }
            ++index;
        } else if (wire.radius < 0.0) {
            result.issues.push_back({card.lineNumber, "GW radius must be positive"});
            continue;
        }
        wires.push_back(std::move(wire));
    }
    for (auto& wire : wires) result.model.addWire(std::move(wire));
    for (auto& patch : patches) result.model.addSurfacePatch(std::move(patch));
    return result;
}

}
