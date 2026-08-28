#pragma once

#include "geometry/OrthographicProjection.h"
#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/ModelSetup.h"
#include "ui/geometry/GeometrySettings.h"

#include <QPointF>
#include <QWidget>

#include <optional>
#include <utility>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;
class QContextMenuEvent;
class QEvent;

namespace necwb::ui {

class GeometryView final : public QWidget {
    Q_OBJECT

public:
    explicit GeometryView(geometry::ProjectionPlane plane, QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model);
    void updateModel(const model::AntennaModel& model);
    void selectWire(int tag);
    void fitToView();
    void setGridSnappingEnabled(bool enabled);
    void setEndpointSnappingEnabled(bool enabled);
    void setLengthUnit(model::LengthUnit unit);
    void setSnapSpacing(double meters);
    void setSettings(const GeometrySettings& settings);
    void setExcitations(const std::vector<model::Excitation>& excitations);
    void setAttachments(const std::vector<model::LoadDefinition>& loads,
        const std::vector<model::TransmissionLineDefinition>& transmissionLines);
    void setPendingTransmissionLineEndpoint(std::optional<std::pair<int, int>> endpoint);
    void selectExcitation(std::size_t sourceLine);
    void selectLoad(std::size_t sourceLine);
    void selectTransmissionLine(std::size_t sourceLine);

signals:
    void wireSelected(int tag);
    void endpointPreviewed(int tag, model::WireEndpoint endpoint, model::Point3D position);
    void endpointMoveFinished(int tag, model::WireEndpoint endpoint, model::Point3D original,
        model::Point3D updated);
    void wirePreviewed(int tag, model::Point3D start, model::Point3D end);
    void wireMoveFinished(int tag, model::Point3D originalStart, model::Point3D originalEnd,
        model::Point3D updatedStart, model::Point3D updatedEnd);
    void addWireRequested(model::Point3D start, model::Point3D end);
    void splitWireRequested(int tag, model::Point3D position);
    void deleteWireRequested(int tag);
    void wirePropertiesRequested(int tag);
    void excitationSelected(std::size_t sourceLine);
    void addExcitationRequested(int wireTag, int segment);
    void editExcitationRequested(std::size_t sourceLine);
    void openExcitationSetupRequested(std::size_t sourceLine);
    void deleteExcitationRequested(std::size_t sourceLine);
    void loadSelected(std::size_t sourceLine);
    void transmissionLineSelected(std::size_t sourceLine);
    void addLoadRequested(int wireTag, int segment);
    void transmissionLineEndpointRequested(int wireTag, int segment);
    void cancelTransmissionLineRequested();
    void editLoadRequested(std::size_t sourceLine);
    void editTransmissionLineRequested(std::size_t sourceLine);
    void deleteLoadRequested(std::size_t sourceLine);
    void deleteTransmissionLineRequested(std::size_t sourceLine);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    [[nodiscard]] auto mapToScreen(const geometry::Point2D& point) const -> QPointF;
    [[nodiscard]] auto mapToWorld(const QPointF& point) const -> geometry::Point2D;
    [[nodiscard]] auto gridSpacing() const -> double;
    [[nodiscard]] auto formattedDistance(double meters) const -> QString;
    void drawGrid(QPainter& painter) const;
    void drawWires(QPainter& painter) const;
    void drawExcitations(QPainter& painter) const;
    void drawAttachments(QPainter& painter) const;
    void drawOverlay(QPainter& painter) const;
    void selectAt(const QPointF& position);
    [[nodiscard]] auto endpointAt(const QPointF& position) const
        -> std::optional<std::pair<int, model::WireEndpoint>>;
    [[nodiscard]] auto wireAt(const QPointF& position) const -> std::optional<int>;
    [[nodiscard]] auto excitationAt(const QPointF& position) const -> std::optional<std::size_t>;
    [[nodiscard]] auto loadAt(const QPointF& position) const -> std::optional<std::size_t>;
    [[nodiscard]] auto transmissionLineAt(const QPointF& position) const -> std::optional<std::size_t>;
    [[nodiscard]] auto segmentAt(int wireTag, const QPointF& position) const -> int;
    [[nodiscard]] auto snappedPoint(const geometry::Point2D& point, int excludedTag) const
        -> geometry::Point2D;
    [[nodiscard]] auto endpointSnapAdjustment(const geometry::Point2D& start,
        const geometry::Point2D& end, int excludedTag) const -> std::optional<geometry::Point2D>;
    [[nodiscard]] auto splitPointAt(int tag, const QPointF& position) const -> std::optional<model::Point3D>;
    void updateDraggedEndpoint(const QPointF& position);
    void updateDraggedWire(const QPointF& position);

    model::AntennaModel model_;
    geometry::ProjectionPlane plane_;
    QPointF worldCenter_;
    QPointF lastMousePosition_;
    QPointF dragPressPosition_;
    double pixelsPerMeter_{50.0};
    GeometrySettings settings_;
    std::vector<model::Excitation> excitations_;
    std::vector<model::LoadDefinition> loads_;
    std::vector<model::TransmissionLineDefinition> transmissionLines_;
    std::optional<int> selectedWireTag_;
    std::optional<std::size_t> selectedExcitationLine_;
    std::optional<std::size_t> selectedLoadLine_;
    std::optional<std::size_t> selectedTransmissionLine_;
    std::optional<std::pair<int, int>> pendingTransmissionLineEndpoint_;
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
