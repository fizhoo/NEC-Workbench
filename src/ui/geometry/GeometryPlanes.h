#pragma once

#include "geometry/OrthographicProjection.h"

#include <QList>
#include <QSplitter>

#include <array>
#include <functional>
#include <optional>

class QAction;
class QHBoxLayout;
class QLabel;

namespace necwb::ui {

class ColorScaleBar;
class GeometryInteractionView;
class GeometryView;

// A row of small tool buttons for `actions`; a null action is a separator.
[[nodiscard]] auto makeViewButtonRow(const QList<QAction*>& actions, QWidget* parent) -> QWidget*;

// One view with its thin title line above and its info line below, so nothing is drawn over
// the view itself. The info line follows the view's infoText() and shows the current scale.
class GeometryPane final : public QWidget {
public:
    GeometryPane(const QString& title, GeometryInteractionView* view, QWidget* parent);

    [[nodiscard]] auto header() const -> QWidget* { return header_; }
    [[nodiscard]] auto info() const -> QLabel* { return info_; }
    [[nodiscard]] auto colorScale() const -> ColorScaleBar* { return scale_; }
    // Puts `tools` at the right end of the title line.
    void addHeaderTools(QWidget* tools);
    // The big view's checked view button names it, so its title can give up the room.
    void setTitleVisible(bool visible);
    // The largest current, or nothing to hide the scale.
    void setCurrentScale(std::optional<double> maximum);

private:
    QWidget* header_{};
    QLabel* title_{};
    QHBoxLayout* headerLayout_{};
    QLabel* info_{};
    ColorScaleBar* scale_{};
};

// The three 2D views: one big on the left, two stacked on the right. Double-clicking a small
// view's title, or Make Main View on its right-click menu, swaps it with the big one. The big
// view's title line carries the view buttons.
class GeometryPlanes final : public QSplitter {
public:
    explicit GeometryPlanes(QWidget* parent = nullptr);

    // The XY, XZ and YZ views, in that order, wherever each is shown.
    [[nodiscard]] auto views() const -> const std::array<GeometryView*, 3>& { return views_; }
    [[nodiscard]] auto panes() const -> const std::array<GeometryPane*, 3>& { return panes_; }
    [[nodiscard]] auto mainPlane() const -> geometry::ProjectionPlane;
    // Puts plane's view in the big pane and fits the views that moved.
    void showMainPlane(geometry::ProjectionPlane plane);
    // The buttons shown in the big view's title line.
    void setMainHeaderActions(const QList<QAction*>& actions);
    // Shown in the big view's info line only.
    void setCurrentScale(std::optional<double> maximum);
    void setMainPlaneChangedCallback(std::function<void(geometry::ProjectionPlane)> callback);

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    void showMainTools();

    std::array<GeometryView*, 3> views_{};
    std::array<GeometryPane*, 3> panes_{};
    std::array<QWidget*, 3> tools_{};
    QSplitter* side_{};
    std::optional<double> currentScale_;
    std::function<void(geometry::ProjectionPlane)> mainPlaneChanged_;
};

}
