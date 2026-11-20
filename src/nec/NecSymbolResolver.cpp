#include "nec/NecSymbolResolver.h"

#include "nec/NecCardCatalog.h"
#include "nec/NecCardFieldEditor.h"
#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/SourcePositions.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace necwb::nec {
namespace {

auto trim(std::string_view text) -> std::string_view
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

auto lower(std::string_view text) -> std::string
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

auto validName(std::string_view name) -> bool
{
    if (name.empty() || (!std::isalpha(static_cast<unsigned char>(name.front())) && name.front() != '_'))
        return false;
    return std::ranges::all_of(name.substr(1), [](unsigned char character) {
        return std::isalnum(character) || character == '_';
    });
}

class ExpressionParser final {
public:
    ExpressionParser(std::string_view expression,
        const std::unordered_map<std::string, double>& symbols)
        : expression_(expression), symbols_(symbols)
    {
    }

    auto parse() -> double
    {
        const auto value = parseAddSubtract();
        skipSpace();
        if (position_ != expression_.size()) fail("unexpected character");
        if (!std::isfinite(value)) fail("expression produced a non-finite value");
        return value;
    }

private:
    auto parseAddSubtract() -> double
    {
        auto value = parseMultiplyDivide();
        while (true) {
            skipSpace();
            if (consume('+')) value += parseMultiplyDivide();
            else if (consume('-')) value -= parseMultiplyDivide();
            else return value;
        }
    }

    auto parseMultiplyDivide() -> double
    {
        auto value = parseUnary();
        while (true) {
            skipSpace();
            if (consume('*')) {
                value *= parseUnary();
            } else if (consume('/')) {
                const auto divisor = parseUnary();
                if (divisor == 0.0) fail("division by zero");
                value /= divisor;
            } else {
                return value;
            }
        }
    }

    auto parseUnary() -> double
    {
        skipSpace();
        if (consume('+')) return parseUnary();
        if (consume('-')) return -parseUnary();
        return parsePower();
    }

    auto parsePower() -> double
    {
        auto value = parsePrimary();
        skipSpace();
        if (consume('^')) value = std::pow(value, parseUnary());
        return value;
    }

    auto parsePrimary() -> double
    {
        skipSpace();
        if (consume('(')) {
            const auto value = parseAddSubtract();
            skipSpace();
            if (!consume(')')) fail("missing closing parenthesis");
            return value;
        }
        if (position_ >= expression_.size()) fail("expected a number, symbol, or parenthesis");
        const auto first = static_cast<unsigned char>(expression_[position_]);
        if (std::isalpha(first) || expression_[position_] == '_') {
            const auto start = position_++;
            while (position_ < expression_.size()) {
                const auto character = static_cast<unsigned char>(expression_[position_]);
                if (!std::isalnum(character) && expression_[position_] != '_') break;
                ++position_;
            }
            const auto name = lower(expression_.substr(start, position_ - start));
            const auto found = symbols_.find(name);
            if (found == symbols_.end()) fail("unknown symbol " + std::string(expression_.substr(start, position_ - start)));
            return found->second;
        }
        double value{};
        const auto* begin = expression_.data() + position_;
        const auto* end = expression_.data() + expression_.size();
        const auto parsed = std::from_chars(begin, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) fail("expected a numeric value");
        position_ = static_cast<std::size_t>(parsed.ptr - expression_.data());
        return value;
    }

    void skipSpace()
    {
        while (position_ < expression_.size()
            && std::isspace(static_cast<unsigned char>(expression_[position_]))) ++position_;
    }

    auto consume(char character) -> bool
    {
        if (position_ >= expression_.size() || expression_[position_] != character) return false;
        ++position_;
        return true;
    }

    [[noreturn]] void fail(std::string message) const
    {
        throw std::runtime_error(std::move(message));
    }

