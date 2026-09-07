#include "nec/NecParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace necwb::nec {
namespace {

auto upper(std::string value) -> std::string
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

auto classify(std::string_view mnemonic) -> NecCardKind
{
    static const std::unordered_map<std::string_view, NecCardKind> kinds{
        {"CM", NecCardKind::Comment},
        {"CE", NecCardKind::Comment},
        {"SY", NecCardKind::Symbol},
        {"GW", NecCardKind::GeometryWire},
        {"GS", NecCardKind::GeometryScale},
        {"GE", NecCardKind::GeometryEnd},
        {"EX", NecCardKind::Excitation},
        {"LD", NecCardKind::Load},
        {"GN", NecCardKind::Ground},
        {"FR", NecCardKind::Frequency},
        {"RP", NecCardKind::RadiationPattern},
        {"XQ", NecCardKind::Execute},
        {"TL", NecCardKind::TransmissionLine},
        {"NT", NecCardKind::Network},
        {"Z0", NecCardKind::ReferenceImpedance},
        {"ZO", NecCardKind::ReferenceImpedance},
        {"EN", NecCardKind::End},
    };

    const auto found = kinds.find(mnemonic);
    return found == kinds.end() ? NecCardKind::Unknown : found->second;
}

auto parseLine(std::string line, std::size_t lineNumber) -> NecCard
{
    std::istringstream input(line);
    std::string mnemonic;
    input >> mnemonic;

    NecCard card;
    card.sourceText = std::move(line);
    card.lineNumber = lineNumber;

    if (mnemonic.empty()) {
        card.kind = NecCardKind::Blank;
        return card;
    }

    card.mnemonic = upper(std::move(mnemonic));
    card.kind = classify(card.mnemonic);

    std::string field;
    while (input >> field) {
        card.fields.push_back(std::move(field));
    }
    return card;
}

}

auto NecParser::parse(std::string_view source) const -> NecDocument
{
    const auto firstNewline = source.find('\n');
    const bool usesCrlf = firstNewline != std::string_view::npos && firstNewline > 0
        && source[firstNewline - 1] == '\r';
    NecDocument document(usesCrlf ? "\r\n" : "\n");

    std::size_t lineNumber = 1;
    std::size_t position = 0;
    while (position < source.size()) {
        const auto newline = source.find('\n', position);
        const auto end = newline == std::string_view::npos ? source.size() : newline;
        auto line = std::string(source.substr(position, end - position));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        document.addCard(parseLine(std::move(line), lineNumber));
        ++lineNumber;

        if (newline == std::string_view::npos) {
            position = source.size();
        } else {
            position = newline + 1;
        }
    }

    document.setHasFinalLineEnding(!source.empty() && source.back() == '\n');
    return document;
}

}
