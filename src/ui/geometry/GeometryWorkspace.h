#pragma once

#include "geometry/OrthographicProjection.h"
#include "model/LengthUnit.h"
#include "ui/geometry/GeometrySettings.h"
#include "ui/results/ResultState.h"

#include <QWidget>

#include <array>

class QAction;
class QActionGroup;
class QComboBox;
class QDoubleSpinBox;
class QStackedWidget;
class QToolBar;
class QToolButton;

namespace necwb::ui {

class Geometry3DView;
class GeometryInteractionView;
class GeometryPane;
class GeometryPlanes;
class ToolbarItemAction;

// The geometry editor in the center: a compact toolbar (snapping, units, settings) over either
// the three 2D planes or the 3D view. The big view's title line carries one-click buttons for
// Top, Front, Side and 3D, Fit, and Currents, which colours the wires by the current at the
// results column's selected frequency. Units is an action that builds its control, so the ››
// menu of a narrow toolbar gets a working copy, kept in step with the toolbar's. Snap is the
// first item, which a toolbar never moves into ››.
class GeometryWorkspace final : public QWidget {
    Q_OBJECT

public:
    // The page shown: the 2D planes or the 3D view. Undo returns to the page an edit came from.
    enum Page { Planes = 0, ThreeD = 1 };

    explicit GeometryWorkspace(QWidget* parent = nullptr);

    [[nodiscard]] auto planes() const -> GeometryPlanes* { return planes_; }
    [[nodiscard]] auto view3D() const -> Geometry3DView* { return view3D_; }
    [[nodiscard]] auto views() const -> std::array<GeometryInteractionView*, 4>;
    [[nodiscard]] auto toolbar() const -> QToolBar* { return toolbar_; }
    [[nodiscard]] auto page() const -> int;
    void showPage(int page);
    // The plane a model opens in; the 3D view, if shown, stays.
    void showMainPlane(geometry::ProjectionPlane plane);
    // Fits what is on screen: all three planes, or the 3D view.
    void fitVisible();
    // Top, Front, Side, 3D, Fit and Currents, for the View menu.
    [[nodiscard]] auto viewActions() const -> QList<QAction*>;
    [[nodiscard]] auto currentsAction() const -> QAction* { return currentsAction_; }

    // Shows the settings in the toolbar and the views, without signalling a change.
    void setSettings(const GeometrySettings& settings);
    // The results column's state: its run with any live points, and the selected frequency.
    // Stale means the shown values at that frequency are out of date.
    void setResults(const ResultState& state, bool stale);

signals:
    void lengthUnitChosen(model::LengthUnit unit);
    // In the active display unit.
    void snapSpacingChosen(double value);
    void gridSnappingToggled(bool enabled);
    void endpointSnappingToggled(bool enabled);
    void settingsRequested();
    // Top, Front, Side, 3D or Fit was used: the geometry should be on screen.
    void viewRequested();

private:
    void createToolbar();
    void createViewActions();
    auto createSnapItem() -> QWidget*;
    // The Units item, on the toolbar or in its ›› menu.
    auto createUnitsItem(QWidget* parent) -> QWidget*;
    // Every copy of the unit box: the toolbar's, and the ›› menu's while it has one.
    [[nodiscard]] auto lengthUnits() const -> QList<QComboBox*>;
    // Shows settings_ in one copy, without signalling a change.
    void showUnit(QComboBox* unit) const;
    void applyCurrents();

    GeometrySettings settings_;
    QToolBar* toolbar_{};
    QAction* snapGridAction_{};
    QAction* snapEndpointsAction_{};
    QToolButton* snapButton_{};
    QDoubleSpinBox* snapSpacing_{};
    ToolbarItemAction* unitsAction_{};
    QStackedWidget* stack_{};
    GeometryPlanes* planes_{};
    GeometryPane* pane3D_{};
    Geometry3DView* view3D_{};
    QActionGroup* viewGroup_{};
    std::array<QAction*, 3> planeActions_{};
    QAction* view3DAction_{};
    QAction* fitAction_{};
    QAction* currentsAction_{};
    ResultState results_;
    bool stale_{};
};

}
