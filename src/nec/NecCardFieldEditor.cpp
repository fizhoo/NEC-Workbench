#include "nec/NecCardFieldEditor.h"

#include "nec/NecParser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <functional>
#include <unordered_set>
#include <vector>

namespace necwb::nec {
namespace {

struct FieldSpan {
    std::size_t begin{};
    std::size_t end{};
};

auto dataFields(std::string_view source) -> std::vector<FieldSpan>
{
    std::vector<FieldSpan> fields;
    auto position = std::size_t{};
    while (position < source.size()
        && std::isspace(static_cast<unsigned char>(source[position]))) ++position;
    while (position < source.size()
        && !std::isspace(static_cast<unsigned char>(source[position]))) ++position;
    while (position < source.size()) {
        while (position < source.size()
            && std::isspace(static_cast<unsigned char>(source[position]))) ++position;
        if (position == source.size()) break;
        const auto begin = position;
        while (position < source.size()
            && !std::isspace(static_cast<unsigned char>(source[position]))) ++position;
        fields.push_back({begin, position});
    }
    return fields;
}

}

auto replaceNecCardFields(std::string_view source,
    std::span<const NecFieldReplacement> replacements) -> std::optional<std::string>
{
    const auto fields = dataFields(source);
    auto ordered = std::vector<NecFieldReplacement>{replacements.begin(), replacements.end()};
    if (std::ranges::any_of(ordered, [&fields](const auto& replacement) {
            return replacement.fieldIndex >= fields.size();
        })) return std::nullopt;
    std::ranges::sort(ordered, std::greater{}, &NecFieldReplacement::fieldIndex);
    for (auto index = std::size_t{1}; index < ordered.size(); ++index)
        if (ordered[index-1].fieldIndex == ordered[index].fieldIndex) return std::nullopt;

    auto result = std::string(source);
    for (const auto& replacement : ordered) {
        const auto field = fields[replacement.fieldIndex];
        result.replace(field.begin, field.end - field.begin, replacement.value);
    }
    return result;
}

auto necCardFieldIsNumeric(std::string_view source, std::size_t fieldIndex) -> bool
{
    const auto fields = dataFields(source);
    if (fieldIndex >= fields.size()) return false;
    const auto field = source.substr(fields[fieldIndex].begin,
        fields[fieldIndex].end - fields[fieldIndex].begin);
    auto normalized = std::string(field);
    std::ranges::replace(normalized, 'D', 'E');
    std::ranges::replace(normalized, 'd', 'e');
    if (normalized.starts_with('+')) normalized.erase(normalized.begin());
    double value{};
    const auto parsed = std::from_chars(
        normalized.data(), normalized.data() + normalized.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == normalized.data() + normalized.size();
}

auto necCardFieldsReferencingSymbols(std::string_view source,
    std::span<const std::string> symbolNames) -> NecParameterFieldMap
{
    std::unordered_set<std::string> names;
    for (const auto& name : symbolNames) {
        auto normalized = name;
        std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        names.insert(std::move(normalized));
    }
    NecParameterFieldMap result;
    const auto document = NecParser{}.parse(source);
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Symbol) continue;
        for (std::size_t fieldIndex = 0; fieldIndex < card.fields.size(); ++fieldIndex) {
            const auto& expression = card.fields[fieldIndex];
            auto position = std::size_t{};
            while (position < expression.size()) {
                const auto first = static_cast<unsigned char>(expression[position]);
                if (!std::isalpha(first) && expression[position] != '_') {
                    ++position;
                    continue;
                }
                const auto begin = position++;
                while (position < expression.size()) {
                    const auto character = static_cast<unsigned char>(expression[position]);
                    if (!std::isalnum(character) && expression[position] != '_') break;
                    ++position;
                }
                auto identifier = expression.substr(begin, position-begin);
                std::ranges::transform(identifier, identifier.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                if (names.contains(identifier)) {
                    result[card.lineNumber][fieldIndex] = expression;
                    break;
                }
            }
        }
    }
    return result;
}

}
