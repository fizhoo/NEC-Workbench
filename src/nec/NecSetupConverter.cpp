#include "nec/NecSetupConverter.h"

#include "nec/ParseNumber.h"

#include <cmath>
#include <numbers>

namespace necwb::nec {
namespace {

// Cards NEC-2 reads without ending a run of NT and TL cards. nec2c leaves its card-flow state
// alone for PT and PL; Workbench-only cards never reach the solver.
auto continuesNetworkGroup(const NecCard& card) -> bool
{
    switch (card.kind) {
    case NecCardKind::Blank:
    case NecCardKind::Comment:
    case NecCardKind::Symbol:
    case NecCardKind::ReferenceImpedance:
    case NecCardKind::TransmissionLine:
    case NecCardKind::Network:
        return true;
    case NecCardKind::ControlOther:
    case NecCardKind::Unknown:
        return card.mnemonic == "PT" || card.mnemonic == "PL";
    default:
        return false;
    }
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
    auto networkGroup = -1;
    auto networkGroupOpen = false;
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::TransmissionLine || card.kind == NecCardKind::Network) {
            const auto startsGroup = !networkGroupOpen;
            if (startsGroup) ++networkGroup;
            networkGroupOpen = true;
            // NEC-2 reads segment -1 on the first card of a group as "clear only".
            int segment{};
            if (startsGroup && card.fields.size() >= 2 && parseNumber(card.fields[1], segment)
                && segment == -1) {
                setup.networkClearLines.push_back(card.lineNumber);
                continue;
            }
        } else if (!continuesNetworkGroup(card)) {
            networkGroupOpen = false;
        }
        if (card.kind == NecCardKind::Frequency && !setup.frequency && card.fields.size() >= 5) {
            model::FrequencyDefinition frequency;
            int unused1{};
            int unused2{};
            frequency.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], frequency.steppingMode)
                && parseNumber(card.fields[1], frequency.count)
                && parseNumber(card.fields[2], unused1)
                && parseNumber(card.fields[3], unused2)
                && parseNumber(card.fields[4], frequency.startMHz)
                && (card.fields.size() < 6 || parseNumber(card.fields[5], frequency.step))
                && (frequency.steppingMode == 0 || frequency.steppingMode == 1)) {
                if (frequency.count == 0) frequency.count = 1;
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
        if (card.kind == NecCardKind::Excitation && card.fields.size() >= 5) {
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
                && (card.fields.size() < 6 || parseNumber(card.fields[5], imaginary))) {
                excitation.magnitude = std::hypot(real, imaginary);
                excitation.phaseDegrees = std::atan2(imaginary, real) * 180.0 / std::numbers::pi;
                setup.excitations.push_back(excitation);
            }
        }
        if (card.kind == NecCardKind::Load && card.fields.size() >= 5) {
            model::LoadDefinition load;
            load.sourceLine = card.lineNumber;
            if (parseNumber(card.fields[0], load.type)
                && parseNumber(card.fields[1], load.wireTag)
                && parseNumber(card.fields[2], load.firstSegment)
                && parseNumber(card.fields[3], load.lastSegment)
                && parseNumber(card.fields[4], load.value1)
                && (card.fields.size() < 6 || parseNumber(card.fields[5], load.value2))
                && (card.fields.size() < 7 || parseNumber(card.fields[6], load.value3))) {
                setup.loads.push_back(load);
            }
        }
        if (card.kind == NecCardKind::TransmissionLine && card.fields.size() >= 5) {
            model::TransmissionLineDefinition line;
            line.sourceLine = card.lineNumber;
            line.group = networkGroup;
            if (parseNumber(card.fields[0], line.wireTag1)
                && parseNumber(card.fields[1], line.segment1)
                && parseNumber(card.fields[2], line.wireTag2)
                && parseNumber(card.fields[3], line.segment2)
                && parseNumber(card.fields[4], line.characteristicImpedance)
                && (card.fields.size() < 6 || parseNumber(card.fields[5], line.lengthMeters))
                && (card.fields.size() < 7 || parseNumber(card.fields[6], line.shuntReal1))
                && (card.fields.size() < 8 || parseNumber(card.fields[7], line.shuntImaginary1))
                && (card.fields.size() < 9 || parseNumber(card.fields[8], line.shuntReal2))
                && (card.fields.size() < 10 || parseNumber(card.fields[9], line.shuntImaginary2))) {
                setup.transmissionLines.push_back(line);
            }
        }
        if (card.kind == NecCardKind::Network && card.fields.size() >= 4) {
            model::NetworkDefinition network;
            network.sourceLine = card.lineNumber;
            network.group = networkGroup;
            const auto optional = [&card](std::size_t index, double& value) {
                return card.fields.size() <= index || parseNumber(card.fields[index], value);
            };
            if (parseNumber(card.fields[0], network.wireTag1)
                && parseNumber(card.fields[1], network.segment1)
                && parseNumber(card.fields[2], network.wireTag2)
                && parseNumber(card.fields[3], network.segment2)
                && optional(4, network.y11Real) && optional(5, network.y11Imaginary)
                && optional(6, network.y12Real) && optional(7, network.y12Imaginary)
                && optional(8, network.y22Real) && optional(9, network.y22Imaginary)) {
                setup.networks.push_back(network);
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
