#include "analysis/SolverInput.h"

#include "nec/NecParser.h"
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

auto prepareSolverInput(std::string_view source, const model::ModelSetup& setup,
    RadiationSweepMode mode) -> std::string
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
    std::vector<int> patternIndexes;
    if (mode == RadiationSweepMode::EveryFrequency) {
        patternIndexes.reserve(static_cast<std::size_t>(setup.frequency->count));
        for (auto index = 0; index < setup.frequency->count; ++index)
            patternIndexes.push_back(index);
    } else if (mode == RadiationSweepMode::RepresentativeFrequencies) {
        patternIndexes = {setup.frequency->count - 1, 0,
            (setup.frequency->count - 1) / 2};
        std::ranges::sort(patternIndexes);
        const auto duplicates = std::ranges::unique(patternIndexes);
        patternIndexes.erase(duplicates.begin(), duplicates.end());
    } else {
        patternIndexes = {(setup.frequency->count - 1) / 2};
    }

    std::vector<std::string> requests;
    requests.reserve(static_cast<std::size_t>(setup.frequency->count) * 2);
    if (mode != RadiationSweepMode::EveryFrequency && setup.executionRequest) {
        requests.push_back(writer.writeFrequencyCard(*setup.frequency));
        requests.push_back(writer.writeExecutionCard(*setup.executionRequest));
    }
    for (const auto index : patternIndexes) {
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

auto prepareImpedanceInput(std::string_view source) -> std::string
{
    const auto document = nec::NecParser{}.parse(source);
    const auto hasExecution = std::ranges::any_of(document.cards(), [](const auto& card) {
        return card.kind == nec::NecCardKind::Execute;
    });
    std::vector<std::string> lines;
    lines.reserve(document.cards().size() + 1);
    auto insertedExecution = false;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::RadiationPattern) continue;
        if (!hasExecution && !insertedExecution && card.kind == nec::NecCardKind::End) {
            lines.emplace_back("XQ 0");
            insertedExecution = true;
        }
        lines.push_back(card.sourceText);
    }
    if (!hasExecution && !insertedExecution) lines.emplace_back("XQ 0");

    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) output << document.lineEnding();
        output << lines[index];
    }
    if (document.hasFinalLineEnding() && !lines.empty()) output << document.lineEnding();
    return output.str();
}

auto prepareExplicitFrequencyInput(std::string_view source,
    std::span<const double> frequenciesMHz) -> std::string
{
    std::vector<double> frequencies;
    frequencies.reserve(frequenciesMHz.size());
    for (const auto frequencyMHz : frequenciesMHz) {
        if (std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            frequencies.push_back(frequencyMHz);
    }
    std::ranges::sort(frequencies);
    const auto duplicates = std::ranges::unique(frequencies);
    frequencies.erase(duplicates.begin(), duplicates.end());
    if (frequencies.empty()) return prepareImpedanceInput(source);

    const auto document = nec::NecParser{}.parse(source);
    const nec::NecWriter writer;
    std::vector<std::string> requests;
    requests.reserve(frequencies.size() * 2);
    for (const auto frequencyMHz : frequencies) {
        model::FrequencyDefinition frequency;
        frequency.startMHz = frequencyMHz;
        requests.push_back(writer.writeFrequencyCard(frequency));
        requests.emplace_back("XQ 0");
    }

    std::vector<std::string> lines;
    lines.reserve(document.cards().size() + requests.size());
    auto insertedRequests = false;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Frequency
            || card.kind == nec::NecCardKind::RadiationPattern
            || card.kind == nec::NecCardKind::Execute) {
            continue;
        }
        if (!insertedRequests && card.kind == nec::NecCardKind::End) {
            lines.insert(lines.end(), requests.begin(), requests.end());
            insertedRequests = true;
        }
        lines.push_back(card.sourceText);
    }
    if (!insertedRequests) lines.insert(lines.end(), requests.begin(), requests.end());

    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) output << document.lineEnding();
        output << lines[index];
    }
    if (document.hasFinalLineEnding() && !lines.empty()) output << document.lineEnding();
    return output.str();
}

}
