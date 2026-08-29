#include "analysis/SolverInput.h"

#include "nec/NecWriter.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace necwb::analysis {
namespace {

auto frequencyAt(const model::FrequencyDefinition& frequency, int index) -> double
{
    if (frequency.steppingMode == 1) {
        return frequency.startMHz * std::pow(frequency.step, index);
    }
    return frequency.startMHz + index * frequency.step;
}

auto splitLines(std::string_view source) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::istringstream input{std::string(source)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (!source.empty() && source.back() == '\n') lines.emplace_back();
    return lines;
}

}

auto prepareSolverInput(std::string_view source, const model::ModelSetup& setup) -> std::string
{
    if (!setup.frequency || !setup.radiationPattern || setup.frequency->count <= 1) {
        return std::string(source);
    }

    auto lines = splitLines(source);
    const auto frequencyLine = setup.frequency->sourceLine;
    const auto patternLine = setup.radiationPattern->sourceLine;
    const auto executionLine = setup.executionRequest
        ? setup.executionRequest->sourceLine : std::size_t{};
    std::vector<std::string> retained;
    retained.reserve(lines.size());
    auto insertionIndex = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto sourceLine = index + 1;
        if (sourceLine == frequencyLine) {
            continue;
        }
        if (sourceLine == patternLine || (executionLine != 0 && sourceLine == executionLine)) {
            insertionIndex = std::min(insertionIndex, retained.size());
            continue;
        }
        retained.push_back(lines[index]);
    }

    insertionIndex = std::min(insertionIndex, retained.size());
    const nec::NecWriter writer;
    std::vector<std::string> requests;
    requests.reserve(static_cast<std::size_t>(setup.frequency->count) * 2);
    for (auto index = 0; index < setup.frequency->count; ++index) {
        auto point = *setup.frequency;
        point.steppingMode = 0;
        point.count = 1;
        point.startMHz = frequencyAt(*setup.frequency, index);
        point.step = 0.0;
        requests.push_back(writer.writeFrequencyCard(point));
        requests.push_back(writer.writeRadiationPatternCard(*setup.radiationPattern));
    }
    retained.insert(retained.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
        requests.begin(), requests.end());

    std::ostringstream output;
    for (std::size_t index = 0; index < retained.size(); ++index) {
        if (index != 0) output << '\n';
        output << retained[index];
    }
    return output.str();
}

}
