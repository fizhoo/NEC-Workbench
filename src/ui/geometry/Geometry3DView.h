#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "ui/geometry/GeometryInteractionView.h"

#include <QPointF>

#include <optional>

class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace necwb::ui {

class Geometry3DView final : public GeometryInteractionView {
    Q_OBJECT

public:
    explicit Geometry3DView(QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model);
    void updateModel(const model::AntennaModel& model);
    void setLengthUnit(model::LengthUnit unit);
    void fitToView() override;
    void setIsometricView();

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct CameraPoint {
        QPointF screen;
        double depth{};
    };

    [[nodiscard]] auto project(const model::Point3D& point) const -> CameraPoint;
    [[nodiscard]] auto cameraCoordinates(const model::Point3D& point) const -> model::Point3D;
    [[nodiscard]] auto wireAt(const QPointF& position) const -> std::optional<int> override;
    [[nodiscard]] auto excitationAt(const QPointF& position) const
        -> std::optional<std::size_t> override;
    [[nodiscard]] auto loadAt(const QPointF& position) const
        -> std::optional<std::size_t> override;
    [[nodiscard]] auto transmissionLineAt(const QPointF& position) const
        -> std::optional<std::size_t> override;
    [[nodiscard]] auto segmentAt(int wireTag, const QPointF& position) const -> int override;
    [[nodiscard]] auto wireMenuOptions(int tag, const QPointF& position) const
        -> GeometryWireMenuOptions override;
    void handleWireContextAction(
        GeometryContextAction action, int tag, const QPointF& position) override;
    void showEmptyContextMenu(QContextMenuEvent* event) override;
    [[nodiscard]] auto formattedLength(double meters) const -> QString;
    void updateModelCenter();

    model::LengthUnit lengthUnit_{model::LengthUnit::Meter};
    model::Point3D modelCenter_;
    QPointF panOffset_;
    QPointF lastMousePosition_;
    QPointF pressPosition_;
    double pixelsPerMeter_{80.0};
    double yaw_{-0.75};
    double pitch_{0.55};
    double modelExtent_{1.0};
    bool orbiting_{};
    bool panning_{};
    bool dragMoved_{};
};

}
