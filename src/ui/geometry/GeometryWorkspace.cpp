#include "ui/geometry/GeometryWorkspace.h"

#include "analysis/FrequencyComparison.h"
#include "analysis/SegmentCurrentDisplay.h"
#include "ui/DisplayFormat.h"
#include "ui/WorkbenchIcon.h"
#include "ui/geometry/EngineeringSpinBox.h"
#include "ui/geometry/Geometry3DView.h"
#include "ui/geometry/GeometryPlanes.h"
#include "ui/geometry/GeometryView.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <tuple>

namespace necwb::ui {
namespace {

const auto ShowCurrentsKey = QStringLiteral("geometry/showCurrents");

// A label and its control as one toolbar item, so they overflow into ›› together.
auto labeled(const QString& text, QWidget* control, QWidget* parent) -> QWidget*
{
    auto* item = new QWidget(parent);
    auto* layout = new QHBoxLayout(item);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(4);
    if (!text.isEmpty()) layout->addWidget(new QLabel(text, item));
    control->setParent(item);
    layout->addWidget(control);
    return item;
}

auto unitSymbol(model::LengthUnit unit) -> QString
{
    const auto symbol = model::lengthUnitSymbol(unit);
    return QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size()));
}

}

GeometryWorkspace::GeometryWorkspace(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("geometryWorkspace"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    createToolbar();
    planes_ = new GeometryPlanes(this);
    view3D_ = new Geometry3DView;
    pane3D_ = new GeometryPane(tr("3D"), view3D_, this);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(planes_);
    stack_->addWidget(pane3D_);
    createViewActions();
    layout->addWidget(toolbar_);
    layout->addWidget(stack_, 1);
}

void GeometryWorkspace::createToolbar()
{
    toolbar_ = new QToolBar(tr("Geometry"), this);
    toolbar_->setObjectName(QStringLiteral("geometryToolbar"));
    toolbar_->setIconSize({16, 16});

    snapGridAction_ = new QAction(tr("Snap to &Grid"), this);
    snapGridAction_->setCheckable(true);
    snapGridAction_->setChecked(true);
    snapGridAction_->setStatusTip(tr("Snap moved geometry to the fixed snap interval"));
    connect(snapGridAction_, &QAction::toggled, this, &GeometryWorkspace::gridSnappingToggled);
    snapEndpointsAction_ = new QAction(tr("Snap to &Endpoints"), this);
    snapEndpointsAction_->setCheckable(true);
    snapEndpointsAction_->setChecked(true);
    snapEndpointsAction_->setStatusTip(tr("Snap moved geometry to nearby wire endpoints"));
    connect(snapEndpointsAction_, &QAction::toggled, this,
        &GeometryWorkspace::endpointSnappingToggled);

    auto* snapItem = new QWidget(toolbar_);
    auto* snapLayout = new QHBoxLayout(snapItem);
    snapLayout->setContentsMargins(0, 0, 4, 0);
    snapLayout->setSpacing(2);
    snapButton_ = new QToolButton(snapItem);
    snapButton_->setObjectName(QStringLiteral("geometrySnapButton"));
    snapButton_->setText(tr("Snap"));
    snapButton_->setPopupMode(QToolButton::InstantPopup);
    snapButton_->setAutoRaise(true);
    auto* snapMenu = new QMenu(snapButton_);
    snapMenu->addAction(snapGridAction_);
    snapMenu->addAction(snapEndpointsAction_);
    snapButton_->setMenu(snapMenu);
    snapSpacing_ = new EngineeringSpinBox(snapItem);
    snapSpacing_->setObjectName(QStringLiteral("geometrySnapSpacing"));
    snapSpacing_->setDecimals(DisplayDecimalPlaces);
    snapSpacing_->setRange(0.000001, 1.0e9);
    snapSpacing_->setKeyboardTracking(false);
    snapSpacing_->setValue(0.1);
    snapSpacing_->setToolTip(
        tr("Snap interval in the active display unit; unit-change behavior is set in Geometry Settings"));
    connect(snapSpacing_, &QDoubleSpinBox::valueChanged, this,
        &GeometryWorkspace::snapSpacingChosen);
    snapLayout->addWidget(snapButton_);
    snapLayout->addWidget(snapSpacing_);
    toolbar_->addWidget(snapItem);
    toolbar_->addSeparator();

    lengthUnit_ = new QComboBox;
    lengthUnit_->setObjectName(QStringLiteral("geometryLengthUnit"));
    lengthUnit_->setToolTip(tr("Display unit for lengths in the geometry views and the wire form"));
    const std::array units{std::pair{model::LengthUnit::Meter, tr("Meters")},
        std::pair{model::LengthUnit::Centimeter, tr("Centimeters")},
        std::pair{model::LengthUnit::Millimeter, tr("Millimeters")},
        std::pair{model::LengthUnit::Inch, tr("Inches")},
        std::pair{model::LengthUnit::Foot, tr("Feet")}};
    for (const auto& [unit, name] : units) {
        lengthUnit_->addItem(unitSymbol(unit), static_cast<int>(unit));
        lengthUnit_->setItemData(lengthUnit_->count() - 1, name, Qt::ToolTipRole);
    }
    connect(lengthUnit_, &QComboBox::currentIndexChanged, this, [this] {
        emit lengthUnitChosen(static_cast<model::LengthUnit>(lengthUnit_->currentData().toInt()));
    });
    auto* unitsItem = labeled(tr("Units"), lengthUnit_, toolbar_);
    unitsItem->setObjectName(QStringLiteral("geometryUnitsItem"));
    toolbar_->addWidget(unitsItem);

    auto* settings = toolbar_->addAction(workbenchIcon("settings"), tr("Geometry Settings…"));
    settings->setObjectName(QStringLiteral("geometryToolbarSettings"));
    connect(settings, &QAction::triggered, this, &GeometryWorkspace::settingsRequested);
}

void GeometryWorkspace::createViewActions()
{
    viewGroup_ = new QActionGroup(this);
    viewGroup_->setExclusive(true);
    const std::array<std::tuple<geometry::ProjectionPlane, QString, QString, QString>, 3> planes{{
        {geometry::ProjectionPlane::XY, tr("Top"), QStringLiteral("geometryTopView"),
            tr("Top view (XY) in the big view")},
        {geometry::ProjectionPlane::XZ, tr("Front"), QStringLiteral("geometryFrontView"),
            tr("Front view (XZ) in the big view")},
        {geometry::ProjectionPlane::YZ, tr("Side"), QStringLiteral("geometrySideView"),
            tr("Side view (YZ) in the big view")}}};
    for (std::size_t index = 0; index < planes.size(); ++index) {
        const auto& [plane, text, name, tip] = planes[index];
        auto* action = viewGroup_->addAction(text);
        action->setObjectName(name);
        action->setCheckable(true);
        action->setToolTip(tip);
        connect(action, &QAction::triggered, this, [this, plane] {
            stack_->setCurrentIndex(Planes);
            planes_->showMainPlane(plane);
        });
        planeActions_[index] = action;
    }
    planeActions_[0]->setChecked(true);
    view3DAction_ = viewGroup_->addAction(tr("3D"));
    view3DAction_->setObjectName(QStringLiteral("geometry3DView"));
    view3DAction_->setCheckable(true);
    view3DAction_->setToolTip(tr("Interactive 3D view: drag to orbit, Shift/middle-drag to pan, "
                                 "wheel to zoom"));
    connect(view3DAction_, &QAction::triggered, this, [this] { showPage(ThreeD); });

    fitAction_ = new QAction(workbenchIcon("fit"), tr("&Fit"), this);
    fitAction_->setObjectName(QStringLiteral("geometryFit"));
    fitAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    fitAction_->setToolTip(tr("Fit the geometry in every view on screen (Ctrl+0)"));
    fitAction_->setStatusTip(tr("Fit the geometry in every view on screen"));
    connect(fitAction_, &QAction::triggered, this, &GeometryWorkspace::fitVisible);

    currentsAction_ = new QAction(tr("Currents"), this);
    currentsAction_->setObjectName(QStringLiteral("geometryCurrents"));
    currentsAction_->setCheckable(true);
    currentsAction_->setEnabled(false);
    currentsAction_->setChecked(QSettings{}.value(ShowCurrentsKey, true).toBool());
    currentsAction_->setToolTip(tr("Colour the wires by current magnitude at the frequency "
                                   "selected in the results column"));
    connect(currentsAction_, &QAction::toggled, this, [this](bool shown) {
        QSettings{}.setValue(ShowCurrentsKey, shown);
        applyCurrents();
    });

    planes_->setMainPlaneChangedCallback([this](geometry::ProjectionPlane plane) {
        if (stack_->currentIndex() != Planes) return;
        for (std::size_t index = 0; index < planeActions_.size(); ++index)
            if (planes_->views()[index]->plane() == plane) planeActions_[index]->setChecked(true);
    });
    const QList<QAction*> buttons{planeActions_[0], planeActions_[1], planeActions_[2],
        view3DAction_, nullptr, fitAction_, nullptr, currentsAction_};
    planes_->setMainHeaderActions(buttons);
    pane3D_->addHeaderTools(makeViewButtonRow(buttons, pane3D_));
    pane3D_->setTitleVisible(false);
}

auto GeometryWorkspace::views() const -> std::array<GeometryInteractionView*, 4>
{
    const auto& planes = planes_->views();
    return {planes[0], planes[1], planes[2], view3D_};
}

auto GeometryWorkspace::page() const -> int
{
    return stack_->currentIndex();
}

void GeometryWorkspace::showPage(int page)
{
    stack_->setCurrentIndex(page == ThreeD ? ThreeD : Planes);
    if (page == ThreeD) {
        view3DAction_->setChecked(true);
    } else {
        planes_->showMainPlane(planes_->mainPlane());
    }
    stack_->currentWidget()->setFocus();
}

void GeometryWorkspace::showMainPlane(geometry::ProjectionPlane plane)
{
    planes_->showMainPlane(plane);
}

void GeometryWorkspace::fitVisible()
{
    if (stack_->currentIndex() == ThreeD) {
        view3D_->fitToView();
        return;
    }
    for (auto* view : planes_->views()) view->fitToView();
}

auto GeometryWorkspace::viewActions() const -> QList<QAction*>
{
    return {planeActions_[0], planeActions_[1], planeActions_[2], view3DAction_, fitAction_,
        currentsAction_};
}

void GeometryWorkspace::setSettings(const GeometrySettings& settings)
{
    {
        const QSignalBlocker unitBlocker(lengthUnit_);
        const QSignalBlocker spacingBlocker(snapSpacing_);
        const QSignalBlocker gridBlocker(snapGridAction_);
        const QSignalBlocker endpointBlocker(snapEndpointsAction_);
        const auto unitIndex = lengthUnit_->findData(static_cast<int>(settings.lengthUnit));
        if (unitIndex >= 0) lengthUnit_->setCurrentIndex(unitIndex);
        snapSpacing_->setSuffix(QStringLiteral(" %1").arg(unitSymbol(settings.lengthUnit)));
        snapSpacing_->setValue(model::fromMeters(settings.snapSpacingMeters, settings.lengthUnit));
        snapGridAction_->setChecked(settings.gridSnapping);
        snapEndpointsAction_->setChecked(settings.endpointSnapping);
    }
    const auto onOff = [this](bool on) { return on ? tr("on") : tr("off"); };
    snapButton_->setToolTip(tr("Snapping: grid %1, endpoints %2")
        .arg(onOff(settings.gridSnapping), onOff(settings.endpointSnapping)));
    for (auto* view : planes_->views()) view->setSettings(settings);
    view3D_->setLengthUnit(settings.lengthUnit);
}

void GeometryWorkspace::setResults(const ResultState& state, bool stale)
{
    const auto same = results_.data == state.data && results_.dataVersion == state.dataVersion
        && results_.frequencyMHz == state.frequencyMHz && stale_ == stale;
    if (same) return;
    results_ = state;
    stale_ = stale;
    applyCurrents();
}

void GeometryWorkspace::applyCurrents()
{
    const auto& result = results_.data->result();
    currentsAction_->setEnabled(!result.currents.empty());
    std::shared_ptr<const analysis::DisplayedCurrents> shown;
    QString note;
    if (currentsAction_->isEnabled() && currentsAction_->isChecked() && results_.frequencyMHz) {
        const auto frequency = *results_.frequencyMHz;
        if (stale_) {
            note = tr("Currents out of date: run again");
        } else {
            // A live solve used the deck on screen; a full run, the model it was run with.
            const auto live = std::ranges::any_of(results_.liveFrequenciesMHz,
                [frequency](double value) { return nearlyEqual(value, frequency); });
            auto currents = analysis::displayedCurrentsAt(result, frequency,
                live ? view3D_->model() : results_.model);
            if (currents.segments.empty() || currents.maximumMagnitude <= 0.0) {
                note = tr("No currents at %1 MHz").arg(formatDecimal(frequency));
            } else {
                note = tr("Currents at %1 MHz").arg(formatDecimal(frequency));
                shown = std::make_shared<const analysis::DisplayedCurrents>(std::move(currents));
            }
        }
    }
    for (auto* view : views()) view->setCurrents(shown, note);
    const auto scale = shown ? std::optional{shown->maximumMagnitude} : std::nullopt;
    planes_->setCurrentScale(scale);
    pane3D_->setCurrentScale(scale);
}

}
