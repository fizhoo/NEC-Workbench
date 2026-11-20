#pragma once

#include "nec/NecDocument.h"

#include <cstddef>
#include <string_view>

namespace necwb::nec {

// Where a 4nec2 comment starts on a line, or npos. 4nec2 takes ' as the start of a comment
// anywhere in a deck (4nec2 help, "Notepad- and Nec-edit"); its fields end there. CM and CE
// cards are comments throughout, so an apostrophe in their text starts nothing.
[[nodiscard]] auto necCommentStart(std::string_view line) noexcept -> std::size_t;

// A line that is only a ' comment reads as blank: it is not a card.
class NecParser {
public:
    [[nodiscard]] auto parse(std::string_view source) const -> NecDocument;
};

}
