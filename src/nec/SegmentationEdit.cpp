#include "nec/SegmentationEdit.h"

#include "nec/NecCardFieldEditor.h"
#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/NecSourceEditor.h"
#include "nec/NecSymbolResolver.h"
#include "nec/SourcePositions.h"

#include <algorithm>
#include <map>
#include <set>

namespace necwb::nec {
namespace {

// Field indices of the segment references, after the mnemonic.
constexpr std::size_t GwSegments = 1;
constexpr std::size_t ExSegment = 2;
constexpr std::size_t LdFirstSegment = 2;
constexpr std::size_t LdLastSegment = 3;
constexpr std::size_t PortSegment1 = 1;
constexpr std::size_t PortSegment2 = 3;

auto joined(const std::vector<std::string>& parts) -> std::string
{
    std::string text;
    for (const auto& part : parts) {
        if (!text.empty()) text += "; ";
        text += part;
    }
    return text;
}

}

auto SegmentationEdit::skippedReason(int wireTag) const -> const std::string*
{
    const auto found = std::ranges::find(skipped, wireTag, &SkippedWire::wireTag);
    return found == skipped.end() ? nullptr : &found->reason;
}

auto planSegmentationEdit(std::string_view source, const model::AntennaModel& model,
    const model::ModelSetup& setup, double maximumFrequencyMHz,
    const model::SegmentationSettings& settings) -> SegmentationEdit
{
    SegmentationEdit edit;
    edit.source = std::string(source);
    edit.proposal = model::proposeSegmentation(model, setup.excitations, maximumFrequencyMHz,
        settings, setup.loads, setup.transmissionLines, setup.networks);
    auto& proposal = edit.proposal;
    const auto authored = NecParser{}.parse(source);
    const auto cardText = [&authored](std::size_t line) -> std::string_view {
        const auto found = std::ranges::find(authored.cards(), line, &NecCard::lineNumber);
        return found == authored.cards().end() ? std::string_view{} : found->sourceText;
    };
    // The field as written when it is a symbol or an expression, or none.
    const auto symbolic = [&cardText](std::size_t line, std::size_t field) -> std::optional<std::string> {
        const auto text = cardText(line);
        auto written = necCardFieldText(text, field);
        if (written.empty() || necCardFieldIsNumeric(text, field)) return std::nullopt;
        return written;
    };

    std::set<int> changing;
    for (const auto& wire : proposal.wires)
        if (wire.oldSegments != wire.newSegments) changing.insert(wire.wireTag);

    edit.excitationMarks.reserve(setup.excitations.size());
    for (const auto& excitation : setup.excitations)
        edit.excitationMarks.push_back(sourcePositionMark(cardText(excitation.sourceLine)));

    // Why each changing wire must be left alone.
    std::map<int, std::vector<std::string>> reasons;
    const auto protect = [&](int tag, std::size_t line, std::size_t field, int before, int after,
                             std::string_view card) {
        if (!changing.contains(tag) || before == after) return;
        if (const auto written = symbolic(line, field))
            reasons[tag].push_back(std::string(card) + " on line " + std::to_string(line)
                + " gives segment " + std::to_string(before) + " as " + *written);
    };
    for (const auto& wire : proposal.wires) {
        if (!changing.contains(wire.wireTag)) continue;
        const auto* original = model.wireByTag(wire.wireTag);
        if (original == nullptr) continue;
        if (const auto written = symbolic(original->sourceLine, GwSegments))
            reasons[wire.wireTag].push_back("its segment count is " + *written + " on line "
                + std::to_string(original->sourceLine));
    }
    for (std::size_t index = 0; index < setup.excitations.size(); ++index) {
        // A source placed along its wire follows its mark, so its field is never in the way.
        if (edit.excitationMarks[index]) continue;
        const auto& before = setup.excitations[index];
        protect(before.wireTag, before.sourceLine, ExSegment, before.segment,
            proposal.remappedExcitations[index].segment, "EX");
    }
    for (std::size_t index = 0; index < setup.loads.size(); ++index) {
        const auto& before = setup.loads[index];
        const auto& after = proposal.remappedLoads[index];
        protect(before.wireTag, before.sourceLine, LdFirstSegment, before.firstSegment,
            after.firstSegment, "LD");
        protect(before.wireTag, before.sourceLine, LdLastSegment, before.lastSegment,
            after.lastSegment, "LD");
    }
    const auto protectPorts = [&protect](const auto& before, const auto& after, std::string_view card) {
        protect(before.wireTag1, before.sourceLine, PortSegment1, before.segment1, after.segment1, card);
        protect(before.wireTag2, before.sourceLine, PortSegment2, before.segment2, after.segment2, card);
    };
    for (std::size_t index = 0; index < setup.transmissionLines.size(); ++index)
        protectPorts(setup.transmissionLines[index], proposal.remappedTransmissionLines[index], "TL");
    for (std::size_t index = 0; index < setup.networks.size(); ++index)
        protectPorts(setup.networks[index], proposal.remappedNetworks[index], "NT");
    for (const auto& [tag, wireReasons] : reasons) {
        edit.skipped.push_back({tag, joined(wireReasons)});
        changing.erase(tag);
    }

    // The fields to write, by line, and the attachments as the edited deck gives them.
    std::map<std::size_t, std::vector<NecFieldReplacement>> writes;
    const auto write = [&writes](std::size_t line, std::size_t field, int value) {
        writes[line].push_back({field, std::to_string(value)});
    };
    for (const auto& wire : proposal.wires) {
        const auto* original = model.wireByTag(wire.wireTag);
        if (original != nullptr && changing.contains(wire.wireTag))
            write(original->sourceLine, GwSegments, wire.newSegments);
    }
    for (std::size_t index = 0; index < setup.excitations.size(); ++index) {
        const auto& before = setup.excitations[index];
        auto& after = proposal.remappedExcitations[index];
        // A marked source is placed below, from the edited deck.
        if (edit.excitationMarks[index] || !changing.contains(before.wireTag)) after = before;
        else if (after.segment != before.segment) write(before.sourceLine, ExSegment, after.segment);
    }
    for (std::size_t index = 0; index < setup.loads.size(); ++index) {
        const auto& before = setup.loads[index];
        auto& after = proposal.remappedLoads[index];
        if (!changing.contains(before.wireTag)) {
            after = before;
            continue;
        }
        if (after.firstSegment != before.firstSegment)
            write(before.sourceLine, LdFirstSegment, after.firstSegment);
        if (after.lastSegment != before.lastSegment)
            write(before.sourceLine, LdLastSegment, after.lastSegment);
    }
    const auto writePorts = [&](const auto& before, auto& after) {
        if (!changing.contains(before.wireTag1)) after.segment1 = before.segment1;
        else if (after.segment1 != before.segment1)
            write(before.sourceLine, PortSegment1, after.segment1);
        if (!changing.contains(before.wireTag2)) after.segment2 = before.segment2;
        else if (after.segment2 != before.segment2)
            write(before.sourceLine, PortSegment2, after.segment2);
    };
    for (std::size_t index = 0; index < setup.transmissionLines.size(); ++index)
        writePorts(setup.transmissionLines[index], proposal.remappedTransmissionLines[index]);
    for (std::size_t index = 0; index < setup.networks.size(); ++index)
        writePorts(setup.networks[index], proposal.remappedNetworks[index]);

    auto edited = std::string(source);
    for (const auto& [line, replacements] : writes) {
        const auto updatedCard = replaceNecCardFields(cardText(line), replacements);
        auto updated = updatedCard ? replaceSourceLine(edited, line, *updatedCard) : std::nullopt;
        if (!updated) {
            edit.error = "The segment fields on line " + std::to_string(line)
                + " could not be updated safely";
            return edit;
        }
        edited = std::move(*updated);
    }
    // Marked sources go where their marks fall on the edited deck, the placement resolving it
    // uses; their segment fields are written as that segment, as every edit writes them.
    edited = refreshSourcePositions(edited);
    if (std::ranges::any_of(edit.excitationMarks, [](const auto& mark) { return mark.has_value(); })) {
        const auto resolution = NecSymbolResolver{}.resolve(edited);
        if (resolution.ok()) {
            const auto resolved = NecParser{}.parse(resolution.resolvedSource);
            const auto placements = placeMarkedSources(resolved,
                NecModelConverter{}.convert(resolved).model);
            for (std::size_t index = 0; index < setup.excitations.size(); ++index) {
                if (!edit.excitationMarks[index]) continue;
                const auto placed = std::ranges::find(placements,
                    setup.excitations[index].sourceLine, &SourcePlacement::lineNumber);
                if (placed != placements.end() && placed->placement)
                    proposal.remappedExcitations[index].segment = placed->placement->segment;
            }
        }
    }
    edit.changed = edited != source;
    edit.source = std::move(edited);
    return edit;
}

}
