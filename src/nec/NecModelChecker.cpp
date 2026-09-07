#include "nec/NecModelChecker.h"

#include "nec/NecModelConverter.h"
#include "nec/NecSetupConverter.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace necwb::nec {
namespace {

template<typename Value>
auto isNumber(std::string_view text) -> bool
{
    Value value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

void addError(ModelCheckResult& result, const NecCard& card, std::string message)
{
    result.diagnostics.push_back(
        {DiagnosticSeverity::Error, card.lineNumber, std::move(message), "Card validation"});
}

void addAdequacyWarning(ModelCheckResult& result, std::size_t lineNumber, std::string message)
{
    result.diagnostics.push_back(
        {DiagnosticSeverity::Warning, lineNumber, std::move(message), "Model adequacy"});
}

auto wireLength(const model::Wire& wire) -> double
{
    const auto x = wire.end.x - wire.start.x;
    const auto y = wire.end.y - wire.start.y;
    const auto z = wire.end.z - wire.start.z;
    return std::sqrt(x * x + y * y + z * z);
}

auto nearby(const model::Point3D& first, const model::Point3D& second) -> bool
{
    const auto scale = std::max({1.0, std::abs(first.x), std::abs(first.y), std::abs(first.z),
        std::abs(second.x), std::abs(second.y), std::abs(second.z)});
    const auto tolerance = scale * 1.0e-9;
    return std::abs(first.x - second.x) <= tolerance
        && std::abs(first.y - second.y) <= tolerance
        && std::abs(first.z - second.z) <= tolerance;
}

auto concise(double value) -> std::string
{
    std::ostringstream stream;
    stream << std::setprecision(4) << value;
    return stream.str();
}

void checkStaticAdequacy(const model::ModelSetup& setup, ModelCheckResult& result)
{
    if (!setup.frequency || result.model.empty()) return;
    const auto highestFrequencyMHz = std::max(setup.frequency->startMHz,
        model::frequencyEndMHz(*setup.frequency));
    if (!std::isfinite(highestFrequencyMHz) || highestFrequencyMHz <= 0.0) return;
    const auto wavelengthMeters = 299.792458 / highestFrequencyMHz;

    struct Endpoint {
        model::Point3D point;
        const model::Wire* wire{};
        double segmentLength{};
    };
    std::vector<Endpoint> endpoints;
    endpoints.reserve(result.model.wireCount() * 2);

    for (const auto& wire : result.model.wires()) {
        if (wire.segments <= 0 || wire.radius <= 0.0) continue;
        const auto segmentLength = wireLength(wire) / static_cast<double>(wire.segments);
        const auto wavelengthRatio = segmentLength / wavelengthMeters;
        const auto diameterRatio = segmentLength / (2.0 * wire.radius);
        if (wavelengthRatio > 0.1) {
            addAdequacyWarning(result, wire.sourceLine,
                "Wire tag " + std::to_string(wire.tag) + " segment length is "
                    + concise(wavelengthRatio) + " wavelength at "
                    + concise(highestFrequencyMHz)
                    + " MHz; keep segments below 0.1 wavelength and prefer about 0.05");
        } else if (wavelengthRatio < 0.001) {
            addAdequacyWarning(result, wire.sourceLine,
                "Wire tag " + std::to_string(wire.tag) + " segment length is "
                    + concise(wavelengthRatio)
                    + " wavelength; very short segments can reduce NEC numerical reliability");
        }
        if (diameterRatio <= 4.0) {
            addAdequacyWarning(result, wire.sourceLine,
                "Wire tag " + std::to_string(wire.tag) + " segment-length/diameter ratio is "
                    + concise(diameterRatio)
                    + "; values above 4 are the conservative NEC-2 thin-wire guideline");
        }
        endpoints.push_back({wire.start, &wire, segmentLength});
        endpoints.push_back({wire.end, &wire, segmentLength});
    }

    std::vector<bool> grouped(endpoints.size());
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        if (grouped[index]) continue;
        std::vector<const Endpoint*> junction;
        for (std::size_t candidate = index; candidate < endpoints.size(); ++candidate) {
            if (nearby(endpoints[index].point, endpoints[candidate].point)) {
                grouped[candidate] = true;
                junction.push_back(&endpoints[candidate]);
            }
        }
        if (junction.size() < 2) continue;
        const auto [shortest, longest] = std::ranges::minmax_element(junction,
            {}, [](const Endpoint* endpoint) { return endpoint->segmentLength; });
        if ((*shortest)->segmentLength > 0.0
            && (*longest)->segmentLength / (*shortest)->segmentLength > 2.0) {
            addAdequacyWarning(result, (*longest)->wire->sourceLine,
                "Connected wires " + std::to_string((*shortest)->wire->tag) + " and "
                    + std::to_string((*longest)->wire->tag)
                    + " have adjoining segment lengths differing by more than 2:1");
        }
        if (junction.size() > 30) {
            addAdequacyWarning(result, junction.front()->wire->sourceLine,
                "Junction contains " + std::to_string(junction.size())
                    + " wires; NEC-2 recommends no more than 30 wires at one junction");
        }
    }

    for (const auto& excitation : setup.excitations) {
        const auto* wire = result.model.wireByTag(excitation.wireTag);
        if (excitation.type != 0 || wire == nullptr || wire->segments % 2 != 0) continue;
        if (excitation.segment == wire->segments / 2
            || excitation.segment == wire->segments / 2 + 1) {
            addAdequacyWarning(result, excitation.sourceLine,
                "Center-fed wire tag " + std::to_string(wire->tag)
                    + " has an even segment count; no segment lies exactly at its center");
        }
    }
}

void checkExcitation(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 6) {
        addError(result, card, "EX requires at least four integer fields and two numeric fields");
        return;
    }

