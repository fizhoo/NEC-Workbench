#pragma once

#include "model/Wire.h"
#include "nec/NecDocument.h"
#include "model/ModelSetup.h"

#include <string>

namespace necwb::nec {

class NecWriter {
public:
    [[nodiscard]] auto write(const NecDocument& document) const -> std::string;
    [[nodiscard]] auto writeWireCard(const model::Wire& wire) const -> std::string;
    [[nodiscard]] auto writeFrequencyCard(const model::FrequencyDefinition& frequency) const -> std::string;
    [[nodiscard]] auto writeExcitationCard(const model::Excitation& excitation) const -> std::string;
    [[nodiscard]] auto writeGroundCard(const model::GroundDefinition& ground) const -> std::string;
    [[nodiscard]] auto writeGeometryEndCard(int groundFlag) const -> std::string;
    [[nodiscard]] auto writeExecutionCard(const model::ExecutionRequest& request) const -> std::string;
    [[nodiscard]] auto writeRadiationPatternCard(const model::RadiationPatternRequest& request) const -> std::string;
    [[nodiscard]] auto writeLoadCard(const model::LoadDefinition& load) const -> std::string;
    [[nodiscard]] auto writeTransmissionLineCard(const model::TransmissionLineDefinition& line) const -> std::string;
};

}
