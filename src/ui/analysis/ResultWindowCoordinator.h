#pragma once

#include "analysis/AnalysisResult.h"

namespace necwb::ui {

class DetachablePanel;

class ResultWindowCoordinator final {
public:
    ResultWindowCoordinator(DetachablePanel* impedance, DetachablePanel* currents,
        DetachablePanel* radiation);

    void showImpedance();
    void showCurrents();
    void showRadiation();
    void presentCompletedAnalysis(const analysis::AnalysisResult& result);

private:
    void establishOpenSet();
    [[nodiscard]] auto openSetEstablished() const -> bool;

    DetachablePanel* impedance_{};
    DetachablePanel* currents_{};
    DetachablePanel* radiation_{};
};

}
