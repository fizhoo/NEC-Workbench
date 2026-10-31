#include "nec/NecModelChecker.h"

#include "nec/NecCardCatalog.h"
#include "nec/NecModelConverter.h"
#include "nec/NecSetupConverter.h"
#include "nec/ParseNumber.h"
#include "model/WireGeometry.h"

#include <algorithm>
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
    return parseNumber(text, value);
}

void addError(ModelCheckResult& result, const NecCard& card, std::string message)
{
    result.diagnostics.push_back(
        {DiagnosticSeverity::Error, card.lineNumber, std::move(message), "Card validation"});
}

void addSequenceDiagnostic(ModelCheckResult& result, DiagnosticSeverity severity,
    const NecCard& card, std::string message)
{
    result.diagnostics.push_back(
        {severity, card.lineNumber, std::move(message), "Control sequence"});
}

void addAdequacyWarning(ModelCheckResult& result, std::size_t lineNumber, std::string message)
{
    result.diagnostics.push_back(
        {DiagnosticSeverity::Warning, lineNumber, std::move(message), "Model adequacy"});
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
        const auto segmentLength = model::wireMinimumSegmentLength(wire);
        const auto wavelengthRatio = segmentLength / wavelengthMeters;
        const auto diameterRatio = segmentLength / (2.0 * model::wireMaximumRadius(wire));
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
        if (excitation.type != 0) continue;
        const auto reference = model::referencedSegment(result.model, excitation.wireTag,
            excitation.segment);
        if (!reference || reference->wire == nullptr) continue;
        const auto* wire = reference->wire;
        if (wire->segments % 2 != 0) continue;
        if (reference->segment == wire->segments / 2
            || reference->segment == wire->segments / 2 + 1) {
            addAdequacyWarning(result, excitation.sourceLine,
                "Center-fed wire tag " + std::to_string(wire->tag)
                    + " has an even segment count; no segment lies exactly at its center");
        }
    }
}

void checkExcitation(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 5) {
        addError(result, card, "EX requires four integer fields and at least one numeric field");
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
    parseNumber(card.fields[0], excitationType);
    parseNumber(card.fields[1], wireTag);
    parseNumber(card.fields[2], segment);
    if (excitationType == 0) {
        // Tag 0 selects an absolute segment number; other tags count through every wire
        // that carries them.
        const auto count = model::referencedSegmentCount(result.model, wireTag);
        if (wireTag != 0 && count == 0) {
            addError(result, card, "EX references a wire tag that does not exist");
        } else if (segment < 1 || segment > count) {
            addError(result, card, wireTag == 0
                ? "EX absolute segment number is outside the structure"
                : "EX segment is outside the referenced wire");
        }
    }
}

void checkFrequency(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 5) {
        addError(result, card, "FR requires four integer fields and a start frequency");
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

    parseNumber(card.fields[0], steppingMode);
    parseNumber(card.fields[1], frequencyCount);
    parseNumber(card.fields[4], startFrequency);
    if (card.fields.size() >= 6)
        parseNumber(card.fields[5], frequencyStep);
    if (steppingMode != 0 && steppingMode != 1) {
        addError(result, card, "FR stepping mode must be 0 (linear) or 1 (multiplicative)");
    }
    if (frequencyCount < 0) {
        addError(result, card, "FR frequency count cannot be negative");
    }
    if (startFrequency <= 0.0) {
        addError(result, card, "FR start frequency must be positive");
    }
    if (frequencyCount > 1 && card.fields.size() < 6) {
        addError(result, card, "FR increment is required when multiple frequencies are requested");
    } else if (frequencyCount > 1 && frequencyStep <= 0.0) {
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
    parseNumber(card.fields[0], groundType);
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
        parseNumber(card.fields[4], relativePermittivity);
        parseNumber(card.fields[5], conductivity);
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
    parseNumber(card.fields[1], thetaCount);
    parseNumber(card.fields[2], phiCount);
    parseNumber(card.fields[6], thetaStep);
    parseNumber(card.fields[7], phiStep);
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
    parseNumber(card.fields[0], impedance);
    if (impedance <= 0.0)
        addError(result, card, "Z0/ZO reference impedance must be positive");
}

void checkLoad(const NecCard& card, CompatibilityProfile profile, ModelCheckResult& result)
{
    if (card.fields.size() < 5) { addError(result, card, "LD requires four integer fields and at least one numeric field"); return; }
    int type{}, tag{}, first{}, last{};
    if (!isNumber<int>(card.fields[0]) || !isNumber<int>(card.fields[1])
        || !isNumber<int>(card.fields[2]) || !isNumber<int>(card.fields[3])) {
        addError(result, card, "LD integer fields are invalid"); return;
    }
    parseNumber(card.fields[0], type);
    parseNumber(card.fields[1], tag);
    parseNumber(card.fields[2], first);
    parseNumber(card.fields[3], last);
    if (type < 0 || type > 6) {
        addError(result, card, "LD type must be 0 through 5 for NEC-2, or type 6 for a supported extended backend");
    } else if (type == 6) {
        if (profile == CompatibilityProfile::OriginalNec2) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, card.lineNumber,
                "LD type 6 is a 4nec2 LC-trap extension. Original NEC-2 and NEC2C support "
                "LD types 0 through 5 and cannot process this card as written.",
                "Compatibility"});
        } else if (profile == CompatibilityProfile::FourNec2) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, card.lineNumber,
                "LD type 6 is recognized as a 4nec2 LC-trap extension, but NEC Workbench does "
                "not yet convert it for direct NEC2dXS execution. Select OpenNEC or replace "
                "the trap with standard NEC-2 loading.",
                "Compatibility"});
        }
    }
    // LD counts segments through every wire with its tag, or absolute segments for tag 0.
    const auto count = model::referencedSegmentCount(result.model, tag);
    if (tag != 0 && count == 0) addError(result, card, "LD references a wire tag that does not exist");
    else if (first < 0 || last < 0 || first > count || last > count
        || (first != 0 && last != 0 && first > last)) addError(result, card, "LD segment range is invalid");
    if (!std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
            [](const auto& field) { return isNumber<double>(field); }))
        addError(result, card, "LD values must be numeric");
}

