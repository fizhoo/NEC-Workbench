#pragma once

#include <string>
#include <string_view>

namespace necwb::analysis {

struct SegmentationConvergenceDeck {
    std::string deck;
    std::string error;
    std::string warning;
    int totalSegments{};

    [[nodiscard]] auto ok() const noexcept -> bool { return error.empty(); }
};

[[nodiscard]] auto prepareSegmentationConvergenceInput(std::string_view source,
    double frequencyMHz, double segmentScale) -> SegmentationConvergenceDeck;

}
