#include "nec/NecModelChecker.h"

#include "nec/NecModelConverter.h"
#include "nec/NecSetupConverter.h"

#include <algorithm>
#include <charconv>
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
    result.diagnostics.push_back({DiagnosticSeverity::Error, card.lineNumber, std::move(message)});
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
    if (card.fields.size() < 10) {
        addError(result, card, "RP requires four integer and six numeric fields");
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
            [](const auto& field) { return isNumber<double>(field); }))
        addError(result, card, "TL values must be numeric");
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
        result.diagnostics.push_back({DiagnosticSeverity::Error, issue.lineNumber, std::move(issue.message)});
    }

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
        case NecCardKind::Unknown:
            result.diagnostics.push_back({DiagnosticSeverity::Warning, card.lineNumber,
                "Unknown card " + card.mnemonic + "; the source line will be preserved"});
            break;
        default:
            break;
        }
    }
    const auto setup = NecSetupConverter{}.convert(document);
    if (result.model.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete model: no valid GW wire geometry"});
    }
    if (!setup.frequency) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported FR frequency definition"});
    }
    if (setup.excitations.empty()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0,
            "Incomplete analysis setup: no supported EX voltage source"});
    }
    std::ranges::sort(result.diagnostics, {}, &ModelDiagnostic::lineNumber);
    return result;
}

}
