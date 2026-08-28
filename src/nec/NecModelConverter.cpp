#include "nec/NecModelConverter.h"

#include <charconv>
#include <string_view>

namespace necwb::nec {
namespace {

template<typename Value>
auto parseNumber(std::string_view text, Value& value) -> bool
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

auto convertWire(const NecCard& card, model::Wire& wire) -> bool
{
    if (card.fields.size() != 9) {
        return false;
    }

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

}

auto NecModelConverter::convert(const NecDocument& document) const -> ModelConversionResult
{
    ModelConversionResult result;
    for (const auto& card : document.cards()) {
        if (card.kind != NecCardKind::GeometryWire) {
            continue;
        }

        model::Wire wire;
        if (!convertWire(card, wire)) {
            result.issues.push_back({card.lineNumber, "GW requires two integers and seven numeric values"});
            continue;
        }
        if (wire.segments <= 0) {
            result.issues.push_back({card.lineNumber, "GW segment count must be positive"});
            continue;
        }
        if (wire.radius <= 0.0) {
            result.issues.push_back({card.lineNumber, "GW radius must be positive"});
            continue;
        }
        if (wire.start == wire.end) {
            result.issues.push_back({card.lineNumber, "GW endpoints must define a non-zero-length wire"});
            continue;
        }
        result.model.addWire(wire);
    }
    return result;
}

}
