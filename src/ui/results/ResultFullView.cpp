#include "ui/results/ResultFullView.h"

#include "ui/CenterViewHeader.h"
#include "ui/DisplayFormat.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/analysis/ImpedanceResultsView.h"
#include "ui/analysis/SweepPlotsView.h"

#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

auto viewTitle(ResultView view) -> QString
{
    switch (view) {
    case ResultView::Swr: return QObject::tr("SWR and impedance vs frequency");
    case ResultView::Azimuth: return QObject::tr("Azimuth pattern");
    case ResultView::Elevation: return QObject::tr("Elevation pattern");
    case ResultView::Currents: return QObject::tr("Currents");
    case ResultView::ImpedanceTable: return QObject::tr("Impedance table");
    case ResultView::CurrentsAlongWire: return QObject::tr("Currents along wire");
    case ResultView::CurrentsTable: return QObject::tr("Currents table");
    case ResultView::Pattern3D: return QObject::tr("3D pattern");
    case ResultView::Performance: return QObject::tr("Performance vs frequency");
    }
    return {};
}

}

ResultFullView::ResultFullView(const ResultState& state, QWidget* parent)
    : QWidget(parent), state_(state)
{
    setObjectName(QStringLiteral("resultFullView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new CenterViewHeader(QStringLiteral("resultFullView"), this);
    frequency_ = new QLabel(this);
    frequency_->setObjectName(QStringLiteral("resultFullViewFrequency"));
    frequency_->setToolTip(tr("Selected frequency; change it in the results column"));
    header_->addTrailingWidget(frequency_);
    layout->addWidget(header_);

    pages_ = new QStackedWidget(this);
    sweep_ = new SweepPlotsView(pages_);
    pattern_ = new RadiationPatternView(pages_);
    currents_ = new CurrentDistributionView(pages_);
    impedance_ = new ImpedanceResultsView(pages_);
    pattern3D_ = new Radiation3DView(pages_);
    performance_ = new RadiationPerformanceView(pages_);
    for (const auto& [page, widget] : std::initializer_list<std::pair<Page*, QWidget*>>{
             {&sweepPage_, sweep_}, {&patternPage_, pattern_}, {&currentsPage_, currents_},
             {&impedancePage_, impedance_}, {&pattern3DPage_, pattern3D_},
             {&performancePage_, performance_}}) {
        page->widget = widget;
        pages_->addWidget(widget);
    }
    // A narrow center scrolls the view's controls rather than widening the window.
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(pages_);
    layout->addWidget(scroll, 1);
    // The column has the one frequency selector and the one overlay control.
    pattern_->setFrequencyControlVisible(false);
    pattern3D_->setFrequencyControlVisible(false);
    sweep_->setReferenceControlVisible(false);
    pattern_->setReferenceControlVisible(false);
    pattern3D_->setReferenceControlVisible(false);
    performance_->setReferenceControlVisible(false);

    pattern_->setSettingsChangedCallback([this](const auto& settings) {
        pattern3D_->setDisplaySettings(settings);
        performance_->setComponent(settings.component);
    });
    pattern3D_->setSettingsChangedCallback([this](const auto& settings) {
        pattern_->setDisplaySettings(settings);
        performance_->setComponent(settings.component);
    });
    performance_->setComponentChangedCallback([this](auto component) {
        pattern_->setComponent(component);
        pattern3D_->setComponent(component);
    });
    const auto useAsForwardDirection = [this](double theta, double phi) {
        showView(ResultView::Performance);
        performance_->setForwardDirection(theta, phi);
    };
    pattern_->setForwardDirectionCallback(useAsForwardDirection);
    pattern3D_->setForwardDirectionCallback(useAsForwardDirection);
}

auto ResultFullView::page(ResultView view) -> Page&
{
    switch (view) {
    case ResultView::Swr: return sweepPage_;
    case ResultView::Azimuth:
    case ResultView::Elevation: return patternPage_;
    case ResultView::Currents:
    case ResultView::CurrentsAlongWire:
    case ResultView::CurrentsTable: return currentsPage_;
    case ResultView::ImpedanceTable: return impedancePage_;
    case ResultView::Pattern3D: return pattern3DPage_;
    case ResultView::Performance: return performancePage_;
    }
    return sweepPage_;
}

void ResultFullView::showView(ResultView view)
{
    view_ = view;
    // Only the page on screen sets the minimum size; the others would widen the center.
    for (auto* other : {sweepPage_.widget, patternPage_.widget, currentsPage_.widget,
             impedancePage_.widget, pattern3DPage_.widget, performancePage_.widget}) {
        other->setSizePolicy(other == page(view).widget
            ? QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred)
            : QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored));
    }
    pages_->setCurrentWidget(page(view).widget);
    apply();
    if (view == ResultView::Azimuth || view == ResultView::Elevation) {
        pattern_->showCut(view == ResultView::Azimuth
            ? RadiationPatternView::Cut::Azimuth : RadiationPatternView::Cut::Elevation);
    } else if (&page(view) == &currentsPage_) {
        currents_->showTab(view == ResultView::Currents ? CurrentDistributionView::Tab::Antenna
            : view == ResultView::CurrentsAlongWire ? CurrentDistributionView::Tab::AlongWire
                                                     : CurrentDistributionView::Tab::Table);
    }
    refreshHeader();
    // Keys such as Space and the arrows reach the view without a click first.
    page(view).widget->setFocus(Qt::OtherFocusReason);
}

void ResultFullView::refresh()
{
    refreshHeader();
    if (isVisible()) apply();
}

void ResultFullView::apply()
{
    auto& applied = page(view_).applied;
    switch (view_) {
    case ResultView::Swr: applyResultState(*sweep_, applied, state_); break;
    case ResultView::Azimuth:
    case ResultView::Elevation: applyResultState(*pattern_, applied, state_); break;
    case ResultView::Currents:
    case ResultView::CurrentsAlongWire:
    case ResultView::CurrentsTable: applyResultState(*currents_, applied, state_); break;
    case ResultView::ImpedanceTable: applyResultState(*impedance_, applied, state_); break;
    case ResultView::Pattern3D: applyResultState(*pattern3D_, applied, state_); break;
    case ResultView::Performance: applyResultState(*performance_, applied, state_); break;
    }
}

void ResultFullView::refreshHeader()
{
    header_->setTitle(viewTitle(view_) + (state_.stale ? tr(" · stale") : QString{}));
    frequency_->setText(state_.frequencyMHz
        ? tr("%1 MHz").arg(formatDecimal(*state_.frequencyMHz)) : QString{});
}

void ResultFullView::setBackCallback(std::function<void()> callback)
{
    header_->setBackCallback(std::move(callback));
}

void ResultFullView::setFrequencyActivatedCallback(std::function<void(double)> callback)
{
    sweep_->setFrequencyActivatedCallback(callback);
    impedance_->setFrequencyActivatedCallback(callback);
    performance_->setFrequencyActivatedCallback(std::move(callback));
}

}
