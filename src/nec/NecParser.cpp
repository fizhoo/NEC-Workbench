#include "nec/NecParser.h"

#include "nec/NecCardCatalog.h"

#include <algorithm>
#include <cctype>
#include <string>
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
    const auto* spec = findNecCardSpec(mnemonic);
    return spec == nullptr ? NecCardKind::Unknown : spec->kind;
}

auto parseLine(std::string line, std::size_t lineNumber) -> NecCard
{
    NecCard card;
    card.sourceText = std::move(line);
    card.lineNumber = lineNumber;

    auto contentStart = std::size_t{};
    if (lineNumber == 1 && card.sourceText.starts_with("\xEF\xBB\xBF")) contentStart = 3;
    const auto first = card.sourceText.find_first_not_of(" \t", contentStart);
    if (first == std::string::npos) {
        card.kind = NecCardKind::Blank;
        return card;
    }

    const auto mnemonicLength = std::min<std::size_t>(2, card.sourceText.size() - first);
    card.mnemonic = upper(card.sourceText.substr(first, mnemonicLength));
    card.kind = classify(card.mnemonic);

    auto position = first + mnemonicLength;
    while (position < card.sourceText.size()) {
        while (position < card.sourceText.size()
            && (std::isspace(static_cast<unsigned char>(card.sourceText[position]))
                || card.sourceText[position] == ',')) {
            ++position;
        }
        const auto fieldStart = position;
        while (position < card.sourceText.size()
            && !std::isspace(static_cast<unsigned char>(card.sourceText[position]))
            && card.sourceText[position] != ',') {
            ++position;
        }
        if (position > fieldStart)
            card.fields.push_back(card.sourceText.substr(fieldStart, position - fieldStart));
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
