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

auto applyMove(const NecCard& card, std::vector<model::Wire>& wires) -> bool
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
        return true;
    }
    auto previous = std::vector<model::Wire>(wires.begin() + static_cast<std::ptrdiff_t>(first),
        wires.end());
    for (auto repetition = 0; repetition < repetitions; ++repetition) {
        for (auto& wire : previous)
            transformWire(wire, transform, tagIncrement, card.lineNumber);
        wires.insert(wires.end(), previous.begin(), previous.end());
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

auto reflectPoint(model::Point3D point, ReflectionAxis axis) noexcept -> model::Point3D
{
    if (axis == ReflectionAxis::X) point.x = -point.x;
    else if (axis == ReflectionAxis::Y) point.y = -point.y;
    else point.z = -point.z;
    return point;
}

auto applyReflection(const NecCard& card, std::vector<model::Wire>& wires) -> bool
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
        currentIncrement *= 2;
    }
    return true;
}

auto applyRotation(const NecCard& card, std::vector<model::Wire>& wires) -> bool
{
    if (card.fields.size() < 2) return false;
    int tagIncrement{};
    int copies{};
    if (!parseNumber(card.fields[0], tagIncrement)
        || !parseNumber(card.fields[1], copies) || copies <= 0) {
        return false;
    }
    const auto originals = wires;
    for (auto copy = 1; copy < copies; ++copy) {
        const auto transform = coordinateTransform(0.0, 0.0,
            360.0 * static_cast<double>(copy) / copies, {});
        for (auto wire : originals) {
            transformWire(wire, transform, copy * tagIncrement, card.lineNumber);
            wires.push_back(std::move(wire));
        }
    }
    return true;
}

}

auto NecModelConverter::convert(const NecDocument& document) const -> ModelConversionResult
{
    ModelConversionResult result;
    std::vector<model::Wire> wires;
    const auto cards = document.cards();
    for (auto index = std::size_t{}; index < cards.size(); ++index) {
        const auto& card = cards[index];
        if (card.kind == NecCardKind::GeometryScale) {
            if (const auto factor = geometryScaleFactor(card)) {
                for (auto& wire : wires) scaleWire(wire, *factor);
            }
            else result.issues.push_back({card.lineNumber,
                "GS requires two integer placeholders and a positive numeric scale factor"});
            continue;
        }
        if (card.mnemonic == "GM") {
            if (!applyMove(card, wires)) result.issues.push_back({card.lineNumber,
                "GM requires a tag increment, nonnegative repetition count, and up to seven numeric transformation fields"});
            continue;
        }
        if (card.mnemonic == "GX") {
            if (!applyReflection(card, wires)) result.issues.push_back({card.lineNumber,
                "GX requires a tag increment and nonzero XYZ reflection flags; reflected wires cannot lie in or cross a selected symmetry plane"});
            continue;
        }
        if (card.mnemonic == "GR") {
            if (!applyRotation(card, wires)) result.issues.push_back({card.lineNumber,
                "GR requires a tag increment and a positive total copy count"});
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
    return result;
}

}
