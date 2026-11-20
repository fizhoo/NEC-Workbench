#pragma once

#include "model/AntennaModel.h"
#include "model/AutoSegmentation.h"
#include "model/ModelSetup.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace necwb::nec {

// A wire whose recommended segment count is not applied, and why.
struct SkippedWire {
    int wireTag{};
    std::string reason;
};

// What Automatic Segmentation does to a deck. The dialog previews it and Apply takes `source`
// as it is, so the two cannot differ.
//
// A field written with a symbol or an expression is never overwritten, and no SY definition
// is changed. A wire is left as it is when its GW segment count is symbolic, or when one of
// its EX, LD, TL or NT segment references would have to change and that field is symbolic;
// for a TL or NT card each endpoint protects its own wire. A source placed along its wire
// (' necwb:at=P%) follows its mark: its segment is where the mark falls on the new
// segmentation, calculated from the edited deck exactly as resolving the deck places it, and
// the mark is kept.
struct SegmentationEdit {
    // `wires` holds every recommendation, applied or not; the remapped attachments are the
    // ones the edited deck gives, so attachments on a skipped wire keep their segments.
    model::SegmentationProposal proposal;
    std::vector<SkippedWire> skipped;
    // For each excitation, the percentage its mark places it at, or none.
    std::vector<std::optional<double>> excitationMarks;
    // The edited authored source; the input unchanged when nothing applies or on an error.
    std::string source;
    bool changed{};
    // Why the edit could not be built; empty when it was.
    std::string error;

    [[nodiscard]] auto skippedReason(int wireTag) const -> const std::string*;
};

// `source` is the authored deck; `model` and `setup` come from its resolved form.
[[nodiscard]] auto planSegmentationEdit(std::string_view source, const model::AntennaModel& model,
    const model::ModelSetup& setup, double maximumFrequencyMHz,
    const model::SegmentationSettings& settings) -> SegmentationEdit;

}
