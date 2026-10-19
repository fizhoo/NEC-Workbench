#include "analysis/OptimizationBaseline.h"

#include "analysis/FrequencyComparison.h"
#include "nec/NecParser.h"
#include "nec/ParseNumber.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace necwb::analysis {
namespace {

auto canonicalNumber(double number) -> std::string
{
    if (number == 0.0) number = 0.0;
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : std::string{};
}

auto canonicalField(std::string_view field) -> std::string
{
    double number{};
    if (!nec::parseNumber(field, number) || !std::isfinite(number)) return std::string(field);
    const auto canonical = canonicalNumber(number);
    return canonical.empty() ? std::string(field) : canonical;
}

void mixByte(std::uint64_t& hash, unsigned char value, std::uint64_t prime)
{
    hash ^= value;
    hash *= prime;
}

auto stableFingerprint(std::string_view value) -> std::string
{
    auto first = std::uint64_t{14695981039346656037ULL};
    auto second = std::uint64_t{7809847782465536322ULL};
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        mixByte(first, byte, 1099511628211ULL);
        mixByte(second, byte, 14029467366897019727ULL);
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << first
           << std::setw(16) << second;
    return output.str();
}

auto directionFingerprint(std::vector<std::pair<double, double>> directions) -> std::string
{
    std::ranges::sort(directions);
    std::string canonical;
    for (const auto& [theta, phi] : directions) {
        canonical += canonicalNumber(theta);
        canonical += ',';
        canonical += canonicalNumber(phi);
        canonical += ';';
    }
    return stableFingerprint(canonical);
}

auto hasFrequency(std::span<const double> available, double required) -> bool
{
    return std::ranges::any_of(available, [required](double frequency) {
        return nearlyEqual(frequency, required);
    });
}

using RadiationCoverageIndex = std::unordered_multimap<int, const RadiationCoverage*>;

auto hasRadiationCoverage(const RadiationCoverageIndex& available,
    const RadiationCoverage& required) -> bool
{
    const auto [first, last] = available.equal_range(required.patternIndex);
    return std::any_of(first, last, [&required](const auto& item) {
        const auto& coverage = *item.second;
        return nearlyEqual(coverage.frequencyMHz, required.frequencyMHz)
            && coverage.sampleCount == required.sampleCount
            && coverage.directionFingerprint == required.directionFingerprint;
    });
}

}

auto OptimizationBaselineCompatibility::compatible() const noexcept -> bool
{
    return status == OptimizationBaselineStatus::Compatible;
}

auto optimizationModelFingerprint(std::string_view resolvedDeck) -> std::string
{
    const auto document = nec::NecParser{}.parse(resolvedDeck);
    std::string canonical;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Blank || card.kind == nec::NecCardKind::Comment
            || card.kind == nec::NecCardKind::Symbol
            || card.kind == nec::NecCardKind::Frequency
            || card.kind == nec::NecCardKind::RadiationPattern
            || card.kind == nec::NecCardKind::Execute) {
            continue;
        }
        canonical += card.mnemonic;
        for (const auto& field : card.fields) {
            canonical += '\x1f';
            canonical += canonicalField(field);
        }
        canonical += '\x1e';
    }
    return stableFingerprint(canonical);
}

auto optimizationResultCoverage(const AnalysisResult& result) -> OptimizationResultCoverage
{
    OptimizationResultCoverage coverage;
    coverage.referenceImpedanceOhms = result.referenceImpedanceOhms;
    for (const auto& feedpoint : result.feedpoints) {
        if (!hasFrequency(coverage.impedanceFrequenciesMHz, feedpoint.frequencyMHz))
            coverage.impedanceFrequenciesMHz.push_back(feedpoint.frequencyMHz);
    }
    std::ranges::sort(coverage.impedanceFrequenciesMHz);

    using RadiationKey = std::pair<double, int>;
    std::map<RadiationKey, std::vector<std::pair<double, double>>> directions;
    for (const auto& sample : result.radiation) {
        directions[{sample.frequencyMHz, sample.patternIndex}].emplace_back(
            sample.thetaDegrees, sample.phiDegrees);
    }
    for (auto& [key, values] : directions) {
        coverage.radiation.push_back({key.first, key.second, values.size(),
            directionFingerprint(std::move(values))});
    }
    return coverage;
}

