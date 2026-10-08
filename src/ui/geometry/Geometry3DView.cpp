#include "ui/geometry/Geometry3DView.h"

#include "model/WireGeometry.h"
#include "ui/DisplayFormat.h"

#include <QContextMenuEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace necwb::ui {
namespace {

auto distanceToSegment(const QPointF& point, const QPointF& start, const QPointF& end) -> double
{
    const auto delta = end - start;
    const auto lengthSquared = QPointF::dotProduct(delta, delta);
    if (lengthSquared <= 1.0e-12) {
        return QLineF(point, start).length();
    }
    const auto parameter = std::clamp(QPointF::dotProduct(point - start, delta) / lengthSquared, 0.0, 1.0);
    return QLineF(point, start + parameter * delta).length();
}

}

Geometry3DView::Geometry3DView(QWidget* parent)
    : GeometryInteractionView(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(420, 320);
    setCursor(Qt::OpenHandCursor);
    setAccessibleName(tr("3D model geometry view"));
    setToolTip(tr("Left-drag to orbit; middle/Shift+drag to pan; wheel to zoom; click a wire to select"));
}

void Geometry3DView::setModel(const model::AntennaModel& model)
{
    setInteractionModel(model, true);
    updateModelCenter();
    fitToView();
}

void Geometry3DView::updateModel(const model::AntennaModel& model)
{
    setInteractionModel(model, false);
    updateModelCenter();
    update();
}

void Geometry3DView::setLengthUnit(model::LengthUnit unit)
{
    lengthUnit_ = unit;
    update();
}

void Geometry3DView::fitToView()
{
    panOffset_ = {};
    if (model_.empty()) {
        pixelsPerMeter_ = 80.0;
        update();
        return;
    }
    auto minimumX = 1.0e300;
    auto maximumX = -1.0e300;
    auto minimumY = 1.0e300;
    auto maximumY = -1.0e300;
    for (const auto& wire : model_.wires()) {
        for (auto point = std::size_t{}; point < model::wirePathPointCount(wire); ++point) {
            const auto camera = cameraCoordinates(model::wirePathPoint(wire, point));
            minimumX = std::min(minimumX, camera.x);
            maximumX = std::max(maximumX, camera.x);
            minimumY = std::min(minimumY, camera.y);
            maximumY = std::max(maximumY, camera.y);
        }
    }
    for (const auto& patch : model_.surfacePatches()) {
        for (const auto& point : patch.corners) {
            const auto camera = cameraCoordinates(point);
            minimumX = std::min(minimumX, camera.x);
            maximumX = std::max(maximumX, camera.x);
            minimumY = std::min(minimumY, camera.y);
            maximumY = std::max(maximumY, camera.y);
        }
    }
    const auto projectedWidth = std::max(maximumX - minimumX, modelExtent_ * 0.08);
    const auto projectedHeight = std::max(maximumY - minimumY, modelExtent_ * 0.08);
    pixelsPerMeter_ = std::clamp(std::min(
        std::max(100, width() - 120) / projectedWidth,
        std::max(100, height() - 120) / projectedHeight), 0.01, 1.0e7);
    update();
}

void Geometry3DView::setIsometricView()
{
    yaw_ = -0.75;
    pitch_ = 0.55;
    fitToView();
}

auto Geometry3DView::wireMenuOptions(int tag, const QPointF&) const
    -> GeometryWireMenuOptions
{
    const auto* wire = model_.wireByTag(tag);
    return {
        .editable = wire != nullptr && wire->editable,
        .transmissionLinePending = pendingTransmissionLineEndpoint_.has_value(),
        .allowFit = true,
    };
}

void Geometry3DView::handleWireContextAction(
    GeometryContextAction action, int, const QPointF&)
{
    if (action == GeometryContextAction::FitView) fitToView();
}

void Geometry3DView::showEmptyContextMenu(QContextMenuEvent* event)
{
    QMenu menu(this);
    auto* fitAction = menu.addAction(tr("Fit 3D View"));
    auto* isometricAction = menu.addAction(tr("Isometric View"));
    auto* cancelLineAction = pendingTransmissionLineEndpoint_
        ? menu.addAction(tr("Cancel Transmission Line")) : nullptr;
    const auto* selectedAction = menu.exec(event->globalPos());
    if (selectedAction == fitAction) {
        fitToView();
    } else if (selectedAction == isometricAction) {
        setIsometricView();
    } else if (cancelLineAction != nullptr && selectedAction == cancelLineAction) {
        emit cancelTransmissionLineRequested();
    }
}

