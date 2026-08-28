#include "nec/NecWriter.h"

#include <iomanip>
#include <cmath>
#include <numbers>
#include <sstream>

namespace necwb::nec {

auto NecWriter::write(const NecDocument& document) const -> std::string
{
    std::string output;
    const auto cards = document.cards();
    for (std::size_t index = 0; index < cards.size(); ++index) {
        output += cards[index].sourceText;
        if (index + 1 < cards.size() || document.hasFinalLineEnding()) {
            output += document.lineEnding();
        }
    }
    return output;
}

auto NecWriter::writeWireCard(const model::Wire& wire) const -> std::string
{
    std::ostringstream output;
    output << std::setprecision(15)
           << "GW " << wire.tag << ' ' << wire.segments << ' '
           << wire.start.x << ' ' << wire.start.y << ' ' << wire.start.z << ' '
           << wire.end.x << ' ' << wire.end.y << ' ' << wire.end.z << ' '
           << wire.radius;
    return output.str();
}

auto NecWriter::writeFrequencyCard(const model::FrequencyDefinition& frequency) const -> std::string
{
    std::ostringstream output;
    output << std::setprecision(15)
           << "FR " << frequency.steppingMode << ' ' << frequency.count << " 0 0 "
           << frequency.startMHz << ' ' << frequency.step;
    return output.str();
}

auto NecWriter::writeExcitationCard(const model::Excitation& excitation) const -> std::string
{
    const auto phaseRadians = excitation.phaseDegrees * std::numbers::pi / 180.0;
    const auto real = excitation.magnitude * std::cos(phaseRadians);
    const auto imaginary = excitation.magnitude * std::sin(phaseRadians);
    std::ostringstream output;
    output << std::setprecision(15)
           << "EX " << excitation.type << ' ' << excitation.wireTag << ' '
           << excitation.segment << " 0 " << real << ' ' << imaginary;
    return output.str();
}

auto NecWriter::writeGroundCard(const model::GroundDefinition& ground) const -> std::string
{
    if (ground.type == model::GroundType::FreeSpace) {
        return "GN -1";
    }
    if (ground.type == model::GroundType::Perfect) {
        return "GN 1";
    }
    std::ostringstream output;
    output << std::setprecision(15)
           << "GN " << static_cast<int>(ground.type) << " 0 0 0 "
           << ground.relativePermittivity << ' ' << ground.conductivity << " 0 0 0 0";
    return output.str();
}

auto NecWriter::writeGeometryEndCard(int groundFlag) const -> std::string
{
    std::ostringstream output;
    output << "GE " << groundFlag;
    return output.str();
}

auto NecWriter::writeExecutionCard(const model::ExecutionRequest& request) const -> std::string
{
    std::ostringstream output;
    output << "XQ " << request.option;
    return output.str();
}

auto NecWriter::writeRadiationPatternCard(const model::RadiationPatternRequest& request) const -> std::string
{
    std::ostringstream output;
    output << std::setprecision(15)
           << "RP 0 " << request.thetaCount << ' ' << request.phiCount << " 1000 "
           << request.thetaStart << ' ' << request.phiStart << ' '
           << request.thetaStep << ' ' << request.phiStep << " 0 0";
    return output.str();
}

auto NecWriter::writeLoadCard(const model::LoadDefinition& load) const -> std::string
{
    std::ostringstream output;
    output << std::setprecision(15) << "LD " << load.type << ' ' << load.wireTag << ' '
           << load.firstSegment << ' ' << load.lastSegment << ' '
           << load.value1 << ' ' << load.value2 << ' ' << load.value3;
    return output.str();
}

auto NecWriter::writeTransmissionLineCard(const model::TransmissionLineDefinition& line) const -> std::string
{
    std::ostringstream output;
    output << std::setprecision(15) << "TL " << line.wireTag1 << ' ' << line.segment1 << ' '
           << line.wireTag2 << ' ' << line.segment2 << ' ' << line.characteristicImpedance << ' '
           << line.lengthMeters << ' ' << line.shuntReal1 << ' ' << line.shuntImaginary1 << ' '
           << line.shuntReal2 << ' ' << line.shuntImaginary2;
    return output.str();
}

}