    std::string_view expression_;
    const std::unordered_map<std::string, double>& symbols_;
    std::size_t position_{};
};

auto formatValue(double value) -> std::string
{
    if (value == 0.0) value = 0.0;
    std::ostringstream output;
    output << std::setprecision(15) << value;
    return output.str();
}

auto needsEvaluation(std::string_view token) -> bool
{
    return std::ranges::any_of(token, [](unsigned char character) {
        return std::isalpha(character) || character == '_'
            || character == '(' || character == ')' || character == '+'
            || character == '-' || character == '*' || character == '/'
            || character == '^';
    });
}

// A value that is a whole number but for rounding error in its arithmetic, as an integer.
auto wholeNumber(double value) -> std::optional<long long>
{
    const auto nearest = std::nearbyint(value);
    if (std::abs(value - nearest) > 1e-9 * std::max(1.0, std::abs(nearest))) return std::nullopt;
    return static_cast<long long>(nearest);
}

// The card's fields, with each expression replaced by its value. The first
// `integerFieldCount` fields are NEC integers (tags, segments, counts, type codes), so an
// expression there must come to a whole number. A ' comment is copied as written.
auto resolveCardLine(std::string_view fullLine, std::size_t lineNumber,
    const std::unordered_map<std::string, double>& symbols, std::size_t integerFieldCount,
    std::vector<SymbolDiagnostic>& diagnostics) -> std::string
{
    const auto comment = std::min(necCommentStart(fullLine), fullLine.size());
    const auto line = fullLine.substr(0, comment);
    std::string result;
    result.reserve(fullLine.size());
    auto position = std::size_t{};
    while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
        result.push_back(line[position++]);
    const auto mnemonicStart = position;
    while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position])))
        result.push_back(line[position++]);
    const auto mnemonic = line.substr(mnemonicStart, position - mnemonicStart);
    auto fieldIndex = std::size_t{};
    while (position < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[position]))
            || line[position] == ',') {
            result.push_back(line[position++]);
            continue;
        }
        const auto start = position;
        while (position < line.size()
            && !std::isspace(static_cast<unsigned char>(line[position]))
            && line[position] != ',') {
            ++position;
        }
        const auto token = line.substr(start, position - start);
        const auto field = fieldIndex++;
        if (!needsEvaluation(token)) {
            result.append(token);
            continue;
        }
        try {
            const auto value = ExpressionParser(token, symbols).parse();
            if (field >= integerFieldCount) {
                result += formatValue(value);
                continue;
            }
            const auto whole = wholeNumber(value);
            const auto name = std::string(mnemonic) + " field " + std::to_string(field + 1);
            const auto described = name + ", " + std::string(token) + " = " + formatValue(value) + ",";
            if (!whole) {
                diagnostics.push_back({lineNumber, described + " must be a whole number: it is a "
                    "tag, segment, count or type code", true, name});
                result.append(token);
            } else if (*whole < std::numeric_limits<int>::min()
                || *whole > std::numeric_limits<int>::max()) {
                diagnostics.push_back({lineNumber, described
                    + " is outside the range of a NEC integer field", true, name});
                result.append(token);
            } else {
                result += std::to_string(*whole);
            }
        } catch (const std::runtime_error& error) {
            diagnostics.push_back({lineNumber,
                "Cannot resolve field '" + std::string(token) + "': " + error.what()});
            result.append(token);
        }
    }
    result.append(fullLine.substr(comment));
    return result;
}

auto generatedCardLine(const NecCard& card, std::string resolvedLine) -> std::string
{
    if (card.kind != NecCardKind::Frequency) return resolvedLine;
    const auto resolvedDocument = NecParser{}.parse(resolvedLine);
    if (resolvedDocument.cards().empty()) return resolvedLine;
    const auto& resolvedCard = resolvedDocument.cards().front();
    if (resolvedCard.fields.size() != 5) return resolvedLine;
    int frequencyCount{};
    const auto& countField = resolvedCard.fields[1];
    const auto parsed = std::from_chars(countField.data(),
        countField.data() + countField.size(), frequencyCount);
    if (parsed.ec == std::errc{} && parsed.ptr == countField.data() + countField.size()
        && frequencyCount == 1)
        resolvedLine += " 0";
    return resolvedLine;
}

