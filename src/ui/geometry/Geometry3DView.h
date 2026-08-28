#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/ModelSetup.h"

#include <QPointF>
#include <QWidget>

#include <optional>
#include <utility>

class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace necwb::ui {

class Geometry3DView final : public QWidget {
    Q_OBJECT

public:
    explicit Geometry3DView(QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model);
    void updateModel(const model::AntennaModel& model);
    void selectWire(int tag);
    void setLengthUnit(model::LengthUnit unit);
    void setExcitations(const std::vector<model::Excitation>& excitations);
    void setAttachments(const std::vector<model::LoadDefinition>& loads,
        const std::vector<model::TransmissionLineDefinition>& transmissionLines);
    void setPendingTransmissionLineEndpoint(std::optional<std::pair<int, int>> endpoint);
    void selectExcitation(std::size_t sourceLine);
    void selectLoad(std::size_t sourceLine);
    void selectTransmissionLine(std::size_t sourceLine);
    void fitToView();
    void setIsometricView();

signals:
    void wireSelected(int tag);
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
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
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
    [[nodiscard]] auto wireAt(const QPointF& position) const -> std::optional<int>;
    [[nodiscard]] auto excitationAt(const QPointF& position) const -> std::optional<std::size_t>;
    [[nodiscard]] auto loadAt(const QPointF& position) const -> std::optional<std::size_t>;
    [[nodiscard]] auto transmissionLineAt(const QPointF& position) const -> std::optional<std::size_t>;
    [[nodiscard]] auto segmentAt(int wireTag, const QPointF& position) const -> int;
    [[nodiscard]] auto formattedLength(double meters) const -> QString;
    void updateModelCenter();

    model::AntennaModel model_;
    model::LengthUnit lengthUnit_{model::LengthUnit::Meter};
    model::Point3D modelCenter_;
    std::vector<model::Excitation> excitations_;
    std::vector<model::LoadDefinition> loads_;
    std::vector<model::TransmissionLineDefinition> transmissionLines_;
    QPointF panOffset_;
    QPointF lastMousePosition_;
    QPointF pressPosition_;
    double pixelsPerMeter_{80.0};
    double yaw_{-0.75};
    double pitch_{0.55};
    double modelExtent_{1.0};
    std::optional<int> selectedWireTag_;
    std::optional<std::size_t> selectedExcitationLine_;
    std::optional<std::size_t> selectedLoadLine_;
    std::optional<std::size_t> selectedTransmissionLine_;
    std::optional<std::pair<int, int>> pendingTransmissionLineEndpoint_;
    bool orbiting_{};
    bool panning_{};
    bool dragMoved_{};
};

}
