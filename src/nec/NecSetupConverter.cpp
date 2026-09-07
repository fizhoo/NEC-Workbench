#include "nec/NecSetupConverter.h"

#include <charconv>
#include <cmath>
#include <numbers>
#include <string_view>

namespace necwb::nec {
namespace {

template<typename Value>
auto parseNumber(std::string_view text, Value& value) -> bool
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

}

auto NecSetupConverter::convert(const NecDocument& document) const -> model::ModelSetup
{
    model::ModelSetup setup;
    auto geometryGroundFlag = 0;
    auto geometryEndSourceLine = std::size_t{};
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::GeometryEnd) {
            geometryEndSourceLine = card.lineNumber;
            if (!card.fields.empty()) {
                parseNumber(card.fields[0], geometryGroundFlag);
            }
            break;
        }
    }
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Frequency && !setup.frequency && card.fields.size() >= 6) {
            model::FrequencyDefinition frequency;
            int unused1{};
            int unused2{};
            frequency.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], frequency.steppingMode)
                && parseNumber(card.fields[1], frequency.count)
                && parseNumber(card.fields[2], unused1)
                && parseNumber(card.fields[3], unused2)
                && parseNumber(card.fields[4], frequency.startMHz)
                && parseNumber(card.fields[5], frequency.step)
                && (frequency.steppingMode == 0 || frequency.steppingMode == 1)) {
                setup.frequency = frequency;
            }
        }
        if (card.kind == NecCardKind::Ground && !setup.ground && !card.fields.empty()) {
            int typeValue{};
            if (!parseNumber(card.fields[0], typeValue)
                || typeValue < static_cast<int>(model::GroundType::FreeSpace)
                || typeValue > static_cast<int>(model::GroundType::SommerfeldNorton)) {
                continue;
            }
            model::GroundDefinition ground;
            ground.type = static_cast<model::GroundType>(typeValue);
            ground.sourceLine = card.lineNumber;
            ground.geometryEndSourceLine = geometryEndSourceLine;
            ground.geometryGroundFlag = geometryGroundFlag;
            if ((ground.type == model::GroundType::ReflectionApproximation
                    || ground.type == model::GroundType::SommerfeldNorton)
                && card.fields.size() >= 6
                && parseNumber(card.fields[4], ground.relativePermittivity)
                && parseNumber(card.fields[5], ground.conductivity)) {
                setup.ground = ground;
            } else if (ground.type == model::GroundType::FreeSpace
                || ground.type == model::GroundType::Perfect) {
                setup.ground = ground;
            }
        }
        if (card.kind == NecCardKind::Execute && !setup.executionRequest) {
            model::ExecutionRequest request;
            request.sourceLine = card.lineNumber;
            if (card.fields.empty() || parseNumber(card.fields[0], request.option)) {
                setup.executionRequest = request;
            }
        }
        if (card.kind == NecCardKind::ReferenceImpedance
            && !setup.referenceImpedance && !card.fields.empty()) {
            model::ReferenceImpedanceDefinition reference;
            reference.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], reference.ohms) && reference.ohms > 0.0)
                setup.referenceImpedance = reference;
        }
        if (card.kind == NecCardKind::RadiationPattern && card.fields.size() >= 8) {
            model::RadiationPatternRequest request;
            int mode{};
            int format{};
            request.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], mode) && mode == 0
                && parseNumber(card.fields[1], request.thetaCount)
                && parseNumber(card.fields[2], request.phiCount)
                && parseNumber(card.fields[3], format)
                && parseNumber(card.fields[4], request.thetaStart)
                && parseNumber(card.fields[5], request.phiStart)
                && parseNumber(card.fields[6], request.thetaStep)
                && parseNumber(card.fields[7], request.phiStep)) {
                setup.radiationPatterns.push_back(request);
            }
        }
        if (card.kind == NecCardKind::Excitation && card.fields.size() >= 6) {
            model::Excitation excitation;
            int unused{};
            double real{};
            double imaginary{};
            excitation.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], excitation.type)
                && excitation.type == 0
                && parseNumber(card.fields[1], excitation.wireTag)
                && parseNumber(card.fields[2], excitation.segment)
                && parseNumber(card.fields[3], unused)
                && parseNumber(card.fields[4], real)
                && parseNumber(card.fields[5], imaginary)) {
                excitation.magnitude = std::hypot(real, imaginary);
                excitation.phaseDegrees = std::atan2(imaginary, real) * 180.0 / std::numbers::pi;
                setup.excitations.push_back(excitation);
            }
        }
        if (card.kind == NecCardKind::Load && card.fields.size() >= 7) {
            model::LoadDefinition load;
            load.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], load.type)
                && parseNumber(card.fields[1], load.wireTag)
                && parseNumber(card.fields[2], load.firstSegment)
                && parseNumber(card.fields[3], load.lastSegment)
                && parseNumber(card.fields[4], load.value1)
                && parseNumber(card.fields[5], load.value2)
                && parseNumber(card.fields[6], load.value3)) {
                setup.loads.push_back(load);
            }
        }
        if (card.kind == NecCardKind::TransmissionLine && card.fields.size() >= 10) {
            model::TransmissionLineDefinition line;
            line.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], line.wireTag1)
                && parseNumber(card.fields[1], line.segment1)
                && parseNumber(card.fields[2], line.wireTag2)
                && parseNumber(card.fields[3], line.segment2)
                && parseNumber(card.fields[4], line.characteristicImpedance)
                && parseNumber(card.fields[5], line.lengthMeters)
                && parseNumber(card.fields[6], line.shuntReal1)
                && parseNumber(card.fields[7], line.shuntImaginary1)
                && parseNumber(card.fields[8], line.shuntReal2)
                && parseNumber(card.fields[9], line.shuntImaginary2)) {
                setup.transmissionLines.push_back(line);
            }
        }
    }
    if (!setup.ground) {
        model::GroundDefinition freeSpace;
        freeSpace.geometryGroundFlag = geometryGroundFlag;
        freeSpace.geometryEndSourceLine = geometryEndSourceLine;
        setup.ground = freeSpace;
    }
    return setup;
}

}
