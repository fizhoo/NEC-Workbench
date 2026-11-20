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

// The geometry editor in the center: a compact toolbar (snapping, units, settings) over either
// the three 2D planes or the 3D view. The big view's title line carries one-click buttons for
// Top, Front, Side and 3D, Fit, and Currents, which colours the wires by the current at the
// results column's selected frequency.
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

private:
    void createToolbar();
    void createViewActions();
    void applyCurrents();

    QToolBar* toolbar_{};
    QToolButton* snapButton_{};
    QAction* snapGridAction_{};
    QAction* snapEndpointsAction_{};
    QDoubleSpinBox* snapSpacing_{};
    QComboBox* lengthUnit_{};
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
