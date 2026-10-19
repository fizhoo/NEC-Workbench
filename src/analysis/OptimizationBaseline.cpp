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
#include <sstream>

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

auto hasRadiationCoverage(std::span<const RadiationCoverage> available,
    const RadiationCoverage& required) -> bool
{
    return std::ranges::any_of(available, [&required](const RadiationCoverage& coverage) {
        return nearlyEqual(coverage.frequencyMHz, required.frequencyMHz)
            && coverage.patternIndex == required.patternIndex
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
    for (const auto& radiation : requirements.radiation) {
        if (!hasRadiationCoverage(baseline->coverage.radiation, radiation))
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
