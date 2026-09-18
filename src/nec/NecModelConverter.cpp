#include "nec/NecModelConverter.h"

#include "nec/DeckGeometryUnits.h"

#include <charconv>
#include <cmath>
#include <numbers>
#include <string_view>

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

}

auto NecModelConverter::convert(const NecDocument& document) const -> ModelConversionResult
{
    ModelConversionResult result;
    const auto cards = document.cards();
    for (auto index = std::size_t{}; index < cards.size(); ++index) {
        const auto& card = cards[index];
        if (card.kind == NecCardKind::GeometryScale) {
            if (const auto factor = geometryScaleFactor(card)) result.model.scale(*factor);
            else result.issues.push_back({card.lineNumber,
                "GS requires two integer placeholders and a positive numeric scale factor"});
            continue;
        }
        if (card.mnemonic == "GA") {
            model::Wire wire;
            if (!convertArc(card, wire)) {
                result.issues.push_back({card.lineNumber,
                    "GA requires a tag, positive segment count, arc radius, two distinct angles, and wire radius"});
            } else {
                result.model.addWire(std::move(wire));
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
                result.model.addWire(std::move(wire));
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
        result.model.addWire(std::move(wire));
    }
    return result;
}

}