void readDefinitions(const NecCard& card, std::unordered_map<std::string, double>& symbols,
    const std::unordered_map<std::string, double>& overrides, SymbolResolution& resolution)
{
    const auto text = std::string_view(card.sourceText).substr(0, necCommentStart(card.sourceText));
    const auto firstSpace = text.find_first_of(" \t");
    auto body = firstSpace == std::string::npos ? std::string_view{} : text.substr(firstSpace + 1);
    while (!body.empty()) {
        const auto comma = body.find(',');
        const auto assignment = trim(body.substr(0, comma));
        body = comma == std::string_view::npos ? std::string_view{} : body.substr(comma + 1);
        if (assignment.empty()) continue;
        const auto equals = assignment.find('=');
        if (equals == std::string_view::npos) {
            resolution.diagnostics.push_back({card.lineNumber,
                "SY assignment requires name=expression: " + std::string(assignment)});
            continue;
        }
        const auto name = trim(assignment.substr(0, equals));
        const auto expression = trim(assignment.substr(equals + 1));
        if (!validName(name)) {
            resolution.diagnostics.push_back({card.lineNumber,
                "Invalid symbol name: " + std::string(name)});
            continue;
        }
        const auto key = lower(name);
        if (symbols.contains(key)) {
            resolution.diagnostics.push_back({card.lineNumber,
                "Duplicate symbol: " + std::string(name)});
            continue;
        }
        if (expression.empty()) {
            resolution.diagnostics.push_back({card.lineNumber,
                "Symbol " + std::string(name) + " requires an expression"});
            continue;
        }
        try {
            const auto overridden = overrides.find(key);
            const auto value = overridden == overrides.end()
                ? ExpressionParser(expression, symbols).parse() : overridden->second;
            if (!std::isfinite(value)) throw std::runtime_error("override is not finite");
            symbols.emplace(key, value);
            resolution.definitions.push_back({std::string(name), std::string(expression), value,
                card.lineNumber});
        } catch (const std::runtime_error& error) {
            resolution.diagnostics.push_back({card.lineNumber,
                "Cannot define symbol " + std::string(name) + ": " + error.what()});
        }
    }
}

}

auto SymbolResolution::ok() const noexcept -> bool
{
    return diagnostics.empty();
}

auto NecSymbolResolver::resolve(std::string_view source,
    const std::unordered_map<std::string, double>& overrides) const -> SymbolResolution
{
    SymbolResolution resolution;
    const auto document = NecParser{}.parse(source);
    std::unordered_map<std::string, double> symbols;
    std::unordered_map<std::string, double> normalizedOverrides;
    for (const auto& [name, value] : overrides) normalizedOverrides.insert_or_assign(lower(name), value);
    // Each card's resolved line; an SY card's is empty.
    std::vector<std::string> lines;
    lines.reserve(document.cards().size());
    auto marked = false;
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Symbol) {
            readDefinitions(card, symbols, normalizedOverrides, resolution);
            lines.emplace_back();
            continue;
        }
        const auto* spec = findNecCardSpec(card.mnemonic);
        lines.push_back(card.kind == NecCardKind::Comment || card.kind == NecCardKind::Blank
                || card.kind == NecCardKind::Unknown
            ? card.sourceText
            : resolveCardLine(card.sourceText, card.lineNumber, symbols,
                spec == nullptr ? 0 : spec->integerFieldCount, resolution.diagnostics));
        marked = marked || (card.kind == NecCardKind::Excitation
            && sourcePositionMark(card.sourceText).has_value());
    }
    // A source placed along its wire takes the segment there in the resolved geometry.
    if (marked && resolution.ok()) {
        std::string joined;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (index != 0) joined += '\n';
            joined += lines[index];
        }
        const auto resolved = NecParser{}.parse(joined);
        for (const auto& placed : placeMarkedSources(resolved, NecModelConverter{}.convert(resolved).model)) {
            if (!placed.placement) continue;
            auto& line = lines[placed.lineNumber - 1];
            if (auto updated = replaceNecCardFields(line,
                    std::array{NecFieldReplacement{2, std::to_string(placed.placement->segment)}}))
                line = std::move(*updated);
        }
    }
    auto wroteResolvedLine = false;
    auto wroteGeneratedLine = false;
    const auto appendLine = [&document](std::string& target, bool& wroteLine,
        std::string_view line) {
        if (wroteLine) target += document.lineEnding();
        target += line;
        wroteLine = true;
    };
    for (const auto& card : document.cards()) {
        const auto& line = lines[card.lineNumber - 1];
        appendLine(resolution.resolvedSource, wroteResolvedLine, line);
        if (card.kind != NecCardKind::Symbol)
            appendLine(resolution.generatedDeck, wroteGeneratedLine, generatedCardLine(card, line));
    }
    if (document.hasFinalLineEnding() && wroteResolvedLine)
        resolution.resolvedSource += document.lineEnding();
    if (document.hasFinalLineEnding() && wroteGeneratedLine)
        resolution.generatedDeck += document.lineEnding();
    return resolution;
}

