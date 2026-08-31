#include "nec/DeckGeometryUnits.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace necwb::nec {
namespace {

auto nearlyEqual(double first, double second) noexcept -> bool
{
    return std::abs(first - second) <= 1.0e-10 * std::max({1.0, std::abs(first), std::abs(second)});
}

}

auto geometryScaleFactor(const NecCard& card) noexcept -> std::optional<double>
{
    if (card.kind != NecCardKind::GeometryScale || card.fields.size() < 3) return std::nullopt;
    for (auto index = 0; index < 2; ++index) {
        int placeholder{};
        const auto& text = card.fields[static_cast<std::size_t>(index)];
        const auto [end, error] = std::from_chars(
            text.data(), text.data() + text.size(), placeholder);
        if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    }
    double factor{};
    const auto& text = card.fields[2];
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), factor);
    if (error != std::errc{} || end != text.data() + text.size()
        || !std::isfinite(factor) || factor <= 0.0) return std::nullopt;
    return factor;
}

auto geometryScaleForLine(const NecDocument& document, std::size_t sourceLine) noexcept -> double
{
    auto factor = 1.0;
    for (const auto& card : document.cards()) {
        if (card.lineNumber <= sourceLine) continue;
        if (card.kind == NecCardKind::GeometryEnd) break;
        if (const auto scale = geometryScaleFactor(card)) factor *= *scale;
    }
    return factor;
}

auto standardLengthUnit(double scaleToMeters) noexcept -> std::optional<model::LengthUnit>
{
    for (const auto unit : {model::LengthUnit::Meter, model::LengthUnit::Centimeter,
             model::LengthUnit::Millimeter, model::LengthUnit::Inch, model::LengthUnit::Foot}) {
        if (nearlyEqual(scaleToMeters, model::metersPerUnit(unit))) return unit;
    }
    return std::nullopt;
}

auto inspectDeckGeometryUnits(const NecDocument& document) noexcept -> DeckGeometryUnitInfo
{
    DeckGeometryUnitInfo result;
    auto foundWire = false;
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::GeometryScale) {
            result.hasScaleCard = true;
            result.scaleCardLine = card.lineNumber;
        }
        if (card.kind != NecCardKind::GeometryWire) continue;
        const auto scale = geometryScaleForLine(document, card.lineNumber);
        if (!foundWire) {
            result.scaleToMeters = scale;
            foundWire = true;
        } else if (!nearlyEqual(result.scaleToMeters, scale)) {
            result.uniform = false;
        }
    }
    if (!foundWire && result.hasScaleCard) {
        auto scale = 1.0;
        for (const auto& card : document.cards()) {
            if (card.kind == NecCardKind::GeometryEnd) break;
            if (const auto factor = geometryScaleFactor(card)) scale *= *factor;
        }
        result.scaleToMeters = scale;
    }
    result.standardUnit = result.uniform ? standardLengthUnit(result.scaleToMeters) : std::nullopt;
    return result;
}

}
