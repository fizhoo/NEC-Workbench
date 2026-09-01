#include "nec/NecSymbolResolver.h"

#include "nec/NecParser.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <iomanip>
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

auto isNumericLiteral(std::string_view expression) -> bool
{
    expression = trim(expression);
    if (!expression.empty() && expression.front() == '+') expression.remove_prefix(1);
    if (expression.empty()) return false;
    double value{};
    const auto [end, error] = std::from_chars(
        expression.data(), expression.data() + expression.size(), value);
    return error == std::errc{} && end == expression.data() + expression.size()
        && std::isfinite(value);
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

auto resolveCardLine(std::string_view line, std::size_t lineNumber,
    const std::unordered_map<std::string, double>& symbols,
    std::vector<SymbolDiagnostic>& diagnostics) -> std::string
{
    std::string result;
    result.reserve(line.size());
    auto position = std::size_t{};
    while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
        result.push_back(line[position++]);
    while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position])))
        result.push_back(line[position++]);
    while (position < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[position]))) {
            result.push_back(line[position++]);
            continue;
        }
        const auto start = position;
        while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position]))) ++position;
        const auto token = line.substr(start, position - start);
        if (!needsEvaluation(token)) {
            result.append(token);
            continue;
        }
        try {
            result += formatValue(ExpressionParser(token, symbols).parse());
        } catch (const std::runtime_error& error) {
            diagnostics.push_back({lineNumber,
                "Cannot resolve field '" + std::string(token) + "': " + error.what()});
            result.append(token);
        }
    }
    return result;
}

void readDefinitions(const NecCard& card, std::unordered_map<std::string, double>& symbols,
    const std::unordered_map<std::string, double>& overrides, SymbolResolution& resolution)
{
    const auto firstSpace = card.sourceText.find_first_of(" \t");
    auto body = firstSpace == std::string::npos
        ? std::string_view{} : std::string_view(card.sourceText).substr(firstSpace + 1);
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
                card.lineNumber, isNumericLiteral(expression)});
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
    auto wroteResolvedLine = false;
    auto wroteGeneratedLine = false;
    const auto appendLine = [&document](std::string& target, bool& wroteLine,
        std::string_view line) {
        if (wroteLine) target += document.lineEnding();
        target += line;
        wroteLine = true;
    };
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Symbol) {
            readDefinitions(card, symbols, normalizedOverrides, resolution);
            appendLine(resolution.resolvedSource, wroteResolvedLine, {});
            continue;
        }
        const auto line = card.kind == NecCardKind::Comment || card.kind == NecCardKind::Blank
                || card.kind == NecCardKind::Unknown
            ? card.sourceText
            : resolveCardLine(card.sourceText, card.lineNumber, symbols, resolution.diagnostics);
        appendLine(resolution.resolvedSource, wroteResolvedLine, line);
        appendLine(resolution.generatedDeck, wroteGeneratedLine, line);
    }
    if (document.hasFinalLineEnding() && wroteResolvedLine)
        resolution.resolvedSource += document.lineEnding();
    if (document.hasFinalLineEnding() && wroteGeneratedLine)
        resolution.generatedDeck += document.lineEnding();
    return resolution;
}

}
