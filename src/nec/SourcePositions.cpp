#include "nec/SourcePositions.h"

#include "nec/NecCardFieldEditor.h"
#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/NecSourceEditor.h"
#include "nec/NecSymbolResolver.h"
#include "nec/ParseNumber.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace necwb::nec {
namespace {

constexpr std::string_view MarkPrefix = "necwb:at=";

// Where the mark starts and ends in the text, or none.
auto findMark(std::string_view text) -> std::optional<std::pair<std::size_t, std::size_t>>
{
    const auto comment = necCommentStart(text);
    if (comment == std::string_view::npos) return std::nullopt;
    for (auto position = comment; position + MarkPrefix.size() <= text.size(); ++position) {
        const auto matches = std::ranges::equal(text.substr(position, MarkPrefix.size()), MarkPrefix,
            [](char first, char second) {
                return std::tolower(static_cast<unsigned char>(first)) == second;
            });
        if (!matches) continue;
        auto end = position + MarkPrefix.size();
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        return std::pair{position, end};
    }
    return std::nullopt;
}

auto trimRight(std::string text) -> std::string
{
    text.erase(text.find_last_not_of(" \t") + 1);
    return text;
}

}

auto sourcePositionMark(std::string_view cardText) -> std::optional<double>
{
    const auto mark = findMark(cardText);
    if (!mark) return std::nullopt;
    const auto value = cardText.substr(mark->first + MarkPrefix.size(),
        mark->second - mark->first - MarkPrefix.size());
    if (!value.ends_with('%')) return std::nullopt;
    double percent{};
    if (!parseNumber(value.substr(0, value.size() - 1), percent) || !std::isfinite(percent))
        return std::nullopt;
    return percent;
}

auto sourcePositionText(double percent) -> std::string
{
    if (percent == 0.0) percent = 0.0;
    std::ostringstream text;
    text << std::setprecision(6) << percent;
    return text.str();
}

auto withSourcePositionMark(std::string_view cardText, std::optional<double> percent) -> std::string
{
    auto text = std::string(cardText);
    const auto mark = findMark(text);
    if (percent) {
        const auto written = std::string(MarkPrefix) + sourcePositionText(*percent) + '%';
        if (mark) return text.replace(mark->first, mark->second - mark->first, written);
        if (necCommentStart(text) != std::string::npos) return trimRight(text) + ' ' + written;
        return trimRight(text) + " ' " + written;
    }
    if (!mark) return text;
    auto begin = mark->first;
    while (begin > 0 && (text[begin - 1] == ' ' || text[begin - 1] == '\t')) --begin;
    text.erase(begin, mark->second - begin);
    // A comment with nothing left goes, with the space before it.
    const auto comment = necCommentStart(text);
    if (comment != std::string::npos
        && text.find_first_not_of(" \t", comment + 1) == std::string::npos) {
        text.erase(comment);
        text = trimRight(text);
    }
    return text;
}

auto placeMarkedSources(const NecDocument& resolved, const model::AntennaModel& model)
    -> std::vector<SourcePlacement>
{
    std::vector<SourcePlacement> placements;
    const auto blocks = model.segmentBlocks();
    for (const auto& card : resolved.cards()) {
        if (card.kind != NecCardKind::Excitation) continue;
        const auto percent = sourcePositionMark(card.sourceText);
        if (!percent) continue;
        auto& placed = placements.emplace_back();
        placed.lineNumber = card.lineNumber;
        placed.percent = *percent;
        int type{};
        int tag{};
        if (card.fields.size() < 3 || !parseNumber(card.fields[0], type)
            || !parseNumber(card.fields[1], tag)) {
            placed.error = "EX needs a whole-number type and tag to be placed along a wire";
            continue;
        }
        placed.wireTag = tag;
        if (type != 0 && type != 5) {
            placed.error = "Only an EX voltage source, type 0 or 5, sits on a wire segment, so only "
                "it can be placed along a wire; remove necwb:at from this EX type "
                + std::to_string(type) + " card";
        } else if (*percent < 0.0 || *percent > 100.0) {
            placed.error = "The position along the wire, " + sourcePositionText(*percent)
                + "%, must be from 0% to 100%";
        } else if (tag == 0) {
            placed.error = "Tag 0 numbers segments across the whole structure, so the source cannot "
                "be placed along one wire; give it the wire's tag or choose an exact segment";
        } else {
            const auto count = std::ranges::count(blocks, tag, &model::SegmentBlock::tag);
            const auto block = std::ranges::find(blocks, tag, &model::SegmentBlock::tag);
            if (count == 0) {
                placed.error = "No wire has tag " + std::to_string(tag);
            } else if (count > 1) {
                placed.error = "Tag " + std::to_string(tag) + " is carried by "
                    + std::to_string(count) + " wires, and NEC counts its segments across all of "
                    "them, so the source cannot be placed along one wire; choose an exact segment";
            } else if (block->wire == nullptr) {
                placed.error = "Wire " + std::to_string(tag) + " is a flat spiral, which NEC "
                    "Workbench does not draw, so the source cannot be placed along it; choose an "
                    "exact segment";
            } else {
                placed.placement = model::placeAlongWire(*block->wire, *percent / 100.0);
                if (!placed.placement)
                    placed.error = "Wire " + std::to_string(tag) + " has no length to place the source along";
            }
        }
    }
    return placements;
}

auto refreshSourcePositions(std::string_view source) -> std::string
{
    if (source.find("necwb:at=") == std::string_view::npos) return std::string(source);
    const auto resolution = NecSymbolResolver{}.resolve(source);
    if (!resolution.ok()) return std::string(source);
    const auto resolved = NecParser{}.parse(resolution.resolvedSource);
    const auto placements = placeMarkedSources(resolved, NecModelConverter{}.convert(resolved).model);
    const auto authored = NecParser{}.parse(source);
    auto updated = std::string(source);
    for (const auto& placed : placements) {
        if (!placed.placement || placed.lineNumber > authored.cards().size()) continue;
        const auto& card = authored.cards()[placed.lineNumber - 1];
        const auto segment = std::to_string(placed.placement->segment);
        if (!necCardFieldIsNumeric(card.sourceText, 2) || card.fields[2] == segment) continue;
        const auto line = replaceNecCardFields(card.sourceText,
            std::array{NecFieldReplacement{2, segment}});
        if (auto replaced = line ? replaceSourceLine(updated, placed.lineNumber, *line) : std::nullopt)
            updated = std::move(*replaced);
    }
    return updated;
}

}
