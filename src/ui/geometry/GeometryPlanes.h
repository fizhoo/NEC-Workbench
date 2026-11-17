#pragma once

#include "geometry/OrthographicProjection.h"

#include <QSplitter>

#include <array>

namespace necwb::ui {

class GeometryView;

// The three 2D views: one big on the left, two stacked on the right. Double-clicking a small
// view's title, or Make Main View on its right-click menu, swaps it with the big one.
class GeometryPlanes final : public QSplitter {
public:
    explicit GeometryPlanes(QWidget* parent = nullptr);

    // The XY, XZ and YZ views, in that order, wherever each is shown.
    [[nodiscard]] auto views() const -> const std::array<GeometryView*, 3>& { return views_; }
    [[nodiscard]] auto mainPlane() const -> geometry::ProjectionPlane;
    // Puts plane's view in the big pane and fits the views that moved.
    void showMainPlane(geometry::ProjectionPlane plane);

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    std::array<GeometryView*, 3> views_{};
    std::array<QWidget*, 3> panes_{};
    std::array<QWidget*, 3> headers_{};
    QSplitter* side_{};
};

}
