#include "nec/NecCardFieldEditor.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <functional>
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

}