void checkTransmissionLine(const NecCard& card, ModelCheckResult& result)
{
    if (card.fields.size() < 5) { addError(result, card, "TL requires four integer fields and an impedance"); return; }
    int tag1{}, segment1{}, tag2{}, segment2{};
    int* integers[]{&tag1, &segment1, &tag2, &segment2};
    for (auto index = 0; index < 4; ++index) {
        if (!isNumber<int>(card.fields[index])) { addError(result, card, "TL integer fields are invalid"); return; }
        parseNumber(card.fields[index], *integers[index]);
    }
    const auto validEnd = [&result](int tag, int segment) {
        return model::referencedSegment(result.model, tag, segment).has_value();
    };
    if (!validEnd(tag1, segment1) || !validEnd(tag2, segment2))
        addError(result, card, "TL endpoint references an invalid wire or segment");
    if (!std::ranges::all_of(card.fields.begin() + 4, card.fields.end(),
            [](const auto& field) { return isNumber<double>(field); })) {
        addError(result, card, "TL values must be numeric");
        return;
    }
    double impedance{};
    parseNumber(card.fields[4], impedance);
    if (impedance == 0.0) addError(result, card, "TL characteristic impedance must be nonzero");
}

void checkGenericKnownCard(const NecCard& card, ModelCheckResult& result)
{
    const auto* spec = findNecCardSpec(card.mnemonic);
    if (spec == nullptr) return;
    const auto maximumFields = spec->integerFieldCount + spec->numericFieldCount;
    if (maximumFields > 0 && card.fields.size() > maximumFields) {
        addError(result, card, card.mnemonic + " has more fields than the NEC-2 card format allows");
        return;
    }
    for (std::size_t index = 0; index < card.fields.size(); ++index) {
        const auto valid = index < spec->integerFieldCount
            ? isNumber<int>(card.fields[index]) : isNumber<double>(card.fields[index]);
        if (!valid) {
            addError(result, card, card.mnemonic + " field " + std::to_string(index + 1)
                + " has the wrong numeric type");
            return;
        }
    }
}

