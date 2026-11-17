#pragma once

#include "model/Point3D.h"

#include <QColor>
#include <QPointF>

#include <cstddef>
#include <vector>

class QPainter;

namespace necwb::ui {

enum class SurfaceStyle { ShadedGrid, Shaded, Wireframe };

// Turbo: dark blue at 0 (the floor) through green and yellow to dark red at 1 (the peak).
[[nodiscard]] auto turboColor(double ratio) -> QColor;

// An orbit view: turned by yaw about Z, tilted by pitch, drawn `scale` pixels per unit.
struct SurfaceView {
    double yaw{};
    double pitch{};
    double scale{1.0};
    QPointF centre;

    [[nodiscard]] auto project(const model::Point3D& point) const -> QPointF;
    // Distance into the screen; larger is farther away.
    [[nodiscard]] auto depth(const model::Point3D& point) const -> double;
};

struct SurfaceSample {
    double thetaDegrees{};
    double phiDegrees{};
    model::Point3D point;
    double colorRatio{};
};

// The pattern as lit patches between neighbouring samples of its θ×φ grid, drawn far to near,
// with the grid in a darker shade if `grid`. Over `maxFaces` patches, every second (third, ...)
// row and column is used. Returns false, drawing nothing, for a single cut with no grid to fill.
auto drawRadiationSurface(QPainter& painter, const std::vector<SurfaceSample>& samples,
    const SurfaceView& view, bool grid, bool seeThrough, std::size_t maxFaces) -> bool;

}
