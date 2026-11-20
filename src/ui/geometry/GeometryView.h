#pragma once

#include "geometry/OrthographicProjection.h"
#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "ui/geometry/GeometryInteractionView.h"
#include "ui/geometry/GeometrySettings.h"

#include <QPointF>
#include <QStringList>

#include <optional>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;
class QContextMenuEvent;
class QEvent;

namespace necwb::ui {

// A grid tick's label with the decimals its step needs, so neighbouring ticks always differ.
[[nodiscard]] auto gridTickLabel(double meters, double spacingMeters, model::LengthUnit unit)
    -> QString;

class GeometryView final : public GeometryInteractionView {
    Q_OBJECT

public:
    explicit GeometryView(geometry::ProjectionPlane plane, QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model) override;
    void updateModel(const model::AntennaModel& model) override;
    void fitToView() override;
    void setLengthUnit(model::LengthUnit unit);
    void setSnapSpacing(double meters);
    void setSettings(const GeometrySettings& settings);
    [[nodiscard]] auto plane() const -> geometry::ProjectionPlane { return plane_; }
    [[nodiscard]] auto infoText() const -> QString override;
    // The grid labels drawn along the bottom (Horizontal) or the left edge (Vertical).
    [[nodiscard]] auto tickLabels(Qt::Orientation orientation) const -> QStringList;
    // Where a model point is drawn in this view.
    [[nodiscard]] auto screenPoint(const model::Point3D& point) const -> QPointF;

signals:
    void endpointPreviewed(int tag, model::WireEndpoint endpoint, model::Point3D position);
    void endpointMoveFinished(int tag, model::WireEndpoint endpoint, model::Point3D original,
        model::Point3D updated);
    void wirePreviewed(int tag, model::Point3D start, model::Point3D end);
    void wireMoveFinished(int tag, model::Point3D originalStart, model::Point3D originalEnd,
        model::Point3D updatedStart, model::Point3D updatedEnd);
    void addWireRequested(model::Point3D start, model::Point3D end);
    void splitWireRequested(int tag, model::Point3D position);
    void deleteWireRequested(int tag);

protected:
    void leaveEvent(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Tick {
        double screen{};
        QString label;
    };
    [[nodiscard]] auto labelledTicks(Qt::Orientation orientation) const -> std::vector<Tick>;
    [[nodiscard]] auto mapToScreen(const geometry::Point2D& point) const -> QPointF;
    [[nodiscard]] auto mapToWorld(const QPointF& point) const -> geometry::Point2D;
    [[nodiscard]] auto gridSpacing() const -> double;
    [[nodiscard]] auto formattedDistance(double meters) const -> QString;
    void drawGrid(QPainter& painter) const;
    void drawSurfacePatches(QPainter& painter) const;
    void drawWires(QPainter& painter) const;
    void drawExcitations(QPainter& painter) const;
    void drawAttachments(QPainter& painter) const;
    void drawEmptyMessage(QPainter& painter) const;
    // Repaints and tells the info line, whose grid and cursor may have changed.
    void changed();
    void selectAt(const QPointF& position);
    [[nodiscard]] auto endpointAt(const QPointF& position) const
        -> std::optional<std::pair<int, model::WireEndpoint>>;
    [[nodiscard]] auto wireAt(const QPointF& position) const -> std::optional<int> override;
    [[nodiscard]] auto excitationAt(const QPointF& position) const
        -> std::optional<std::size_t> override;
    [[nodiscard]] auto loadAt(const QPointF& position) const
        -> std::optional<std::size_t> override;
    [[nodiscard]] auto transmissionLineAt(const QPointF& position) const
        -> std::optional<std::size_t> override;
    [[nodiscard]] auto networkAt(const QPointF& position) const
        -> std::optional<std::size_t> override;
    [[nodiscard]] auto segmentAt(int wireTag, const QPointF& position) const -> int override;
    [[nodiscard]] auto wireMenuOptions(int tag, const QPointF& position) const
        -> GeometryWireMenuOptions override;
    void handleWireContextAction(
        GeometryContextAction action, int tag, const QPointF& position) override;
    void showEmptyContextMenu(QContextMenuEvent* event) override;
    [[nodiscard]] auto snappedPoint(const geometry::Point2D& point, int excludedTag) const
        -> geometry::Point2D;
    [[nodiscard]] auto endpointSnapAdjustment(const geometry::Point2D& start,
        const geometry::Point2D& end, int excludedTag) const -> std::optional<geometry::Point2D>;
    [[nodiscard]] auto splitPointAt(int tag, const QPointF& position) const -> std::optional<model::Point3D>;
    void updateDraggedEndpoint(const QPointF& position);
    void updateDraggedWire(const QPointF& position);

    geometry::ProjectionPlane plane_;
    QPointF worldCenter_;
    QPointF lastMousePosition_;
    QPointF dragPressPosition_;
    double pixelsPerMeter_{50.0};
    GeometrySettings settings_;
    std::optional<geometry::Point2D> cursorWorld_;
    std::optional<model::WireEndpoint> draggedEndpoint_;
    model::Point3D dragOriginal_;
    model::Point3D wireDragOriginalStart_;
    model::Point3D wireDragOriginalEnd_;
    geometry::Point2D wireDragAnchor_;
    int draggedWireTag_{};
    bool panning_{false};
    bool draggingWire_{false};
};

}
