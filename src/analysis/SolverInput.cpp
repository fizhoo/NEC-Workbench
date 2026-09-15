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

auto joinLines(const std::vector<std::string>& lines) -> std::string
{
    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) output << '\n';
        output << lines[index];
    }
    return output.str();
}

}

auto normalizeSolverDeck(std::string_view source) -> std::string
{
    const auto document = nec::NecParser{}.parse(source);
    auto openingCommentBlock = true;
    std::ostringstream output;
    const auto cards = document.cards();
    for (std::size_t index = 0; index < cards.size(); ++index) {
        const auto& card = cards[index];
        const auto inlineComment = card.mnemonic == "CM" && !openingCommentBlock;
        const auto compatibilityMetadata = card.kind == nec::NecCardKind::ReferenceImpedance;
        if (!inlineComment && !compatibilityMetadata) output << card.sourceText;
        if (card.mnemonic == "CE"
            || (card.kind != nec::NecCardKind::Blank && card.mnemonic != "CM")) {
            openingCommentBlock = false;
        }
        if (index + 1 < cards.size() || document.hasFinalLineEnding())
            output << document.lineEnding();
    }
    return output.str();
}

auto prepareSolverInput(std::string_view source, const model::ModelSetup& setup,
    RadiationSweepMode mode) -> std::string
{
    FrequencyPlan plan;
    if (!setup.frequency || setup.frequency->count <= 1
        || mode == RadiationSweepMode::EveryFrequency) {
        plan.mode = FrequencyPlanMode::ModelSweep;
    } else {
        plan.mode = FrequencyPlanMode::Explicit;
        std::vector<int> indexes;
        if (mode == RadiationSweepMode::RepresentativeFrequencies) {
            indexes = {setup.frequency->count - 1, 0,
                (setup.frequency->count - 1) / 2};
            std::ranges::sort(indexes);
            const auto duplicates = std::ranges::unique(indexes);
            indexes.erase(duplicates.begin(), duplicates.end());
        } else {
            indexes = {(setup.frequency->count - 1) / 2};
        }
        for (const auto index : indexes)
            plan.pointsMHz.push_back(frequencyAt(*setup.frequency, index));
    }
    return prepareSolverInput(source, setup, plan);
}

auto prepareSolverInput(std::string_view source, const model::ModelSetup& setup,
    const FrequencyPlan& radiationFrequencies) -> std::string
{
    const auto compatibleSource = normalizeSolverDeck(source);
    if (setup.radiationPatterns.empty()
        || radiationFrequencies.mode == FrequencyPlanMode::ModelSweep) {
        return compatibleSource;
    }
    const auto frequencies = frequencyPlanPoints(radiationFrequencies);
    if (frequencies.empty()) return compatibleSource;

    auto lines = splitLines(compatibleSource);
    const auto frequencyLine = setup.frequency
        ? setup.frequency->sourceLine : std::size_t{};
    std::vector<std::size_t> patternLines;
    patternLines.reserve(setup.radiationPatterns.size());
    for (const auto& pattern : setup.radiationPatterns) patternLines.push_back(pattern.sourceLine);
    const auto executionLine = setup.executionRequest
        ? setup.executionRequest->sourceLine : std::size_t{};
    std::vector<std::string> retained;
    retained.reserve(lines.size());
    auto insertionIndex = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto sourceLine = index + 1;
        if (frequencyLine != 0 && sourceLine == frequencyLine) {
            continue;
        }
        if (std::ranges::find(patternLines, sourceLine) != patternLines.end()
            || (executionLine != 0 && sourceLine == executionLine)) {
            insertionIndex = std::min(insertionIndex, retained.size());
            continue;
        }
        retained.push_back(lines[index]);
    }

    insertionIndex = std::min(insertionIndex, retained.size());
    const nec::NecWriter writer;
    std::vector<std::string> requests;
    requests.reserve(frequencies.size()
        * (setup.radiationPatterns.size() + 1));
    if (setup.frequency) requests.push_back(writer.writeFrequencyCard(*setup.frequency));
    requests.push_back(setup.executionRequest
        ? writer.writeExecutionCard(*setup.executionRequest) : std::string{"XQ 0"});
    for (const auto frequencyMHz : frequencies) {
        model::FrequencyDefinition point;
        point.startMHz = frequencyMHz;
        requests.push_back(writer.writeFrequencyCard(point));
        for (const auto& pattern : setup.radiationPatterns)
            requests.push_back(writer.writeRadiationPatternCard(pattern));
    }
    retained.insert(retained.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
        requests.begin(), requests.end());

    return joinLines(retained);
}

auto prepareImpedanceInput(std::string_view source) -> std::string
{
    const auto document = nec::NecParser{}.parse(normalizeSolverDeck(source));
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

    const auto document = nec::NecParser{}.parse(normalizeSolverDeck(source));
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
