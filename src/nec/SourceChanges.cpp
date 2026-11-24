#include "nec/SourceChanges.h"

#include "nec/NecParser.h"
#include "nec/NecSymbolResolver.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <ranges>

namespace necwb::nec {
namespace {

auto lowered(std::string text) -> std::string
{
    std::ranges::transform(text, text.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

// Variables by lowercase name, as written.
auto symbols(std::string_view source, const std::vector<std::string>& ignored)
    -> std::map<std::string, SymbolDefinition>
{
    std::map<std::string, SymbolDefinition> byName;
    for (auto& definition : NecSymbolResolver{}.resolve(source).definitions) {
        auto key = lowered(definition.name);
        if (std::ranges::none_of(ignored, [&key](const auto& name) { return lowered(name) == key; }))
            byName.insert_or_assign(std::move(key), std::move(definition));
    }
    return byName;
}

auto cards(std::string_view source) -> std::vector<NecCard>
{
    std::vector<NecCard> kept;
    const auto document = NecParser{}.parse(source);
    for (const auto& card : document.cards()) {
        if (card.kind != NecCardKind::Blank && card.kind != NecCardKind::Comment
            && card.kind != NecCardKind::Symbol)
            kept.push_back(card);
    }
    return kept;
}

auto where(const NecCard& card) -> std::string
{
    return card.mnemonic + " (line " + std::to_string(card.lineNumber) + ")";
}

}

auto describeSourceChanges(std::string_view before, std::string_view after,
    const std::vector<std::string>& ignoredSymbols) -> std::vector<std::string>
{
    std::vector<std::string> changes;
    const auto oldSymbols = symbols(before, ignoredSymbols);
    const auto newSymbols = symbols(after, ignoredSymbols);
    for (const auto& [key, definition] : oldSymbols) {
        const auto found = newSymbols.find(key);
        if (found == newSymbols.end())
            changes.push_back("Variable " + definition.name + " removed");
        else if (found->second.expression != definition.expression)
            changes.push_back("Variable " + definition.name + ": " + definition.expression + " → "
                + found->second.expression);
    }
    for (const auto& [key, definition] : newSymbols) {
        if (!oldSymbols.contains(key))
            changes.push_back("Variable " + definition.name + " added (= " + definition.expression + ")");
    }

    // Cards are paired along the longest common run of mnemonics, so one card added or
    // removed reads as that, not as every later card changing.
    const auto oldCards = cards(before);
    const auto newCards = cards(after);
    const auto rows = oldCards.size();
    const auto columns = newCards.size();
    std::vector<std::size_t> common((rows + 1) * (columns + 1));
    const auto at = [columns, &common](std::size_t row, std::size_t column) -> std::size_t& {
        return common[row * (columns + 1) + column];
    };
    const auto same = [&](std::size_t row, std::size_t column) {
        return lowered(oldCards[row].mnemonic) == lowered(newCards[column].mnemonic);
    };
    for (auto row = rows; row-- > 0;)
        for (auto column = columns; column-- > 0;)
            at(row, column) = same(row, column) ? at(row + 1, column + 1) + 1
                : std::max(at(row + 1, column), at(row, column + 1));
    auto row = std::size_t{};
    auto column = std::size_t{};
    while (row < rows || column < columns) {
        if (row < rows && column < columns && same(row, column)) {
            const auto& was = oldCards[row++];
            const auto& is = newCards[column++];
            const auto fields = std::max(was.fields.size(), is.fields.size());
            for (auto field = std::size_t{}; field < fields; ++field) {
                const auto oldValue = field < was.fields.size() ? was.fields[field] : "(none)";
                const auto newValue = field < is.fields.size() ? is.fields[field] : "(none)";
                if (oldValue != newValue)
                    changes.push_back(where(is) + ", field " + std::to_string(field + 1) + ": "
                        + oldValue + " → " + newValue);
            }
        } else if (column < columns && (row == rows || at(row, column + 1) >= at(row + 1, column))) {
            changes.push_back(where(newCards[column++]) + " added");
        } else {
            changes.push_back(where(oldCards[row++]) + " removed");
        }
    }
    return changes;
}

}