    const bool integerFieldsValid = std::ranges::all_of(card.fields.begin(), card.fields.begin() + 4,
        [](const auto& field) { return isNumber<int>(field); });
    const bool numericFieldsValid = std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
        [](const auto& field) { return isNumber<double>(field); });
    if (!integerFieldsValid || !numericFieldsValid) {
        addError(result, card, "EX contains a value with the wrong numeric type");
        return;
    }

    int excitationType{};
    int wireTag{};
    int segment{};
    std::from_chars(card.fields[0].data(), card.fields[0].data() + card.fields[0].size(), excitationType);
    std::from_chars(card.fields[1].data(), card.fields[1].data() + card.fields[1].size(), wireTag);
    std::from_chars(card.fields[2].data(), card.fields[2].data() + card.fields[2].size(), segment);
    if (excitationType == 0) {
        const auto* wire = result.model.wireByTag(wireTag);
        if (wire == nullptr) {
            addError(result, card, "EX references a wire tag that does not exist");
        } else if (segment < 1 || segment > wire->segments) {
            addError(result, card, "EX segment is outside the referenced wire");
        }
    }
}

void checkFrequency(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 6) {
        addError(result, card, "FR requires four integer fields and at least two numeric fields");
        return;
    }

    int frequencyCount{};
    int steppingMode{};
    double startFrequency{};
    double frequencyStep{};
    const bool integerFieldsValid = std::ranges::all_of(card.fields.begin(), card.fields.begin() + 4,
        [](const auto& field) { return isNumber<int>(field); });
    const bool numericFieldsValid = std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
        [](const auto& field) { return isNumber<double>(field); });
    if (!integerFieldsValid || !numericFieldsValid) {
        addError(result, card, "FR contains a value with the wrong numeric type");
        return;
    }

    std::from_chars(card.fields[0].data(), card.fields[0].data() + card.fields[0].size(), steppingMode);
    std::from_chars(card.fields[1].data(), card.fields[1].data() + card.fields[1].size(), frequencyCount);
    std::from_chars(card.fields[4].data(), card.fields[4].data() + card.fields[4].size(), startFrequency);
    std::from_chars(card.fields[5].data(), card.fields[5].data() + card.fields[5].size(), frequencyStep);
    if (steppingMode != 0 && steppingMode != 1) {
        addError(result, card, "FR stepping mode must be 0 (linear) or 1 (multiplicative)");
    }
    if (frequencyCount <= 0) {
        addError(result, card, "FR frequency count must be positive");
    }
    if (startFrequency <= 0.0) {
        addError(result, card, "FR start frequency must be positive");
    }
    if (frequencyCount > 1 && frequencyStep <= 0.0) {
        addError(result, card, "FR step must be positive when multiple frequencies are requested");
    }
}

void checkGround(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.empty() || !isNumber<int>(card.fields[0])) {
        addError(result, card, "GN requires a ground-type integer");
        return;
    }
    int groundType{};
    std::from_chars(card.fields[0].data(), card.fields[0].data() + card.fields[0].size(), groundType);
    if (groundType < -1 || groundType > 2) {
        addError(result, card, "GN ground type must be -1, 0, 1, or 2");
        return;
    }
    if (groundType == 0 || groundType == 2) {
        if (card.fields.size() < 6) {
            addError(result, card, "Finite-ground GN requires four integer and two numeric fields");
            return;
        }
        double relativePermittivity{};
        double conductivity{};
        if (!isNumber<double>(card.fields[4]) || !isNumber<double>(card.fields[5])) {
            addError(result, card, "GN dielectric constant and conductivity must be numeric");
            return;
        }
        std::from_chars(card.fields[4].data(), card.fields[4].data() + card.fields[4].size(), relativePermittivity);
        std::from_chars(card.fields[5].data(), card.fields[5].data() + card.fields[5].size(), conductivity);
        if (relativePermittivity <= 0.0 || conductivity < 0.0) {
            addError(result, card, "GN dielectric constant must be positive and conductivity cannot be negative");
        }
    }
}

