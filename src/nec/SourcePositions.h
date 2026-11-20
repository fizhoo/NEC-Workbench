#pragma once

#include "model/AntennaModel.h"
#include "model/SegmentPlacement.h"
#include "nec/NecDocument.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace necwb::nec {

// A voltage source placed along its wire rather than on a fixed segment. NEC Workbench
// writes the request in the EX card's 4nec2 ' comment, after the segment it currently gives:
//
//     EX 0 1 6 0 1 0 ' necwb:at=50%
//
// NEC Workbench puts the source on the segment at that point every time it resolves the
// deck, so the segment follows the wire's segmentation; other programs read the comment as a
// comment and use the segment written, which NEC Workbench keeps current when it edits the
// deck. The percentage is measured along the wire from End 1.

// The percentage marked on a card, or none. A mark outside 0 to 100 is still returned.
[[nodiscard]] auto sourcePositionMark(std::string_view cardText) -> std::optional<double>;
// The card with its mark set to `percent`, or removed. Other comment text is kept; a comment
// left empty is removed.
[[nodiscard]] auto withSourcePositionMark(std::string_view cardText,
    std::optional<double> percent) -> std::string;
// The text of a percentage as a mark writes it: 50, 37.5.
[[nodiscard]] auto sourcePositionText(double percent) -> std::string;

struct SourcePlacement {
    std::size_t lineNumber{};
    double percent{};
    int wireTag{};
    std::optional<model::AlongWirePlacement> placement;
    // Why the source cannot be placed; empty when it is.
    std::string error;
};

// Places every marked EX card of a resolved deck on the model's wires. A mark is supported
// on an EX type 0 or 5 voltage source whose tag is carried by exactly one drawn wire; tag 0
// numbers segments across the whole structure, a shared tag numbers them across several
// wires, and a flat spiral is not drawn, so these are refused with the reason.
[[nodiscard]] auto placeMarkedSources(const NecDocument& resolved, const model::AntennaModel& model)
    -> std::vector<SourcePlacement>;

// The source with each marked EX card's segment field written as the segment its mark gives,
// so programs that read the field see where NEC Workbench puts the source. Cards that cannot be
// placed, and an authored source whose symbols do not resolve, are left as they are.
[[nodiscard]] auto refreshSourcePositions(std::string_view source) -> std::string;

}
