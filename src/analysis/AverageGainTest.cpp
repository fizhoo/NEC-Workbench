#include "analysis/AverageGainTest.h"

#include "nec/NecParser.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <vector>

namespace necwb::analysis {
namespace {

auto cardText(const std::string& mnemonic, const std::vector<std::string>& fields) -> std::string
{
    std::ostringstream output;
    output << mnemonic;
    for (const auto& field : fields) output << ' ' << field;
    return output.str();
}

auto losslessCard(const nec::NecCard& card) -> std::string
{
    auto fields = card.fields;
    if (card.kind == nec::NecCardKind::Load && fields.size() >= 7) {
        int type{};
        const auto [end, error] = std::from_chars(
            fields[0].data(), fields[0].data() + fields[0].size(), type);
        if (error == std::errc{} && end == fields[0].data() + fields[0].size()) {
            if (type == 5) return {};
            fields[4] = "0";
        }
    } else if (card.kind == nec::NecCardKind::TransmissionLine && fields.size() >= 10) {
        fields[6] = "0";
        fields[8] = "0";
    } else if (card.kind == nec::NecCardKind::Network && fields.size() >= 10) {
        fields[4] = "0";
        fields[6] = "0";
        fields[8] = "0";
    }
    return cardText(card.mnemonic, fields);
}

}

auto prepareAverageGainTestInput(std::string_view source, double frequencyMHz,
    AverageGainEnvironment environment) -> std::string
{
    const auto document = nec::NecParser{}.parse(source);
    std::vector<std::string> lines;
    lines.reserve(document.cards().size() + 4);
    auto insertedRequests = false;
    const auto insertRequests = [&] {
        if (insertedRequests) return;
        std::ostringstream frequency;
        frequency << std::setprecision(15) << "FR 0 1 0 0 " << frequencyMHz << " 0";
        lines.push_back(frequency.str());
        lines.push_back(environment == AverageGainEnvironment::PerfectGround
                ? "RP 0 91 361 1002 0 0 1 1"
                : "RP 0 181 361 1002 0 0 1 1");
        insertedRequests = true;
    };

    auto groundInserted = false;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Frequency
            || card.kind == nec::NecCardKind::RadiationPattern
            || card.kind == nec::NecCardKind::Execute
            || card.kind == nec::NecCardKind::Ground) {
            continue;
        }
        if (card.kind == nec::NecCardKind::GeometryEnd) {
            lines.push_back(environment == AverageGainEnvironment::PerfectGround ? "GE 1" : "GE 0");
            if (environment == AverageGainEnvironment::PerfectGround) {
                lines.push_back("GN 1");
                groundInserted = true;
            }
            continue;
        }
        if (card.kind == nec::NecCardKind::End) {
            insertRequests();
            lines.push_back(card.sourceText);
            continue;
        }
        if (card.kind == nec::NecCardKind::Load
            || card.kind == nec::NecCardKind::TransmissionLine
            || card.kind == nec::NecCardKind::Network) {
            const auto transformed = losslessCard(card);
            if (!transformed.empty()) lines.push_back(transformed);
            continue;
        }
        lines.push_back(card.sourceText);
    }
    if (environment == AverageGainEnvironment::PerfectGround && !groundInserted) {
        const auto geometryEnd = std::ranges::find_if(lines,
            [](const auto& line) { return line.starts_with("GE ") || line == "GE"; });
        if (geometryEnd != lines.end()) lines.insert(std::next(geometryEnd), "GN 1");
    }
    if (!insertedRequests) insertRequests();

    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) output << document.lineEnding();
        output << lines[index];
    }
    if (document.hasFinalLineEnding() && !lines.empty()) output << document.lineEnding();
    return output.str();
}

auto assessAverageGain(double averagePowerGain, double expectedGain) -> AverageGainAssessment
{
    AverageGainAssessment assessment;
    assessment.averagePowerGain = averagePowerGain;
    assessment.expectedGain = expectedGain;
    assessment.normalizedGain = expectedGain > 0.0 ? averagePowerGain / expectedGain : 0.0;
    assessment.gainAdjustmentDb = assessment.normalizedGain > 0.0
        ? -10.0 * std::log10(assessment.normalizedGain) : 0.0;
    const auto value = assessment.normalizedGain;
    if (value >= 0.95 && value <= 1.05)
        assessment.classification = AverageGainClassification::Pass;
    else if (value >= 0.90 && value <= 1.10)
        assessment.classification = AverageGainClassification::Usable;
    else if (value >= 0.80 && value <= 1.20)
        assessment.classification = AverageGainClassification::Caution;
    else
        assessment.classification = AverageGainClassification::Questionable;
    return assessment;
}

}
