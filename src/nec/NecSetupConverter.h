#pragma once

#include "model/ModelSetup.h"
#include "nec/NecDocument.h"

#include <cstddef>
#include <optional>

namespace necwb::nec {

// True for cards NEC-2 reads without ending a run of NT and TL cards: NT, TL, PT, PL, and
// cards that never reach the solver. Any other card ends the run, and the next NT or TL card
// discards every earlier network and line.
[[nodiscard]] auto continuesNetworkGroup(const NecCard& card) -> bool;
// Zero-based source index at which a new NT or TL card joins the deck's last NT/TL group, so
// it adds to the existing networks instead of discarding them. Empty when the deck has none.
[[nodiscard]] auto networkCardInsertionIndex(const NecDocument& document)
    -> std::optional<std::size_t>;

class NecSetupConverter {
public:
    [[nodiscard]] auto convert(const NecDocument& document) const -> model::ModelSetup;
};

}
