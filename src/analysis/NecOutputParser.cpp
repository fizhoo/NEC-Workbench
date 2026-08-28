#include "analysis/NecOutputParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace necwb::analysis {
namespace {

auto parseFrequency(const std::string& line, double& frequencyMHz) -> bool
{
    const auto marker = line.find("FREQUENCY :");
    if (marker == std::string::npos) {
        return false;
    }
    auto valueText = line.substr(marker + 11);
    std::ranges::replace(valueText, 'D', 'E');
    std::ranges::replace(valueText, 'd', 'e');
    std::istringstream values(valueText);
    return static_cast<bool>(values >> frequencyMHz);
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

}

auto NecOutputParser::parse(std::string_view output) const -> AnalysisResult
{
    AnalysisResult result;
    std::istringstream lines{std::string(output)};
    std::string line;
    double frequencyMHz{};
    bool readingInputParameters{};
    bool foundInputRow{};
    bool readingCurrents{};
    bool foundCurrentRow{};
    bool readingRadiation{};
    bool foundRadiationRow{};
    while (std::getline(lines, line)) {
        parseFrequency(line, frequencyMHz);
        if (line.find("ANTENNA INPUT PARAMETERS") != std::string::npos) {
            readingInputParameters = true;
            foundInputRow = false;
            continue;
        }
        if (line.find("CURRENTS AND LOCATION") != std::string::npos) {
            readingCurrents = true;
            foundCurrentRow = false;
            continue;
        }
        if (line.find("RADIATION PATTERNS") != std::string::npos) {
            readingRadiation = true;
            foundRadiationRow = false;
            continue;
        }
        if (readingCurrents) {
            SegmentCurrentResult current;
            if (parseCurrent(line, frequencyMHz, current)) {
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
            result.feedpoints.push_back(feedpoint);
            foundInputRow = true;
        } else if (foundInputRow && line.find_first_not_of(" \t\r") == std::string::npos) {
            readingInputParameters = false;
        }
    }
    return result;
}

}