void Geometry3DView::mouseMoveEvent(QMouseEvent* event)
{
    const auto delta = event->position() - lastMousePosition_;
    if (orbiting_) {
        yaw_ += delta.x() * 0.009;
        pitch_ = std::clamp(pitch_ - delta.y() * 0.009, -1.45, 1.45);
        dragMoved_ = dragMoved_ || QLineF(pressPosition_, event->position()).length() > 4.0;
        lastMousePosition_ = event->position();
        update();
        event->accept();
        return;
    }
    if (panning_) {
        panOffset_ += delta;
        dragMoved_ = true;
        lastMousePosition_ = event->position();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void Geometry3DView::mousePressEvent(QMouseEvent* event)
{
    const bool panGesture = event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier));
    if (panGesture || event->button() == Qt::LeftButton) {
        panning_ = panGesture;
        orbiting_ = !panGesture;
        dragMoved_ = false;
        pressPosition_ = event->position();
        lastMousePosition_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void Geometry3DView::mouseReleaseEvent(QMouseEvent* event)
{
    if ((orbiting_ || panning_) && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        const bool selectClick = orbiting_ && !dragMoved_;
        orbiting_ = false;
        panning_ = false;
        setCursor(Qt::OpenHandCursor);
        if (selectClick) {
            if (const auto sourceLine = loadAt(event->position())) {
                selectLoad(*sourceLine);
                emit loadSelected(*sourceLine);
            } else if (const auto sourceLine = transmissionLineAt(event->position())) {
                selectTransmissionLine(*sourceLine);
                emit transmissionLineSelected(*sourceLine);
            } else if (const auto sourceLine = excitationAt(event->position())) {
                selectExcitation(*sourceLine);
                emit excitationSelected(*sourceLine);
            } else {
                const auto tag = wireAt(event->position());
                selectedWireTag_ = tag;
                selectedExcitationLine_.reset();
                selectedLoadLine_.reset();
                selectedTransmissionLine_.reset();
                emit wireSelected(tag.value_or(-1));
            }
            update();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Geometry3DView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());

    if (model_.empty()) {
        painter.setPen(palette().placeholderText().color());
        painter.drawText(rect(), Qt::AlignCenter,
            tr("No valid geometry\nOpen a NEC file or add geometry cards, then run Check Model."));
        return;
    }

    const auto axisLength = std::max(modelExtent_ * 0.22, 1.0e-6);
    const model::Point3D origin{};
    const std::array<std::pair<model::Point3D, QColor>, 3> axes{{
        {{axisLength, 0.0, 0.0}, QColor(205, 60, 60)},
        {{0.0, axisLength, 0.0}, QColor(55, 155, 80)},
        {{0.0, 0.0, axisLength}, QColor(55, 105, 205)}}};
    const std::array<QString, 3> labels{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    const auto projectedOrigin = project(origin).screen;
    for (auto index = std::size_t{0}; index < axes.size(); ++index) {
        const auto endpoint = project(axes[index].first).screen;
        painter.setPen(QPen(axes[index].second, 2.25, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(projectedOrigin, endpoint);
        painter.drawText(endpoint + QPointF{5.0, -4.0}, labels[index]);
    }

    struct RenderPatch {
        QPolygonF polygon;
        double depth{};
    };
    std::vector<RenderPatch> renderPatches;
    renderPatches.reserve(model_.surfacePatchCount());
    for (const auto& patch : model_.surfacePatches()) {
        RenderPatch rendered;
        for (const auto& corner : patch.corners) {
            const auto projected = project(corner);
            rendered.polygon << projected.screen;
            rendered.depth += projected.depth;
        }
        if (!patch.corners.empty()) rendered.depth /= patch.corners.size();
        renderPatches.push_back(std::move(rendered));
    }
    std::ranges::sort(renderPatches, {}, &RenderPatch::depth);
    painter.setPen(QPen(QColor(25, 125, 110), 1.5));
    painter.setBrush(QColor(45, 165, 145, 85));
    for (const auto& patch : renderPatches) {
        if (patch.polygon.size() >= 3) painter.drawPolygon(patch.polygon);
    }

    for (const auto& line : transmissionLines_) {
        const auto first = model::wireSegmentPosition(model_, line.wireTag1, line.segment1);
        const auto second = model::wireSegmentPosition(model_, line.wireTag2, line.segment2);
        if (!first || !second) continue;
        const auto start = project(*first).screen;
        const auto end = project(*second).screen;
        const auto selected = selectedTransmissionLine_ == line.sourceLine;
        const auto color = selected ? QColor(225, 105, 25) : QColor(125, 70, 175);
        painter.setPen(QPen(color, selected ? 3.0 : 2.0, Qt::DashLine));
        painter.setBrush(palette().base());
        painter.drawLine(start, end);
        painter.drawEllipse(start, selected ? 5.0 : 4.0, selected ? 5.0 : 4.0);
        painter.drawEllipse(end, selected ? 5.0 : 4.0, selected ? 5.0 : 4.0);
        painter.drawText((start + end) / 2.0 + QPointF{6.0, -6.0}, tr("TL"));
    }
    if (pendingTransmissionLineEndpoint_) {
        const auto position = model::wireSegmentPosition(model_,
            pendingTransmissionLineEndpoint_->first, pendingTransmissionLineEndpoint_->second);
        if (position) {
            const auto screen = project(*position).screen;
            painter.setPen(QPen(QColor(125, 70, 175), 2.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(screen, 9.0, 9.0);
            painter.drawText(screen + QPointF{11.0, -8.0}, tr("TL start"));
        }
    }

    struct RenderWire {
        const model::Wire* wire{};
        CameraPoint start;
        CameraPoint end;
        bool drawStart{};
        bool drawEnd{};
        bool drawLabel{};
    };
    std::vector<RenderWire> renderWires;
    renderWires.reserve(model_.wireCount());
    for (const auto& wire : model_.wires()) {
        const auto pointCount = model::wirePathPointCount(wire);
        for (auto point = std::size_t{1}; point < pointCount; ++point) {
            renderWires.push_back({&wire, project(model::wirePathPoint(wire, point - 1)),
                project(model::wirePathPoint(wire, point)), point == 1,
                point + 1 == pointCount, point == pointCount / 2});
        }
    }
    std::ranges::sort(renderWires, {}, [](const RenderWire& wire) {
        return (wire.start.depth + wire.end.depth) / 2.0;
    });

    for (const auto& rendered : renderWires) {
        const bool selected = selectedWireTag_ == rendered.wire->tag;
        const auto color = selected ? QColor(230, 126, 34) : QColor(40, 105, 170);
        painter.setPen(QPen(color, selected ? 4.5 : 2.75, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(rendered.start.screen, rendered.end.screen);
        painter.setBrush(color);
        const auto endpointRadius = selected ? 5.0 : 3.5;
        if (rendered.drawStart)
            painter.drawEllipse(rendered.start.screen, endpointRadius, endpointRadius);
        if (rendered.drawEnd)
            painter.drawEllipse(rendered.end.screen, endpointRadius, endpointRadius);
        if (selected && rendered.drawLabel) {
            const auto midpoint = (rendered.start.screen + rendered.end.screen) / 2.0;
            const auto mnemonic = rendered.wire->geometryKind == model::WireGeometryKind::Arc
                ? QStringLiteral("GA") : rendered.wire->geometryKind == model::WireGeometryKind::Helix
                ? QStringLiteral("GH") : QStringLiteral("GW");
            painter.drawText(midpoint + QPointF{8.0, -8.0},
                QStringLiteral("%1 %2").arg(mnemonic).arg(rendered.wire->tag));
        }
    }

    for (const auto& load : loads_) {
        const auto position = model::loadPosition(model_, load);
        if (!position) continue;
        const auto screen = project(*position).screen;
        const auto selected = selectedLoadLine_ == load.sourceLine;
        const auto halfSize = selected ? 8.0 : 6.0;
        const QRectF marker(screen.x() - halfSize, screen.y() - halfSize,
            halfSize * 2.0, halfSize * 2.0);
        painter.setPen(QPen(selected ? QColor(225, 105, 25) : QColor(45, 145, 115),
            selected ? 3.0 : 2.0));
        painter.setBrush(QColor(205, 245, 225));
        painter.drawRect(marker);
        painter.setPen(QColor(25, 105, 80));
        painter.drawText(screen + QPointF{9.0, -7.0}, tr("LD"));
    }

    for (const auto& excitation : excitations_) {
        const auto position = model::excitationPosition(model_, excitation);
        if (!position) {
            continue;
        }
        const auto screen = project(*position).screen;
        const bool selected = selectedExcitationLine_ == excitation.sourceLine;
        const auto radius = selected ? 10.0 : 8.0;
        const QPolygonF diamond{screen + QPointF{0.0, -radius}, screen + QPointF{radius, 0.0},
            screen + QPointF{0.0, radius}, screen + QPointF{-radius, 0.0}};
        painter.setPen(QPen(selected ? QColor(230, 100, 25) : QColor(190, 45, 55), selected ? 3.0 : 2.0));
        painter.setBrush(QColor(255, 235, 120));
        painter.drawPolygon(diamond);
        painter.setPen(QColor(150, 30, 40));
        painter.drawText(screen + QPointF{10.0, -8.0}, tr("EX"));
    }

    const auto overlay = tr("3D Model  •  %1 wires  •  %2 patches  •  Extent %3\nLeft-drag orbit  •  Shift/middle-drag pan  •  Wheel zoom  •  Click select")
        .arg(static_cast<qulonglong>(model_.wireCount()))
        .arg(static_cast<qulonglong>(model_.surfacePatchCount()))
        .arg(formattedLength(modelExtent_));
    const auto overlayBounds = painter.fontMetrics().boundingRect(QRect(12, 12, width() - 24, 60),
        Qt::AlignLeft | Qt::AlignTop, overlay).adjusted(-7, -5, 7, 5);
    painter.fillRect(overlayBounds, palette().window().color());
    painter.setPen(palette().windowText().color());
    painter.drawText(overlayBounds.adjusted(7, 5, -7, -5), Qt::AlignLeft | Qt::AlignTop, overlay);
}

void Geometry3DView::wheelEvent(QWheelEvent* event)
{
    const auto steps = event->angleDelta().y() / 120.0;
    pixelsPerMeter_ = std::clamp(pixelsPerMeter_ * std::pow(1.18, steps), 0.01, 1.0e7);
    update();
    event->accept();
}

auto Geometry3DView::project(const model::Point3D& point) const -> CameraPoint
{
    const auto camera = cameraCoordinates(point);
    return {{width() / 2.0 + panOffset_.x() + camera.x * pixelsPerMeter_,
                height() / 2.0 + panOffset_.y() - camera.y * pixelsPerMeter_},
        camera.z};
}

auto Geometry3DView::cameraCoordinates(const model::Point3D& point) const -> model::Point3D
{
    const auto deltaX = point.x - modelCenter_.x;
    const auto deltaY = point.y - modelCenter_.y;
    const auto deltaZ = point.z - modelCenter_.z;
    const auto yawX = std::cos(yaw_) * deltaX - std::sin(yaw_) * deltaY;
    const auto yawY = std::sin(yaw_) * deltaX + std::cos(yaw_) * deltaY;
    return {yawX,
        -std::sin(pitch_) * yawY + std::cos(pitch_) * deltaZ,
        std::cos(pitch_) * yawY + std::sin(pitch_) * deltaZ};
}

auto Geometry3DView::wireAt(const QPointF& position) const -> std::optional<int>
{
    auto closestDistance = 9.0;
    std::optional<int> closestTag;
    for (const auto& wire : model_.wires()) {
        for (auto point = std::size_t{1}; point < model::wirePathPointCount(wire); ++point) {
            const auto distance = distanceToSegment(position,
                project(model::wirePathPoint(wire, point - 1)).screen,
                project(model::wirePathPoint(wire, point)).screen);
            if (distance < closestDistance) {
                closestDistance = distance;
                closestTag = wire.tag;
            }
        }
    }
    return closestTag;
}

auto Geometry3DView::excitationAt(const QPointF& position) const -> std::optional<std::size_t>
{
    for (auto iterator = excitations_.rbegin(); iterator != excitations_.rend(); ++iterator) {
        const auto sourcePosition = model::excitationPosition(model_, *iterator);
        if (sourcePosition && QLineF(position, project(*sourcePosition).screen).length() <= 12.0) {
            return iterator->sourceLine;
        }
    }
    return std::nullopt;
}

auto Geometry3DView::loadAt(const QPointF& position) const -> std::optional<std::size_t>
{
    for (auto iterator = loads_.rbegin(); iterator != loads_.rend(); ++iterator) {
        const auto markerPosition = model::loadPosition(model_, *iterator);
        if (markerPosition && QLineF(position, project(*markerPosition).screen).length() <= 12.0)
            return iterator->sourceLine;
    }
    return std::nullopt;
}

auto Geometry3DView::transmissionLineAt(const QPointF& position) const
    -> std::optional<std::size_t>
{
    for (auto iterator = transmissionLines_.rbegin(); iterator != transmissionLines_.rend(); ++iterator) {
        const auto first = model::wireSegmentPosition(model_, iterator->wireTag1, iterator->segment1);
        const auto second = model::wireSegmentPosition(model_, iterator->wireTag2, iterator->segment2);
        if (first && second
            && distanceToSegment(position, project(*first).screen, project(*second).screen) <= 8.0)
            return iterator->sourceLine;
    }
    return std::nullopt;
}

auto Geometry3DView::segmentAt(int wireTag, const QPointF& position) const -> int
{
    const auto* wire = model_.wireByTag(wireTag);
    if (wire == nullptr) {
        return 1;
    }
    if (wire->path.size() == static_cast<std::size_t>(wire->segments + 1)) {
        auto closestSegment = 1;
        auto closestDistance = std::numeric_limits<double>::infinity();
        for (auto segment = 1; segment <= wire->segments; ++segment) {
            const auto endpoints = model::wireSegmentEndpoints(*wire, segment);
            const auto distance = distanceToSegment(position, project(endpoints->first).screen,
                project(endpoints->second).screen);
            if (distance < closestDistance) {
                closestDistance = distance;
                closestSegment = segment;
            }
        }
        return closestSegment;
    }
    const auto start = project(wire->start).screen;
    const auto end = project(wire->end).screen;
    const auto delta = end - start;
    const auto lengthSquared = QPointF::dotProduct(delta, delta);
    const auto parameter = lengthSquared <= 1.0e-12 ? 0.0
        : std::clamp(QPointF::dotProduct(position - start, delta) / lengthSquared, 0.0, 1.0);
    return std::clamp(static_cast<int>(parameter * wire->segments) + 1, 1, wire->segments);
}

auto Geometry3DView::formattedLength(double meters) const -> QString
{
    const auto symbol = model::lengthUnitSymbol(lengthUnit_);
    return QStringLiteral("%1 %2")
        .arg(formatDecimal(model::fromMeters(meters, lengthUnit_)))
        .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

void Geometry3DView::updateModelCenter()
{
    if (model_.empty()) {
        modelCenter_ = {};
        modelExtent_ = 1.0;
        return;
    }
    auto minimum = !model_.wires().empty() ? model_.wires().front().start
        : model_.surfacePatches().front().corners.front();
    auto maximum = minimum;
    for (const auto& wire : model_.wires()) {
        for (auto index = std::size_t{}; index < model::wirePathPointCount(wire); ++index) {
            const auto& point = model::wirePathPoint(wire, index);
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        }
    }
    for (const auto& patch : model_.surfacePatches()) {
        for (const auto& point : patch.corners) {
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        }
    }
    modelCenter_ = {(minimum.x + maximum.x) / 2.0,
        (minimum.y + maximum.y) / 2.0,
        (minimum.z + maximum.z) / 2.0};
    modelExtent_ = std::max({maximum.x - minimum.x, maximum.y - minimum.y,
        maximum.z - minimum.z, 1.0e-6});
}

}
