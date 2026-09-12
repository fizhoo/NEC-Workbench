#include "nec/NecSymbolEditor.h"

#include "nec/NecCardFieldEditor.h"
#include "nec/NecParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace necwb::nec {
namespace {

auto trim(std::string_view value) -> std::string_view
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

auto lower(std::string_view value) -> std::string
{
    auto result = std::string(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

auto assignments(std::string_view line) -> std::vector<std::string>
{
    const auto firstSpace = line.find_first_of(" \t");
    auto body = firstSpace == std::string_view::npos
        ? std::string_view{} : line.substr(firstSpace + 1);
    std::vector<std::string> result;
    while (!body.empty()) {
        const auto comma = body.find(',');
        const auto assignment = trim(body.substr(0, comma));
        if (!assignment.empty()) result.emplace_back(assignment);
        body = comma == std::string_view::npos ? std::string_view{} : body.substr(comma + 1);
    }
    return result;
}

auto assignmentName(std::string_view assignment) -> std::string_view
{
    const auto equals = assignment.find('=');
    return equals == std::string_view::npos ? std::string_view{} : trim(assignment.substr(0, equals));
}

auto sourceLines(const NecDocument& document) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.reserve(document.cards().size());
    for (const auto& card : document.cards()) lines.push_back(card.sourceText);
    return lines;
}

auto joinSource(const std::vector<std::string>& lines, const NecDocument& document) -> std::string
{
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) result += document.lineEnding();
        result += lines[index];
    }
    if (document.hasFinalLineEnding() && !lines.empty()) result += document.lineEnding();
    return result;
}

auto symbolLine(std::string_view name, std::string_view expression) -> std::string
{
    return "SY " + std::string(trim(name)) + '=' + std::string(trim(expression));
}

}

auto insertSymbolDefinition(std::string_view source,
    std::string_view name, std::string_view expression) -> std::string
{
    const auto document = NecParser{}.parse(source);
    auto lines = sourceLines(document);
    auto insertion = lines.size();
    auto foundSymbol = false;
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Symbol) {
            insertion = card.lineNumber;
            foundSymbol = true;
            continue;
        }
        if (!foundSymbol && card.kind != NecCardKind::Comment && card.kind != NecCardKind::Blank) {
            insertion = card.lineNumber - 1;
            break;
        }
        if (foundSymbol) break;
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertion),
        symbolLine(name, expression));
    return joinSource(lines, document);
}

auto replaceSymbolDefinition(std::string_view source,
    std::size_t lineNumber, std::string_view originalName,
    std::string_view name, std::string_view expression) -> std::optional<std::string>
{
    const auto document = NecParser{}.parse(source);
    if (lineNumber == 0 || lineNumber > document.cards().size()) return std::nullopt;
    const auto& card = document.cards()[lineNumber - 1];
    if (card.kind != NecCardKind::Symbol) return std::nullopt;
    auto values = assignments(card.sourceText);
    const auto target = lower(trim(originalName));
    const auto found = std::ranges::find_if(values, [&target](const auto& assignment) {
        return lower(assignmentName(assignment)) == target;
    });
    if (found == values.end()) return std::nullopt;
    *found = std::string(trim(name)) + '=' + std::string(trim(expression));
    auto lines = sourceLines(document);
    lines[lineNumber - 1] = "SY ";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) lines[lineNumber - 1] += ", ";
        lines[lineNumber - 1] += values[index];
    }
    return joinSource(lines, document);
}

auto removeSymbolDefinition(std::string_view source,
    std::size_t lineNumber, std::string_view name) -> std::optional<std::string>
{
    const auto document = NecParser{}.parse(source);
    if (lineNumber == 0 || lineNumber > document.cards().size()) return std::nullopt;
    const auto& card = document.cards()[lineNumber - 1];
    if (card.kind != NecCardKind::Symbol) return std::nullopt;
    auto values = assignments(card.sourceText);
    const auto target = lower(trim(name));
    const auto found = std::ranges::find_if(values, [&target](const auto& assignment) {
        return lower(assignmentName(assignment)) == target;
    });
    if (found == values.end()) return std::nullopt;
    values.erase(found);
    auto lines = sourceLines(document);
    if (values.empty()) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(lineNumber - 1));
    } else {
        lines[lineNumber - 1] = "SY ";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0) lines[lineNumber - 1] += ", ";
            lines[lineNumber - 1] += values[index];
        }
    }
    return joinSource(lines, document);
}

auto parameterizeNecCardField(std::string_view source, std::size_t lineNumber,
    std::size_t fieldIndex, std::string_view name) -> std::optional<std::string>
{
    const auto document = NecParser{}.parse(source);
    if (lineNumber == 0 || lineNumber > document.cards().size()) return std::nullopt;
    const auto& card = document.cards()[lineNumber - 1];
    if (fieldIndex >= card.fields.size()
        || !necCardFieldIsNumeric(card.sourceText, fieldIndex)) return std::nullopt;
    const auto replacement = replaceNecCardFields(card.sourceText,
        std::array{NecFieldReplacement{fieldIndex, std::string(trim(name))}});
    if (!replacement) return std::nullopt;
    auto lines = sourceLines(document);
    lines[lineNumber - 1] = *replacement;
    return insertSymbolDefinition(joinSource(lines, document), name, card.fields[fieldIndex]);
}

}