void checkRadiationPattern(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 8) {
        addError(result, card,
            "RP requires four integer and four numeric stepping fields; distance and normalization are optional");
        return;
    }
    const bool integerFieldsValid = std::ranges::all_of(card.fields.begin(), card.fields.begin() + 4,
        [](const auto& field) { return isNumber<int>(field); });
    const bool numericFieldsValid = std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
        [](const auto& field) { return isNumber<double>(field); });
    if (!integerFieldsValid || !numericFieldsValid) {
        addError(result, card, "RP contains a value with the wrong numeric type");
        return;
    }
    int thetaCount{};
    int phiCount{};
    double thetaStep{};
    double phiStep{};
    std::from_chars(card.fields[1].data(), card.fields[1].data() + card.fields[1].size(), thetaCount);
    std::from_chars(card.fields[2].data(), card.fields[2].data() + card.fields[2].size(), phiCount);
    std::from_chars(card.fields[6].data(), card.fields[6].data() + card.fields[6].size(), thetaStep);
    std::from_chars(card.fields[7].data(), card.fields[7].data() + card.fields[7].size(), phiStep);
    if (thetaCount <= 0 || phiCount <= 0) {
        addError(result, card, "RP theta and phi counts must be positive");
    }
    if ((thetaCount > 1 && thetaStep <= 0.0) || (phiCount > 1 && phiStep <= 0.0)) {
        addError(result, card, "RP angular steps must be positive when their count exceeds one");
    }
}

void checkExecute(const NecCard& card, ModelCheckResult& result)
{
    if (!card.fields.empty() && !isNumber<int>(card.fields[0])) {
        addError(result, card, "XQ option must be an integer");
    }
}

void checkReferenceImpedance(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.empty() || !isNumber<double>(card.fields[0])) {
        addError(result, card, "Z0/ZO reference impedance requires a numeric value in ohms");
        return;
    }
    double impedance{};
    std::from_chars(card.fields[0].data(), card.fields[0].data() + card.fields[0].size(), impedance);
    if (impedance <= 0.0)
        addError(result, card, "Z0/ZO reference impedance must be positive");
}

void checkLoad(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 7) { addError(result, card, "LD requires four integer and three numeric fields"); return; }
    int type{}, tag{}, first{}, last{};
    if (!isNumber<int>(card.fields[0]) || !isNumber<int>(card.fields[1])
        || !isNumber<int>(card.fields[2]) || !isNumber<int>(card.fields[3])) {
        addError(result, card, "LD integer fields are invalid"); return;
    }
    std::from_chars(card.fields[0].data(), card.fields[0].data() + card.fields[0].size(), type);
    std::from_chars(card.fields[1].data(), card.fields[1].data() + card.fields[1].size(), tag);
    std::from_chars(card.fields[2].data(), card.fields[2].data() + card.fields[2].size(), first);
    std::from_chars(card.fields[3].data(), card.fields[3].data() + card.fields[3].size(), last);
    if (type < 0 || type > 5) addError(result, card, "LD type must be 0 through 5");
    const auto* wire = result.model.wireByTag(tag);
    if (wire == nullptr) addError(result, card, "LD references a wire tag that does not exist");
    else if (first < 0 || last < 0 || first > wire->segments || last > wire->segments
        || (first != 0 && last != 0 && first > last)) addError(result, card, "LD segment range is invalid");
    if (!std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
            [](const auto& field) { return isNumber<double>(field); }))
        addError(result, card, "LD values must be numeric");
}

