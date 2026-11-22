#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace necwb::nec {

// What changed between two versions of a deck, one line per change: variables added, removed
// or redefined (except those in ignoredSymbols, compared without case), card fields changed,
// and cards added or removed. Comments, blank lines and formatting are not changes.
[[nodiscard]] auto describeSourceChanges(std::string_view before, std::string_view after,
    const std::vector<std::string>& ignoredSymbols = {}) -> std::vector<std::string>;

}
