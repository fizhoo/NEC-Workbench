#include "analysis/SegmentationConvergence.h"

#include "analysis/SolverInput.h"

#include "model/ModelSetup.h"
#include "nec/NecModelConverter.h"
#include "nec/DeckGeometryUnits.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
#include "nec/NecWriter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <sstream>
#include <unordered_map>

namespace necwb::analysis {
namespace {

auto remappedSegment(int segment, int oldCount, int newCount) -> int
{
    if (segment <= 0) return segment;
    const auto position = (std::clamp(segment, 1, oldCount) - 0.5) / oldCount;
    return std::clamp(static_cast<int>(std::floor(position * newCount)) + 1, 1, newCount);
}

auto centeredExcitation(const model::Excitation& excitation, int segments) -> bool
{
    return segments % 2 == 1 && excitation.segment == (segments + 1) / 2;
}

}

auto prepareSegmentationConvergenceInput(std::string_view source, double frequencyMHz,
    double segmentScale) -> SegmentationConvergenceDeck
{
    SegmentationConvergenceDeck result;
    if (frequencyMHz <= 0.0 || segmentScale < 1.0) {
        result.error = "Frequency must be positive and segment scale must be at least 1.0.";
        return result;
    }

    const auto document = nec::NecParser{}.parse(normalizeSolverDeck(source));
    const auto conversion = nec::NecModelConverter{}.convert(document);
    const auto setup = nec::NecSetupConverter{}.convert(document);
    if (conversion.model.wires().empty()) {
        result.error = "The model contains no supported wire geometry.";
        return result;
    }
    if (std::ranges::any_of(document.cards(), [](const auto& card) {
            return card.kind == nec::NecCardKind::Network;
        })) {
        result.error = "NT network cards cannot yet be remapped safely for convergence testing.";
        return result;
    }
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Excitation
            && std::ranges::none_of(setup.excitations, [line = card.lineNumber](const auto& value) {
                   return value.sourceLine == line;
               })) {
            result.error = "The model contains an unsupported EX card type.";
            return result;
        }
    }
    if (std::ranges::any_of(setup.loads, [](const auto& load) {
            return load.wireTag == 0 && (load.firstSegment > 0 || load.lastSegment > 0);
        })) {
        result.error = "A global LD card with a segment subset cannot be remapped across differently refined wires.";
        return result;
    }

    std::unordered_map<int, int> segmentCounts;
    const auto wavelengthMeters = 299.792458 / frequencyMHz;
    for (const auto& wire : conversion.model.wires()) {
        const auto scaled = std::ceil(static_cast<double>(wire.segments) * segmentScale);
        auto count = static_cast<int>(std::clamp(scaled, 1.0,
            static_cast<double>(std::numeric_limits<int>::max())));
        const auto needsCenteredSegment = std::ranges::any_of(setup.excitations,
            [&wire](const auto& excitation) {
                return excitation.wireTag == wire.tag
                    && centeredExcitation(excitation, wire.segments);
            });
        if (needsCenteredSegment && count % 2 == 0
            && count < std::numeric_limits<int>::max()) ++count;
        segmentCounts.emplace(wire.tag, count);
        result.totalSegments += count;
        const auto length = std::sqrt(std::pow(wire.end.x - wire.start.x, 2)
            + std::pow(wire.end.y - wire.start.y, 2)
            + std::pow(wire.end.z - wire.start.z, 2));
        const auto segmentLength = length / count;
        if (result.warning.empty() && segmentLength < 0.001 * wavelengthMeters) {
            result.warning = "Refinement produces segments shorter than 0.001 wavelength.";
        }
        if (result.warning.empty() && wire.radius > 0.0
            && segmentLength / (2.0 * wire.radius) <= 4.0) {
            result.warning = "Refinement produces a segment-length/diameter ratio of 4 or less.";
        }
    }

    const auto remap = [&conversion, &segmentCounts](int wireTag, int segment) {
        const auto* wire = conversion.model.wireByTag(wireTag);
        const auto found = segmentCounts.find(wireTag);
        return wire == nullptr || found == segmentCounts.end()
            ? segment : remappedSegment(segment, wire->segments, found->second);
    };

    const nec::NecWriter writer;
    std::ostringstream output;
    auto wroteFrequency = false;
    auto hasRequest = false;
    const auto append = [&output, &document](const std::string& line) {
        if (output.tellp() > 0) output << document.lineEnding();
        output << line;
    };
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::GeometryWire) {
            const auto wire = std::ranges::find(conversion.model.wires(), card.lineNumber,
                &model::Wire::sourceLine);
            if (wire == conversion.model.wires().end()) {
                result.error = "A GW card could not be converted for convergence testing.";
                return result;
            }
            auto updated = *wire;
            updated.segments = segmentCounts.at(updated.tag);
            append(writer.writeWireCard(updated,
                nec::geometryScaleForLine(document, card.lineNumber)));
        } else if (card.kind == nec::NecCardKind::Excitation) {
            const auto excitation = std::ranges::find(setup.excitations, card.lineNumber,
                &model::Excitation::sourceLine);
            auto updated = *excitation;
            updated.segment = remap(updated.wireTag, updated.segment);
            append(writer.writeExcitationCard(updated));
        } else if (card.kind == nec::NecCardKind::Load) {
            const auto load = std::ranges::find(setup.loads, card.lineNumber,
                &model::LoadDefinition::sourceLine);
            if (load == setup.loads.end()) {
                result.error = "An LD card could not be converted for convergence testing.";
                return result;
            }
            auto updated = *load;
            updated.firstSegment = remap(updated.wireTag, updated.firstSegment);
            updated.lastSegment = remap(updated.wireTag, updated.lastSegment);
            append(writer.writeLoadCard(updated));
        } else if (card.kind == nec::NecCardKind::TransmissionLine) {
            const auto line = std::ranges::find(setup.transmissionLines, card.lineNumber,
                &model::TransmissionLineDefinition::sourceLine);
            if (line == setup.transmissionLines.end()) {
                result.error = "A TL card could not be converted for convergence testing.";
                return result;
            }
            auto updated = *line;
            updated.segment1 = remap(updated.wireTag1, updated.segment1);
            updated.segment2 = remap(updated.wireTag2, updated.segment2);
            append(writer.writeTransmissionLineCard(updated));
        } else if (card.kind == nec::NecCardKind::Frequency) {
            if (!wroteFrequency) {
                append(writer.writeFrequencyCard({.steppingMode = 0, .count = 1,
                    .startMHz = frequencyMHz, .step = 0.0}));
                wroteFrequency = true;
            }
        } else if (card.kind == nec::NecCardKind::Execute
            || card.kind == nec::NecCardKind::RadiationPattern) {
            hasRequest = true;
            append(card.sourceText);
        } else if (card.kind == nec::NecCardKind::End) {
            if (!wroteFrequency) {
                append(writer.writeFrequencyCard({.steppingMode = 0, .count = 1,
                    .startMHz = frequencyMHz, .step = 0.0}));
                wroteFrequency = true;
            }
            if (!hasRequest) append("XQ 0");
            append(card.sourceText);
        } else {
            append(card.sourceText);
        }
    }
    if (!wroteFrequency) append(writer.writeFrequencyCard(
        {.steppingMode = 0, .count = 1, .startMHz = frequencyMHz, .step = 0.0}));
    result.deck = output.str();
    if (document.hasFinalLineEnding() && !result.deck.empty()) result.deck += document.lineEnding();
    return result;
}

}