auto wholeNumberFieldAt(std::string_view source, std::string_view name, double value)
    -> std::optional<SymbolDiagnostic>
{
    const auto resolution = NecSymbolResolver{}.resolve(source, {{std::string(name), value}});
    const auto found = std::ranges::find_if(resolution.diagnostics, &SymbolDiagnostic::wholeNumberField);
    return found == resolution.diagnostics.end() ? std::nullopt : std::optional{*found};
}

auto wholeNumberFieldReached(std::string_view source, const SymbolDefinition& definition)
    -> std::optional<SymbolDiagnostic>
{
    const auto scale = std::max(1.0, std::abs(definition.value));
    for (const auto step : {0.318309886, 0.707106781}) {
        if (auto found = wholeNumberFieldAt(source, definition.name, definition.value + step * scale))
            return found;
    }
    return std::nullopt;
}

auto undefinedSymbols(std::string_view expression,
    const std::vector<SymbolDefinition>& definitions) -> std::vector<std::string>
{
    std::vector<std::string> names;
    const auto known = [&definitions, &names](const std::string& name) {
        return std::ranges::any_of(definitions, [&name](const auto& definition) {
                return lower(definition.name) == lower(name); })
            || std::ranges::any_of(names, [&name](const auto& listed) { return lower(listed) == lower(name); });
    };
    for (std::size_t position = 0; position < expression.size();) {
        const auto character = static_cast<unsigned char>(expression[position]);
        if (std::isalpha(character) || character == '_') {
            auto end = position + 1;
            while (end < expression.size() && (std::isalnum(static_cast<unsigned char>(expression[end]))
                       || expression[end] == '_')) ++end;
            auto name = std::string(expression.substr(position, end - position));
            if (!known(name)) names.push_back(std::move(name));
            position = end;
        } else if (std::isdigit(character) || character == '.') {
            // A number, with any exponent: the e of 1e3 is not a name.
            double value{};
            const auto parsed = std::from_chars(expression.data() + position,
                expression.data() + expression.size(), value);
            position = parsed.ec == std::errc{} ? static_cast<std::size_t>(parsed.ptr - expression.data())
                                                : position + 1;
        } else {
            ++position;
        }
    }
    return names;
}

auto symbolValueKeeping(std::string_view expression, std::string_view name,
    const std::vector<SymbolDefinition>& definitions, double target) -> std::optional<double>
{
    std::unordered_map<std::string, double> symbols;
    for (const auto& definition : definitions) symbols.insert_or_assign(lower(definition.name), definition.value);
    const auto at = [&](double value) -> std::optional<double> {
        symbols.insert_or_assign(lower(name), value);
        try {
            return ExpressionParser{expression, symbols}.parse();
        } catch (const std::runtime_error&) {
            return std::nullopt;
        }
    };
    const auto zero = at(0.0);
    const auto one = at(1.0);
    const auto two = at(2.0);
    if (!zero || !one || !two) return std::nullopt;
    const auto slope = *one - *zero;
    const auto scale = std::max({1.0, std::abs(*zero), std::abs(*one), std::abs(*two)});
    if (slope == 0.0 || std::abs(*two - 2.0 * *one + *zero) > 1e-9 * scale) return std::nullopt;
    return (target - *zero) / slope;
}

}
