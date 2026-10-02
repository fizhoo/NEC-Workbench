#include "analysis/AverageGainTest.h"

#include "analysis/SolverInput.h"

#include "nec/NecParser.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <utility>
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
    const auto document = nec::NecParser{}.parse(normalizeSolverDeck(source));
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

auto integrateAverageGain(std::span<const RadiationSample> samples,
    AverageGainEnvironment environment) -> std::optional<IntegratedAverageGain>
{
    if (samples.empty()) return std::nullopt;
    constexpr auto Pi = 3.14159265358979323846;
    constexpr auto DegreesToRadians = Pi / 180.0;
    constexpr auto AngleTolerance = 1.0e-6;
    const auto frequencyMHz = samples.front().frequencyMHz;
    const auto patternIndex = samples.front().patternIndex;
    std::map<double, std::vector<std::pair<double, double>>> rows;
    for (const auto& sample : samples) {
        if (std::abs(sample.frequencyMHz - frequencyMHz) > 1.0e-9
            || sample.patternIndex != patternIndex
            || !std::isfinite(sample.thetaDegrees)
            || !std::isfinite(sample.phiDegrees)
            || !std::isfinite(sample.totalGainDb)) {
            continue;
        }
        rows[sample.thetaDegrees].emplace_back(
            sample.phiDegrees, std::pow(10.0, sample.totalGainDb / 10.0));
    }
    if (rows.size() < 2) return std::nullopt;
    for (auto& [theta, row] : rows) {
        std::ranges::sort(row, {}, &std::pair<double, double>::first);
    }
    const auto expectedMaximumTheta = environment == AverageGainEnvironment::PerfectGround
        ? 90.0 : 180.0;
    if (std::abs(rows.begin()->first) > AngleTolerance
        || std::abs(rows.rbegin()->first - expectedMaximumTheta) > AngleTolerance) {
        return std::nullopt;
    }

    auto integratedGain = 0.0;
    auto solidAngle = 0.0;
    for (auto upper = rows.begin(), lower = std::next(upper); lower != rows.end(); ++upper, ++lower) {
        const auto& upperRow = upper->second;
        const auto& lowerRow = lower->second;
        if (upperRow.size() < 2 || upperRow.size() != lowerRow.size()
            || std::abs(upperRow.front().first) > AngleTolerance
            || std::abs(upperRow.back().first - 360.0) > AngleTolerance) {
            return std::nullopt;
        }
        const auto thetaWeight = std::cos(upper->first * DegreesToRadians)
            - std::cos(lower->first * DegreesToRadians);
        if (thetaWeight <= 0.0) return std::nullopt;
        for (auto index = std::size_t{}; index + 1 < upperRow.size(); ++index) {
            if (std::abs(upperRow[index].first - lowerRow[index].first) > AngleTolerance
                || std::abs(upperRow[index + 1].first - lowerRow[index + 1].first)
                    > AngleTolerance) {
                return std::nullopt;
            }
            const auto phiWidth = (upperRow[index + 1].first - upperRow[index].first)
                * DegreesToRadians;
            if (phiWidth <= 0.0) return std::nullopt;
            const auto cellSolidAngle = phiWidth * thetaWeight;
            const auto cellGain = (upperRow[index].second + upperRow[index + 1].second
                + lowerRow[index].second + lowerRow[index + 1].second) / 4.0;
            integratedGain += cellGain * cellSolidAngle;
            solidAngle += cellSolidAngle;
        }
    }
    if (solidAngle <= 0.0) return std::nullopt;
    return IntegratedAverageGain{integratedGain / solidAngle, solidAngle / Pi};
}

}
