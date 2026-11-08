#include "nec/NecSymbolEditor.h"

#include "nec/NecCardFieldEditor.h"
#include "nec/NecParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
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

auto identifierCharacter(char character) -> bool
{
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

auto isOperator(char character) -> bool
{
    return character == '+' || character == '-' || character == '*'
        || character == '/' || character == '^';
}

// Rewrites whole-word uses of target (lower case) after the card mnemonic. Runs that
// start with a digit or '.' are numbers, so exponents such as 1e5 are never matched.
auto substituteSymbol(std::string_view line, std::string_view target,
    std::string_view replacement, std::size_t& count) -> std::string
{
    std::string result;
    auto position = line.find_first_not_of(" \t");
    position = position == std::string_view::npos ? line.size() : line.find_first_of(" \t", position);
    if (position == std::string_view::npos) return std::string(line);
    result.append(line.substr(0, position));
    while (position < line.size()) {
        if (!identifierCharacter(line[position]) && line[position] != '.') {
            result.push_back(line[position++]);
            continue;
        }
        const auto start = position;
        while (position < line.size()
            && (identifierCharacter(line[position]) || line[position] == '.')) ++position;
        const auto run = line.substr(start, position - start);
        const auto first = static_cast<unsigned char>(run.front());
        if ((!std::isalpha(first) && run.front() != '_') || lower(run) != target) {
            result.append(run);
            continue;
        }
        ++count;
        if (!replacement.starts_with('-')) {
            result.append(replacement);
            continue;
        }
        const auto before = result.find_last_not_of(" \t");
        const auto after = line.find_first_not_of(" \t", position);
        const auto bracket = (before != std::string::npos && isOperator(result[before]))
            || (after != std::string_view::npos && line[after] == '^');
        if (bracket) result += '(';
        result.append(replacement);
        if (bracket) result += ')';
    }
    return result;
}

auto substituteSymbolInSource(std::string_view source, std::string_view name,
    std::string_view replacement, std::size_t& count) -> std::string
{
    const auto document = NecParser{}.parse(source);
    auto lines = sourceLines(document);
    const auto target = lower(trim(name));
    for (const auto& card : document.cards()) {
        // Match NecSymbolResolver, which never resolves symbols on these lines.
        if (card.kind == NecCardKind::Comment || card.kind == NecCardKind::Blank
            || card.kind == NecCardKind::Unknown) continue;
        lines[card.lineNumber - 1] = substituteSymbol(card.sourceText, target, replacement, count);
    }
    return joinSource(lines, document);
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

    auto definition = card.fields[fieldIndex];
    auto reference = std::string(trim(name));
    if (definition.starts_with('-')) {
        definition.erase(definition.begin());
        reference.insert(reference.begin(), '-');
    } else if (definition.starts_with('+')) {
        definition.erase(definition.begin());
    }
    std::ranges::replace(definition, 'D', 'E');
    std::ranges::replace(definition, 'd', 'e');
    const auto replacement = replaceNecCardFields(card.sourceText,
        std::array{NecFieldReplacement{fieldIndex, reference}});
    if (!replacement) return std::nullopt;
    auto lines = sourceLines(document);
    lines[lineNumber - 1] = *replacement;
    return insertSymbolDefinition(joinSource(lines, document), name, definition);
}

auto replaceNecCardFieldExpression(std::string_view source, std::size_t lineNumber,
    std::size_t fieldIndex, std::string_view expression) -> std::optional<std::string>
{
    const auto document = NecParser{}.parse(source);
    if (lineNumber == 0 || lineNumber > document.cards().size()) return std::nullopt;
    const auto& card = document.cards()[lineNumber - 1];
    if (fieldIndex >= card.fields.size() || trim(expression).empty()) return std::nullopt;
    const auto replacement = replaceNecCardFields(card.sourceText,
        std::array{NecFieldReplacement{fieldIndex, std::string(trim(expression))}});
    if (!replacement) return std::nullopt;
    auto lines = sourceLines(document);
    lines[lineNumber - 1] = *replacement;
    return joinSource(lines, document);
}

auto symbolReferencePreservingValue(std::string_view name,
    double symbolValue, double fieldValue) -> std::string
{
    const auto scale = std::max({1.0, std::abs(symbolValue), std::abs(fieldValue)});
    const auto close = [scale](double first, double second) {
        return std::abs(first - second) <= scale * 1.0e-12;
    };
    auto reference = std::string(trim(name));
    if (!close(fieldValue, symbolValue) && close(fieldValue, -symbolValue))
        reference.insert(reference.begin(), '-');
    return reference;
}

auto symbolReferenceCount(std::string_view source, std::string_view name) -> std::size_t
{
    std::size_t count{};
    static_cast<void>(substituteSymbolInSource(source, name, std::string(trim(name)), count));
    return count;
}

auto inlineSymbolReferences(std::string_view source, std::string_view name,
    double value) -> std::string
{
    if (value == 0.0) value = 0.0;
    std::ostringstream number;
    number << std::setprecision(15) << value;
    std::size_t count{};
    return substituteSymbolInSource(source, name, number.str(), count);
}

}
