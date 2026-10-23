#include "ui/analysis/ResultWindowCoordinator.h"

#include "ui/DetachablePanel.h"

#include <QSettings>

namespace necwb::ui {
namespace {

constexpr auto OpenSetEstablishedKey = "resultWindows/openSetEstablished";

}

ResultWindowCoordinator::ResultWindowCoordinator(DetachablePanel* impedance,
    DetachablePanel* currents, DetachablePanel* radiation)
    : impedance_(impedance), currents_(currents), radiation_(radiation),
      openSetEstablished_(QSettings{}.value(
          QString::fromLatin1(OpenSetEstablishedKey), false).toBool())
{
}

void ResultWindowCoordinator::showImpedance()
{
    establishOpenSet();
    if (impedance_ != nullptr) impedance_->showWindow();
}

void ResultWindowCoordinator::showCurrents()
{
    establishOpenSet();
    if (currents_ != nullptr) currents_->showWindow();
}

void ResultWindowCoordinator::showRadiation()
{
    establishOpenSet();
    if (radiation_ != nullptr) radiation_->showWindow();
}

void ResultWindowCoordinator::presentCompletedAnalysis(
    const analysis::AnalysisResult& result)
{
    if (openSetEstablished()) return;
    establishOpenSet();

    if (impedance_ != nullptr && !result.feedpoints.empty())
        impedance_->showWindow(DetachablePanel::WindowActivation::Preserve);
    if (radiation_ != nullptr && !result.radiation.empty())
        radiation_->showWindow(DetachablePanel::WindowActivation::Preserve);
}

void ResultWindowCoordinator::establishOpenSet()
{
    if (openSetEstablished_) return;
    openSetEstablished_ = true;
    QSettings{}.setValue(QString::fromLatin1(OpenSetEstablishedKey), true);
}

auto ResultWindowCoordinator::openSetEstablished() const -> bool
{
    if ((impedance_ != nullptr && impedance_->isWindowVisible())
        || (currents_ != nullptr && currents_->isWindowVisible())
        || (radiation_ != nullptr && radiation_->isWindowVisible())) return true;
    return openSetEstablished_;
}

}