void checkTransmissionLine(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 10) { addError(result, card, "TL requires four integer and six numeric fields"); return; }
    int tag1{}, segment1{}, tag2{}, segment2{};
    int* integers[]{&tag1, &segment1, &tag2, &segment2};
    for (auto index = 0; index < 4; ++index) {
        if (!isNumber<int>(card.fields[index])) { addError(result, card, "TL integer fields are invalid"); return; }
        std::from_chars(card.fields[index].data(), card.fields[index].data() + card.fields[index].size(), *integers[index]);
    }
    const auto validEnd = [&result](int tag, int segment) {
        const auto* wire = result.model.wireByTag(tag);
        return wire != nullptr && segment >= 1 && segment <= wire->segments;
    };
    if (!validEnd(tag1, segment1) || !validEnd(tag2, segment2))
        addError(result, card, "TL endpoint references an invalid wire or segment");
    if (!std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
            [](const auto& field) { return isNumber<double>(field); })) {
        addError(result, card, "TL values must be numeric");
        return;
    }
    double impedance{};
    std::from_chars(card.fields[4].data(), card.fields[4].data() + card.fields[4].size(), impedance);
    if (impedance == 0.0) addError(result, card, "TL characteristic impedance must be nonzero");
}

void checkCardOrdering(const NecDocument& document, ModelCheckResult& result)
{
    const NecCard* lastGeometry{};
    auto geometryEnded = false;
    auto reportedMissingEnd = false;
    auto controlSeen = false;
    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::GeometryWire
            || card.kind == NecCardKind::GeometryScale) {
            if (card.kind == NecCardKind::GeometryWire) lastGeometry = &card;
            if (geometryEnded) {
                addError(result, card, card.mnemonic + " geometry cards must appear before GE");
            }
            if (controlSeen) {
                addError(result, card, card.mnemonic
                    + " geometry cards must precede GN, EX, FR, and other control cards");
            }
            continue;
        }
        if (card.kind == NecCardKind::GeometryEnd) {
            if (geometryEnded) {
                addError(result, card, "Only one GE geometry-end card is allowed");
            }
            geometryEnded = true;
            continue;
        }
        const auto isControlCard = card.kind == NecCardKind::Excitation
            || card.kind == NecCardKind::Load
            || card.kind == NecCardKind::Ground
            || card.kind == NecCardKind::Frequency
            || card.kind == NecCardKind::RadiationPattern
            || card.kind == NecCardKind::Execute
            || card.kind == NecCardKind::TransmissionLine
            || card.kind == NecCardKind::Network
            || card.kind == NecCardKind::ReferenceImpedance
            || card.kind == NecCardKind::End;
        if (isControlCard && lastGeometry != nullptr && !geometryEnded && !reportedMissingEnd) {
            addError(result, card, card.mnemonic
                + " appears before GE; terminate GW geometry with a GE card before control cards");
            reportedMissingEnd = true;
        }
        controlSeen = controlSeen || isControlCard;
    }
    if (lastGeometry != nullptr && !geometryEnded && !reportedMissingEnd) {
        addError(result, *lastGeometry, "Geometry section requires a GE card after the final GW card");
    }
}

}

auto ModelCheckResult::errorCount() const noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    }));
}

auto ModelCheckResult::warningCount() const noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Warning;
    }));
}

auto NecModelChecker::check(const NecDocument& document) const -> ModelCheckResult
{
    auto conversion = NecModelConverter{}.convert(document);
    ModelCheckResult result{std::move(conversion.model), {}};
    for (auto& issue : conversion.issues) {
        result.diagnostics.push_back({DiagnosticSeverity::Error, issue.lineNumber,
            std::move(issue.message), "Geometry"});
    }

    checkCardOrdering(document, result);

    for (const auto& card : document.cards()) {
        switch (card.kind) {
        case NecCardKind::Excitation:
            checkExcitation(card, result);
            break;
        case NecCardKind::Frequency:
            checkFrequency(card, result);
            break;
        case NecCardKind::Ground:
            checkGround(card, result);
            break;
        case NecCardKind::RadiationPattern:
            checkRadiationPattern(card, result);
            break;
        case NecCardKind::Execute:
            checkExecute(card, result);
            break;
        case NecCardKind::Load:
            checkLoad(card, result);
            break;
        case NecCardKind::TransmissionLine:
            checkTransmissionLine(card, result);
            break;
        case NecCardKind::ReferenceImpedance:
            checkReferenceImpedance(card, result);
            break;
        case NecCardKind::Unknown:
            result.diagnostics.push_back({DiagnosticSeverity::Warning, card.lineNumber,
                "Unknown card " + card.mnemonic + "; the source line will be preserved",
                "Compatibility"});
            break;
        default:
            break;
        }
    }
    const auto setup = NecSetupConverter{}.convert(document);
    if (result.model.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete model: no valid GW wire geometry", "Readiness"});
    }
    if (!setup.frequency) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported FR frequency definition", "Readiness"});
    }
    if (setup.excitations.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported EX voltage source", "Readiness"});
    }
    checkStaticAdequacy(setup, result);
    std::ranges::sort(result.diagnostics, {}, &ModelDiagnostic::lineNumber);
    return result;
}

}
