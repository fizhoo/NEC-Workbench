#pragma once

#include "analysis/AnalysisResult.h"

namespace necwb::analysis {

enum class HistoricalOverlayStatus {
    Compatible,
    Partial,
    NoMatchingData
};

struct HistoricalOverlayCompatibility {
    HistoricalOverlayStatus impedance{HistoricalOverlayStatus::NoMatchingData};
    HistoricalOverlayStatus radiation{HistoricalOverlayStatus::NoMatchingData};
};

[[nodiscard]] auto evaluateHistoricalOverlayCompatibility(
    const AnalysisResult& current, const AnalysisResult& historical)
    -> HistoricalOverlayCompatibility;

}
