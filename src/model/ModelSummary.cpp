#include "model/ModelSummary.h"

#include "model/AntennaModel.h"
#include "nec/NecDocument.h"

#include <algorithm>

namespace necwb::model {

auto summarizeModel(const AntennaModel& model, const ModelSetup& setup,
    const nec::NecDocument& document) -> ModelSummary
{
    ModelSummary summary;
    summary.wireCount = model.wireCount();
    summary.surfacePatchCount = model.surfacePatchCount();
    for (const auto& wire : model.wires())
        summary.segmentCount += static_cast<std::size_t>(std::max(0, wire.segments));
    summary.loadCount = setup.loads.size();
    summary.transmissionLineCount = setup.transmissionLines.size();
    summary.networkCount = setup.networks.size();
    summary.executionRequestCount = setup.executionRequest ? 1U : 0U;
    for (const auto& excitation : setup.excitations)
        summary.sources.push_back({excitation.wireTag, excitation.segment});
    for (const auto& request : setup.radiationPatterns)
        summary.radiationRequests.push_back({request.thetaCount, request.phiCount});
    summary.groundType = setup.ground ? setup.ground->type : GroundType::FreeSpace;
    if (setup.frequency) {
        summary.frequency = FrequencySummary{
            setup.frequency->startMHz,
            frequencyEndMHz(*setup.frequency),
            std::max(1, setup.frequency->count),
        };
    }
    for (const auto& card : document.cards()) {
        if (card.kind == nec::NecCardKind::Blank) continue;
        ++summary.cardCount;
        if (card.kind != nec::NecCardKind::Symbol) continue;
        // One SY card can define several variables: "SY a=1, b=2".
        const auto body = card.sourceText.substr(
            std::min(card.sourceText.size(), card.sourceText.find_first_of(" \t")));
        summary.symbolCount += static_cast<std::size_t>(std::ranges::count(body, '='));
    }
    return summary;
}

}