auto optimizationRadiationRequirements(std::string_view solverDeck)
    -> std::vector<RadiationCoverage>
{
    const auto document = nec::NecParser{}.parse(solverDeck);
    std::optional<double> frequencyMHz;
    std::vector<std::pair<double, int>> nextPatternIndexes;
    std::vector<RadiationCoverage> requirements;
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Frequency && card.fields.size() >= 5) {
            double parsedFrequency{};
            if (nec::parseNumber(card.fields[4], parsedFrequency)
                && std::isfinite(parsedFrequency) && parsedFrequency > 0.0) {
                frequencyMHz = parsedFrequency;
            }
            continue;
        }
        if (card.kind != nec::NecCardKind::RadiationPattern || !frequencyMHz
            || card.fields.size() < 8) {
            continue;
        }

        int thetaCount{};
        int phiCount{};
        double thetaStart{};
        double phiStart{};
        double thetaStep{};
        double phiStep{};
        if (!nec::parseNumber(card.fields[1], thetaCount)
            || !nec::parseNumber(card.fields[2], phiCount)
            || !nec::parseNumber(card.fields[4], thetaStart)
            || !nec::parseNumber(card.fields[5], phiStart)
            || !nec::parseNumber(card.fields[6], thetaStep)
            || !nec::parseNumber(card.fields[7], phiStep)
            || thetaCount <= 0 || phiCount <= 0) {
            continue;
        }

        auto pattern = std::ranges::find_if(nextPatternIndexes,
            [frequency = *frequencyMHz](const auto& item) {
                return nearlyEqual(item.first, frequency);
            });
        if (pattern == nextPatternIndexes.end()) {
            nextPatternIndexes.emplace_back(*frequencyMHz, 0);
            pattern = std::prev(nextPatternIndexes.end());
        }

        std::vector<std::pair<double, double>> directions;
        directions.reserve(static_cast<std::size_t>(thetaCount)
            * static_cast<std::size_t>(phiCount));
        for (auto thetaIndex = 0; thetaIndex < thetaCount; ++thetaIndex) {
            for (auto phiIndex = 0; phiIndex < phiCount; ++phiIndex) {
                directions.emplace_back(thetaStart + thetaIndex * thetaStep,
                    phiStart + phiIndex * phiStep);
            }
        }
        requirements.push_back({*frequencyMHz, pattern->second++, directions.size(),
            directionFingerprint(std::move(directions))});
    }
    return requirements;
}

auto evaluateOptimizationBaseline(
    const std::optional<OptimizationBaselineDescriptor>& baseline,
    const OptimizationBaselineRequirements& requirements)
    -> OptimizationBaselineCompatibility
{
    const auto status = [](OptimizationBaselineStatus value) {
        OptimizationBaselineCompatibility result;
        result.status = value;
        return result;
    };
    if (!baseline) return status(OptimizationBaselineStatus::Missing);
    if (baseline->modelFingerprint.empty() || requirements.modelFingerprint.empty()
        || baseline->backend.empty() || requirements.backend.empty()) {
        return status(OptimizationBaselineStatus::IncompleteMetadata);
    }
    if (baseline->modelFingerprint != requirements.modelFingerprint)
        return status(OptimizationBaselineStatus::ModelChanged);
    if (baseline->backend != requirements.backend)
        return status(OptimizationBaselineStatus::BackendChanged);
    if (!requirements.impedanceFrequenciesMHz.empty()
        && !nearlyEqual(baseline->coverage.referenceImpedanceOhms,
            requirements.referenceImpedanceOhms)) {
        return status(OptimizationBaselineStatus::ReferenceImpedanceChanged);
    }

    OptimizationBaselineCompatibility compatibility;
    for (const auto frequency : requirements.impedanceFrequenciesMHz) {
        if (!hasFrequency(baseline->coverage.impedanceFrequenciesMHz, frequency))
            compatibility.missingImpedanceFrequenciesMHz.push_back(frequency);
    }
    if (!compatibility.missingImpedanceFrequenciesMHz.empty()) {
        compatibility.status = OptimizationBaselineStatus::MissingImpedanceCoverage;
        return compatibility;
    }
    for (const auto frequency : requirements.radiationFrequenciesMHz) {
        const auto covered = std::ranges::any_of(baseline->coverage.radiation,
            [frequency](const RadiationCoverage& coverage) {
                return nearlyEqual(coverage.frequencyMHz, frequency);
            });
        if (!covered) compatibility.missingRadiationFrequenciesMHz.push_back(frequency);
    }
    if (!compatibility.missingRadiationFrequenciesMHz.empty()) {
        compatibility.status = OptimizationBaselineStatus::MissingRadiationCoverage;
        return compatibility;
    }
    RadiationCoverageIndex radiationIndex;
    radiationIndex.reserve(baseline->coverage.radiation.size());
    for (const auto& radiation : baseline->coverage.radiation)
        radiationIndex.emplace(radiation.patternIndex, &radiation);
    for (const auto& radiation : requirements.radiation) {
        if (!hasRadiationCoverage(radiationIndex, radiation))
            compatibility.missingRadiation.push_back(radiation);
    }
    if (!compatibility.missingRadiation.empty()) {
        compatibility.status = OptimizationBaselineStatus::MissingRadiationCoverage;
        return compatibility;
    }
    compatibility.status = OptimizationBaselineStatus::Compatible;
    return compatibility;
}

}
