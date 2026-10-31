#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace necwb::analysis {

struct ReferenceRunCandidate {
    std::string id;
    std::string modelIdentity;
    std::string runType;
    std::string status;
    std::int64_t startedOrder{};
};

[[nodiscard]] auto selectPreviousReferenceRun(
    std::span<const ReferenceRunCandidate> candidates,
    const std::string& activeRunId, const std::string& modelIdentity)
    -> std::optional<std::size_t>;

}
