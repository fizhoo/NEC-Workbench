#include "analysis/NecOutputParser.h"

#include "analysis/FrequencyComparison.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace necwb::analysis {
namespace {

auto parseFrequency(const std::string& line, double& frequencyMHz) -> bool
{
    const auto marker = line.find("FREQUENCY");
    if (marker == std::string::npos) {
        return false;
    }
    const auto delimiter = line.find_first_of(":=", marker + 9);
    if (delimiter == std::string::npos) {
        return false;
    }
    auto valueText = line.substr(delimiter + 1);
    std::ranges::replace(valueText, 'D', 'E');
    std::ranges::replace(valueText, 'd', 'e');
    std::istringstream values(valueText);
    return static_cast<bool>(values >> frequencyMHz);
}

auto parseLabeledValue(const std::string& line, std::string_view marker, double& value) -> bool
{
    const auto position = line.find(marker);
    if (position == std::string::npos) return false;
    auto valueText = line.substr(position + marker.size());
    std::ranges::replace(valueText, 'D', 'E');
    std::ranges::replace(valueText, 'd', 'e');
    const auto numberStart = valueText.find_first_of("+-.0123456789");
    if (numberStart == std::string::npos) return false;
    std::istringstream values(valueText.substr(numberStart));
    return static_cast<bool>(values >> value);
}

auto parseFeedpoint(const std::string& source, double frequencyMHz, FeedpointResult& result) -> bool
{
    auto line = source;
    std::ranges::replace(line, 'D', 'E');
    std::ranges::replace(line, 'd', 'e');
    double voltageReal{};
    double voltageImaginary{};
    double currentReal{};
    double currentImaginary{};
    double impedanceReal{};
    double impedanceImaginary{};
    double admittanceReal{};
    double admittanceImaginary{};
    std::istringstream values(line);
    if (!(values >> result.wireTag >> result.segment
            >> voltageReal >> voltageImaginary
            >> currentReal >> currentImaginary
            >> impedanceReal >> impedanceImaginary
            >> admittanceReal >> admittanceImaginary
            >> result.inputPowerWatts)) {
        return false;
    }
    result.frequencyMHz = frequencyMHz;
    result.voltage = {voltageReal, voltageImaginary};
    result.current = {currentReal, currentImaginary};
    result.impedance = {impedanceReal, impedanceImaginary};
    return true;
}

auto normalizedNumericLine(const std::string& source) -> std::string
{
    auto line = source;
    std::ranges::replace(line, 'D', 'E');
    std::ranges::replace(line, 'd', 'e');
    return line;
}

auto parseCurrent(const std::string& source, double frequencyMHz,
    SegmentCurrentResult& result) -> bool
{
    double currentReal{};
    double currentImaginary{};
    std::istringstream values(normalizedNumericLine(source));
    if (!(values >> result.segment >> result.wireTag
            >> result.xWavelengths >> result.yWavelengths >> result.zWavelengths
            >> result.lengthWavelengths >> currentReal >> currentImaginary
            >> result.magnitude >> result.phaseDegrees)) {
        return false;
    }
    result.frequencyMHz = frequencyMHz;
    result.current = {currentReal, currentImaginary};
    return true;
}

auto parseRadiation(const std::string& source, double frequencyMHz,
    RadiationSample& result) -> bool
{
    std::istringstream values(normalizedNumericLine(source));
    if (!(values >> result.thetaDegrees >> result.phiDegrees
            >> result.verticalGainDb >> result.horizontalGainDb >> result.totalGainDb)) {
        return false;
    }
    std::string sense;
    if (values >> result.axialRatio >> result.tiltDegrees >> sense) {
        std::ranges::transform(sense, sense.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        if (sense.starts_with("RIGHT")) result.polarizationSense = PolarizationSense::RightHand;
        else if (sense.starts_with("LEFT")) result.polarizationSense = PolarizationSense::LeftHand;
        else if (sense.starts_with("LINEAR")) result.polarizationSense = PolarizationSense::Linear;
    }
    result.frequencyMHz = frequencyMHz;
    return true;
}

struct SourceResultKey {
    int frequencyIndex{};
    int wireTag{};
    int segment{};

    auto operator==(const SourceResultKey&) const -> bool = default;
};

struct SourceResultKeyHash {
    auto operator()(const SourceResultKey& key) const noexcept -> std::size_t
    {
        auto value = std::hash<int>{}(key.frequencyIndex);
        value ^= std::hash<int>{}(key.wireTag) + 0x9e3779b9U + (value << 6U) + (value >> 2U);
        value ^= std::hash<int>{}(key.segment) + 0x9e3779b9U + (value << 6U) + (value >> 2U);
        return value;
    }
};

}

auto NecOutputParser::parse(std::string_view output) const -> AnalysisResult
{
    AnalysisResult result;
    std::istringstream lines{std::string(output)};
    std::string line;
    double frequencyMHz{};
    int frequencyIndex{-1};
    std::vector<double> frequencies;
    std::unordered_set<SourceResultKey, SourceResultKeyHash> feedpointKeys;
    std::unordered_set<SourceResultKey, SourceResultKeyHash> currentKeys;
    std::unordered_map<int, int> firstGlobalSegmentByTag;
    std::unordered_map<int, int> nextPatternIndexByFrequency;
    int currentPatternIndex{};
    bool readingInputParameters{};
    bool foundInputRow{};
    bool readingCurrents{};
    bool foundCurrentRow{};
    bool readingRadiation{};
    bool foundRadiationRow{};
    while (std::getline(lines, line)) {
        if (parseFrequency(line, frequencyMHz)) {
            const auto existing = std::ranges::find_if(frequencies, [frequencyMHz](double value) {
                return nearlyEqual(value, frequencyMHz);
            });
            if (existing == frequencies.end()) {
                frequencyIndex = static_cast<int>(frequencies.size());
                frequencies.push_back(frequencyMHz);
            } else {
                frequencyIndex = static_cast<int>(std::distance(frequencies.begin(), existing));
            }
        }
        double averagePowerGain{};
        if (parseLabeledValue(line, "AVERAGE POWER GAIN", averagePowerGain))
            result.averagePowerGain = averagePowerGain;
        double solidAnglePi{};
        if (parseLabeledValue(line, "SOLID ANGLE USED IN AVERAGING", solidAnglePi))
            result.averagingSolidAnglePi = solidAnglePi;
        if (line.find("ANTENNA INPUT PARAMETERS") != std::string::npos) {
            readingInputParameters = true;
            foundInputRow = false;
            continue;
        }
        if (line.find("CURRENTS AND LOCATION") != std::string::npos) {
            readingCurrents = true;
            foundCurrentRow = false;
            firstGlobalSegmentByTag.clear();
            continue;
        }
        if (line.find("RADIATION PATTERNS") != std::string::npos) {
            readingRadiation = true;
            foundRadiationRow = false;
            currentPatternIndex = nextPatternIndexByFrequency[frequencyIndex]++;
            continue;
        }
        if (readingCurrents) {
            SegmentCurrentResult current;
            if (parseCurrent(line, frequencyMHz, current)) {
                const auto globalSegment = current.segment;
                const auto first = firstGlobalSegmentByTag.try_emplace(
                    current.wireTag, globalSegment).first;
                current.segment = globalSegment - first->second + 1;
                if (currentKeys.insert({frequencyIndex, current.wireTag, current.segment}).second)
                    result.currents.push_back(current);
                foundCurrentRow = true;
                continue;
            }
            if (foundCurrentRow && line.find_first_not_of(" \t\r") == std::string::npos) {
                readingCurrents = false;
            }
        }
        if (readingRadiation) {
            RadiationSample sample;
            if (parseRadiation(line, frequencyMHz, sample)) {
                sample.patternIndex = currentPatternIndex;
                result.radiation.push_back(sample);
                foundRadiationRow = true;
                continue;
            }
            if (foundRadiationRow && line.find_first_not_of(" \t\r") == std::string::npos) {
                readingRadiation = false;
            }
        }
        if (!readingInputParameters) {
            continue;
        }
        FeedpointResult feedpoint;
        if (parseFeedpoint(line, frequencyMHz, feedpoint)) {
            if (feedpointKeys.insert({frequencyIndex, feedpoint.wireTag, feedpoint.segment}).second)
                result.feedpoints.push_back(feedpoint);
            foundInputRow = true;
        } else if (foundInputRow && line.find_first_not_of(" \t\r") == std::string::npos) {
            readingInputParameters = false;
        }
    }
    return result;
}

}