void checkCardOrdering(const NecDocument& document, ModelCheckResult& result)
{
    const NecCard* lastGeometry{};
    auto geometryEnded = false;
    auto reportedMissingEnd = false;
    auto controlSeen = false;
    for (const auto& card : document.cards()) {
        if (isGeometryCard(card.kind)) {
            lastGeometry = &card;
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
        const auto controlCard = isControlCard(card.kind);
        if (controlCard && lastGeometry != nullptr && !geometryEnded && !reportedMissingEnd) {
            addError(result, card, card.mnemonic
                + " appears before GE; terminate GW geometry with a GE card before control cards");
            reportedMissingEnd = true;
        }
        controlSeen = controlSeen || controlCard;
    }
    if (lastGeometry != nullptr && !geometryEnded && !reportedMissingEnd) {
        addError(result, *lastGeometry, "Geometry section requires a GE card after the final GW card");
    }
}

void checkDeckStructure(const NecDocument& document, ModelCheckResult& result)
{
    const NecCard* lastCard{};
    auto leadingSection = true;
    auto commentBlockOpen = false;
    auto endSeen = false;

    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Blank) continue;
        lastCard = &card;

        if (leadingSection) {
            if (card.mnemonic == "CM") {
                commentBlockOpen = true;
            } else if (card.mnemonic == "CE") {
                commentBlockOpen = false;
                leadingSection = false;
            } else {
                if (commentBlockOpen) {
                    addError(result, card, card.mnemonic
                        + " appears before CE; terminate the leading CM comment block with CE");
                    commentBlockOpen = false;
                }
                leadingSection = false;
            }
        }

        if (card.kind == NecCardKind::End) {
            if (endSeen) addError(result, card, "Only one EN end card is allowed");
            endSeen = true;
        } else if (endSeen) {
            addError(result, card, "EN must be the final nonblank card in the deck");
        }
    }

    if (!endSeen) {
        if (lastCard != nullptr) {
            addError(result, *lastCard, "Deck requires a final EN end card");
        } else {
            result.diagnostics.push_back({DiagnosticSeverity::Error, 0,
                "Deck requires a final EN end card", "Card validation"});
        }
    }
}

auto changesCalculationState(const NecCard& card) -> bool
{
    switch (card.kind) {
    case NecCardKind::Excitation:
    case NecCardKind::Load:
    case NecCardKind::Ground:
    case NecCardKind::Frequency:
    case NecCardKind::TransmissionLine:
    case NecCardKind::Network:
        return true;
    case NecCardKind::ControlOther:
        return card.mnemonic == "EK" || card.mnemonic == "KH" || card.mnemonic == "PQ"
            || card.mnemonic == "PT";
    default:
        return false;
    }
}

auto isNearFieldRequest(const NecCard& card) -> bool
{
    return card.kind == NecCardKind::ControlOther
        && (card.mnemonic == "NE" || card.mnemonic == "NH");
}

void checkControlSequence(const NecDocument& document, ModelCheckResult& result)
{
    auto excitationDefined = false;
    auto executionOccurred = false;
    auto frequencyCount = 1;
    auto multiFrequencyLoopCompleted = false;
    std::string lastExecution;
    std::vector<const NecCard*> settingsAwaitingExecution;
    std::vector<const NecCard*> radiationSettingsAwaitingPattern;
    std::vector<const NecCard*> nearFieldsAwaitingExecution;

    for (const auto& card : document.cards()) {
        if (card.kind == NecCardKind::Excitation) excitationDefined = true;
        if (card.kind == NecCardKind::Frequency) {
            frequencyCount = 1;
            if (card.fields.size() > 1 && isNumber<int>(card.fields[1])) {
                parseNumber(card.fields[1], frequencyCount);
                if (frequencyCount == 0) frequencyCount = 1;
            }
            multiFrequencyLoopCompleted = false;
        }

        if (changesCalculationState(card)) {
            settingsAwaitingExecution.push_back(&card);
        }
        if (card.kind == NecCardKind::ControlOther && card.mnemonic == "GD") {
            radiationSettingsAwaitingPattern.push_back(&card);
        }

        const auto nearField = isNearFieldRequest(card);
        if (nearField && frequencyCount > 1) {
            nearFieldsAwaitingExecution.push_back(&card);
        }
        const auto executionRequest = card.kind == NecCardKind::Execute
            || card.kind == NecCardKind::RadiationPattern
            || (nearField && frequencyCount == 1);
        if (!executionRequest) continue;

        if (!excitationDefined) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Error, card,
                card.mnemonic + " requests execution before an EX excitation is defined");
        }

        const auto dividerAfterFieldRequest = card.kind == NecCardKind::Execute
            && (lastExecution == "RP" || lastExecution == "NE" || lastExecution == "NH")
            && settingsAwaitingExecution.empty() && nearFieldsAwaitingExecution.empty();
        if (frequencyCount > 1 && multiFrequencyLoopCompleted && !dividerAfterFieldRequest) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Warning, card,
                card.mnemonic
                    + " follows a completed multi-frequency request without a repeated FR; "
                      "NEC-2 evaluates this request at the final frequency only");
        }

        settingsAwaitingExecution.clear();
        nearFieldsAwaitingExecution.clear();
        if (card.kind == NecCardKind::RadiationPattern) {
            radiationSettingsAwaitingPattern.clear();
        }
        executionOccurred = true;
        if (frequencyCount > 1) multiFrequencyLoopCompleted = true;
        lastExecution = card.mnemonic;
    }

    if (executionOccurred) {
        for (const auto* card : settingsAwaitingExecution) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Error, *card,
                card->mnemonic
                    + " changes calculation state after the final execution request; "
                      "move it before that request or add a later execution request");
        }
        for (const auto* card : radiationSettingsAwaitingPattern) {
            addSequenceDiagnostic(result, DiagnosticSeverity::Error, *card,
                "GD changes radiated-field parameters but no later RP request uses them");
        }
    }
    for (const auto* card : nearFieldsAwaitingExecution) {
        addSequenceDiagnostic(result, DiagnosticSeverity::Error, *card,
            card->mnemonic
                + " does not execute during automatic frequency stepping; "
                  "follow it with XQ or RP");
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

auto ModelCheckResult::informationCount() const noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Information;
    }));
}

