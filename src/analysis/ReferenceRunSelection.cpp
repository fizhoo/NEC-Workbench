#include "analysis/ReferenceRunSelection.h"

namespace necwb::analysis {

auto selectPreviousReferenceRun(std::span<const ReferenceRunCandidate> candidates,
    const std::string& activeRunId, const std::string& modelIdentity)
    -> std::optional<std::size_t>
{
    if (modelIdentity.empty()) return std::nullopt;

    std::optional<std::size_t> selected;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        const auto supportedType = candidate.runType == "analysis"
            || candidate.runType == "quick-sweep";
        if (!supportedType || candidate.status != "Completed"
            || candidate.id == activeRunId || candidate.modelIdentity != modelIdentity) {
            continue;
        }
        if (!selected
            || candidate.startedOrder > candidates[*selected].startedOrder) {
            selected = index;
        }
    }
    return selected;
}

}
