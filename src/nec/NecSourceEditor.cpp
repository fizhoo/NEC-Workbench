#include "nec/NecSourceEditor.h"

#include <vector>

namespace necwb::nec {
namespace {

auto splitLines(std::string_view source) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::size_t start{};
    while (start <= source.size()) {
        const auto end = source.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(source.substr(start));
            break;
        }
        lines.emplace_back(source.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

auto joinLines(const std::vector<std::string>& lines) -> std::string
{
    std::string source;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) source.push_back('\n');
        source += lines[index];
    }
    return source;
}

}

auto sourceLineCount(std::string_view source) -> std::size_t
{
    return splitLines(source).size();
}

auto replaceSourceLine(std::string_view source, std::size_t lineNumber,
    std::string_view replacement) -> std::optional<std::string>
{
    auto lines = splitLines(source);
    if (lineNumber == 0 || lineNumber > lines.size()) return std::nullopt;
    lines[lineNumber - 1] = replacement;
    return joinLines(lines);
}

auto removeSourceLine(std::string_view source, std::size_t lineNumber)
    -> std::optional<std::string>
{
    auto lines = splitLines(source);
    if (lineNumber == 0 || lineNumber > lines.size()) return std::nullopt;
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(lineNumber - 1));
    return joinLines(lines);
}

auto insertSourceLine(std::string_view source, std::size_t zeroBasedIndex,
    std::string_view line) -> std::optional<std::string>
{
    auto lines = splitLines(source);
    if (zeroBasedIndex > lines.size()) return std::nullopt;
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(zeroBasedIndex),
        std::string(line));
    return joinLines(lines);
}

}