auto compatibilityProfileForBackend(std::string_view backendId) noexcept
    -> CompatibilityProfile
{
    if (backendId == "opennec") return CompatibilityProfile::OpenNec;
    if (backendId == "nec2dxs") return CompatibilityProfile::FourNec2;
    return CompatibilityProfile::OriginalNec2;
}

auto compatibilityProfileName(CompatibilityProfile profile) noexcept -> std::string_view
{
    switch (profile) {
    case CompatibilityProfile::FourNec2:
        return "4nec2 NEC2dXS";
    case CompatibilityProfile::OpenNec:
        return "OpenNEC";
    case CompatibilityProfile::OriginalNec2:
    default:
        return "Original NEC-2 / NEC2C";
    }
}

auto NecModelChecker::check(const NecDocument& document, ModelCheckOptions options) const
    -> ModelCheckResult
{
    auto conversion = NecModelConverter{}.convert(document);
    ModelCheckResult result{std::move(conversion.model), {}};
    for (auto& issue : conversion.issues) {
        result.diagnostics.push_back({DiagnosticSeverity::Error, issue.lineNumber,
            std::move(issue.message), "Geometry"});
    }

    checkDeckStructure(document, result);
    checkCardOrdering(document, result);
    checkControlSequence(document, result);

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
            checkLoad(card, options.compatibility, result);
            break;
        case NecCardKind::TransmissionLine:
            checkTransmissionLine(card, result);
            break;
        case NecCardKind::ReferenceImpedance:
            checkReferenceImpedance(card, result);
            break;
        case NecCardKind::GeometryOther:
        case NecCardKind::ControlOther:
        case NecCardKind::Network:
            if (std::ranges::none_of(result.diagnostics, [&card](const auto& diagnostic) {
                    return diagnostic.severity == DiagnosticSeverity::Error
                        && diagnostic.lineNumber == card.lineNumber;
                })) {
                checkGenericKnownCard(card, result);
            }
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
    for (const auto& card : document.cards()) {
        if (card.mnemonic != "GH") continue;
        if (std::ranges::any_of(result.model.unrenderedSegments(),
                [&card](const auto& spiral) { return spiral.sourceLine == card.lineNumber; })) {
            result.diagnostics.push_back({DiagnosticSeverity::Warning, card.lineNumber,
                "Flat-spiral GH (zero axial length) is not drawn, and many NEC-2 solvers do not "
                "handle it reliably; check the results carefully",
                "Compatibility"});
        }
    }
    const auto setup = NecSetupConverter{}.convert(document);
    if (result.model.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete model: no valid geometry", "Readiness"});
    }
    if (!setup.frequency) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported FR frequency definition", "Readiness"});
    }
    if (setup.excitations.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported EX voltage source", "Readiness"});
    }
    if (options.expandedSymbolCount != 0) {
        result.diagnostics.push_back({DiagnosticSeverity::Information, 0,
            std::to_string(options.expandedSymbolCount)
                + (options.expandedSymbolCount == 1
                        ? " SY symbol was resolved and expanded in the generated solver deck"
                        : " SY symbols were resolved and expanded in the generated solver deck"),
            "Compatibility"});
    }
    checkStaticAdequacy(setup, result);
    std::ranges::sort(result.diagnostics, {}, &ModelDiagnostic::lineNumber);
    return result;
}

}
