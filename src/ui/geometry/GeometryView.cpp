#include "ui/geometry/GeometryView.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QLineF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace necwb::ui {
namespace {

auto planeName(geometry::ProjectionPlane plane) -> QString
{
    switch (plane) {
    case geometry::ProjectionPlane::XY:
        return QStringLiteral("XY");
    case geometry::ProjectionPlane::XZ:
        return QStringLiteral("XZ");
    case geometry::ProjectionPlane::YZ:
        return QStringLiteral("YZ");
    }
    return {};
}

auto horizontalAxis(geometry::ProjectionPlane plane) -> QString
{
    return plane == geometry::ProjectionPlane::YZ ? QStringLiteral("Y") : QStringLiteral("X");
}

auto verticalAxis(geometry::ProjectionPlane plane) -> QString
{
    return plane == geometry::ProjectionPlane::XY ? QStringLiteral("Y") : QStringLiteral("Z");
}

}

GeometryView::GeometryView(geometry::ProjectionPlane plane, QWidget* parent)
    : QWidget(parent)
    , plane_(plane)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setMinimumSize(320, 240);
    setAccessibleName(planeName(plane_) + tr(" geometry view"));
    setToolTip(tr("Drag endpoint to reshape; drag wire to move; middle/Alt+drag to pan; wheel to zoom"));
}

void GeometryView::setModel(const model::AntennaModel& model)
{
    model_ = model;
    selectedWireTag_.reset();
    fitToView();
}

void GeometryView::updateModel(const model::AntennaModel& model)
{
    model_ = model;
    if (selectedWireTag_ && model_.wireByTag(*selectedWireTag_) == nullptr) {
        selectedWireTag_.reset();
    }
    update();
}

void GeometryView::selectWire(int tag)
{
    const auto found = std::ranges::find(model_.wires(), tag, &model::Wire::tag);
    selectedWireTag_ = found == model_.wires().end() ? std::nullopt : std::optional<int>{tag};
    selectedExcitationLine_.reset();
    selectedLoadLine_.reset();
    selectedTransmissionLine_.reset();
    update();
}

void GeometryView::fitToView()
{
    const auto bounds = geometry::projectedBounds(model_.wires(), plane_);
    if (!bounds) {
        worldCenter_ = {};
        pixelsPerMeter_ = 50.0;
        update();
        return;
    }

    const auto center = bounds->center();
    worldCenter_ = {center.horizontal, center.vertical};
    const auto largestExtent = std::max({bounds->width(), bounds->height(), 1.0e-6});
    const auto horizontalExtent = std::max(bounds->width(), largestExtent * 0.1);
    const auto verticalExtent = std::max(bounds->height(), largestExtent * 0.1);
    const auto availableWidth = std::max(100, width() - 100);
    const auto availableHeight = std::max(100, height() - 100);
    pixelsPerMeter_ = std::clamp(std::min(availableWidth / horizontalExtent, availableHeight / verticalExtent),
        0.02, 1.0e6);
    update();
}

void GeometryView::setGridSnappingEnabled(bool enabled)
{
    settings_.gridSnapping = enabled;
    update();
}

void GeometryView::setEndpointSnappingEnabled(bool enabled)
{
    settings_.endpointSnapping = enabled;
    update();
}

void GeometryView::setLengthUnit(model::LengthUnit unit)
{
    settings_.lengthUnit = unit;
    update();
}

void GeometryView::setSnapSpacing(double meters)
{
    settings_.snapSpacingMeters = std::max(meters, 1.0e-12);
    update();
}

void GeometryView::setSettings(const GeometrySettings& settings)
{
    settings_ = settings;
    settings_.manualGridSpacingMeters = std::max(settings_.manualGridSpacingMeters, 1.0e-12);
    settings_.snapSpacingMeters = std::max(settings_.snapSpacingMeters, 1.0e-12);
    settings_.minorGridDivisions = std::max(settings_.minorGridDivisions, 1);
    update();
}

void GeometryView::setExcitations(const std::vector<model::Excitation>& excitations)
{
    excitations_ = excitations;
    if (selectedExcitationLine_
        && std::ranges::find(excitations_, *selectedExcitationLine_, &model::Excitation::sourceLine)
            == excitations_.end()) {
        selectedExcitationLine_.reset();
    }
    update();
}

void GeometryView::setAttachments(const std::vector<model::LoadDefinition>& loads,
    const std::vector<model::TransmissionLineDefinition>& transmissionLines)
{
    loads_ = loads;
    transmissionLines_ = transmissionLines;
    if (selectedLoadLine_
        && std::ranges::find(loads_, *selectedLoadLine_, &model::LoadDefinition::sourceLine)
            == loads_.end()) selectedLoadLine_.reset();
    if (selectedTransmissionLine_
        && std::ranges::find(transmissionLines_, *selectedTransmissionLine_,
            &model::TransmissionLineDefinition::sourceLine) == transmissionLines_.end())
        selectedTransmissionLine_.reset();
    update();
}

void GeometryView::setPendingTransmissionLineEndpoint(
    std::optional<std::pair<int, int>> endpoint)
{
    pendingTransmissionLineEndpoint_ = endpoint;
    update();
}

void GeometryView::selectExcitation(std::size_t sourceLine)
{
    const auto found = std::ranges::find(excitations_, sourceLine, &model::Excitation::sourceLine);
    selectedExcitationLine_ = found == excitations_.end()
        ? std::nullopt : std::optional<std::size_t>{sourceLine};
    selectedWireTag_.reset();
    selectedLoadLine_.reset();
    selectedTransmissionLine_.reset();
    update();
}

void GeometryView::selectLoad(std::size_t sourceLine)
{
    const auto found = std::ranges::find(loads_, sourceLine, &model::LoadDefinition::sourceLine);
    selectedLoadLine_ = found == loads_.end() ? std::nullopt : std::optional{sourceLine};
    selectedExcitationLine_.reset();
    selectedTransmissionLine_.reset();
    selectedWireTag_.reset();
    update();
}

void GeometryView::selectTransmissionLine(std::size_t sourceLine)
{
    const auto found = std::ranges::find(transmissionLines_, sourceLine,
        &model::TransmissionLineDefinition::sourceLine);
    selectedTransmissionLine_ = found == transmissionLines_.end()
        ? std::nullopt : std::optional{sourceLine};
    selectedExcitationLine_.reset();
    selectedLoadLine_.reset();
    selectedWireTag_.reset();
    update();
}

void GeometryView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        fitToView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void GeometryView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    if (const auto sourceLine = loadAt(event->pos())) {
        selectLoad(*sourceLine);
        emit loadSelected(*sourceLine);
        auto* editAction = menu.addAction(tr("Edit in Model Setup"));
        menu.addSeparator();
        auto* deleteAction = menu.addAction(tr("Delete Load"));
        const auto* selectedAction = menu.exec(event->globalPos());
        if (selectedAction == editAction) emit editLoadRequested(*sourceLine);
        else if (selectedAction == deleteAction) emit deleteLoadRequested(*sourceLine);
    } else if (const auto sourceLine = transmissionLineAt(event->pos())) {
        selectTransmissionLine(*sourceLine);
        emit transmissionLineSelected(*sourceLine);
        auto* editAction = menu.addAction(tr("Edit in Model Setup"));
        menu.addSeparator();
        auto* deleteAction = menu.addAction(tr("Delete Transmission Line"));
        const auto* selectedAction = menu.exec(event->globalPos());
        if (selectedAction == editAction) emit editTransmissionLineRequested(*sourceLine);
        else if (selectedAction == deleteAction) emit deleteTransmissionLineRequested(*sourceLine);
    } else if (const auto sourceLine = excitationAt(event->pos())) {
        selectExcitation(*sourceLine);
        emit excitationSelected(*sourceLine);
        auto* propertiesAction = menu.addAction(tr("Properties…"));
        auto* setupAction = menu.addAction(tr("Edit in Setup"));
        menu.addSeparator();
        auto* deleteAction = menu.addAction(tr("Delete Voltage Source"));
        const auto* selectedAction = menu.exec(event->globalPos());
        if (selectedAction == propertiesAction) {
            emit editExcitationRequested(*sourceLine);
        } else if (selectedAction == setupAction) {
            emit openExcitationSetupRequested(*sourceLine);
        } else if (selectedAction == deleteAction) {
            emit deleteExcitationRequested(*sourceLine);
        }
    } else if (const auto tag = wireAt(event->pos())) {
        selectedWireTag_ = *tag;
        selectedExcitationLine_.reset();
        selectedLoadLine_.reset();
        selectedTransmissionLine_.reset();
        emit wireSelected(*tag);
        const auto splitPoint = splitPointAt(*tag, event->pos());
        auto* propertiesAction = menu.addAction(tr("Properties…"));
        menu.addSeparator();
        auto* addSourceAction = menu.addAction(tr("Add Voltage Source Here"));
        auto* addLoadAction = menu.addAction(tr("Add Load Here"));
        auto* lineEndpointAction = menu.addAction(pendingTransmissionLineEndpoint_
            ? tr("Complete Transmission Line Here") : tr("Start Transmission Line Here"));
        auto* cancelLineAction = pendingTransmissionLineEndpoint_
            ? menu.addAction(tr("Cancel Transmission Line")) : nullptr;
        auto* splitAction = menu.addAction(tr("Split Wire Here"));
        splitAction->setEnabled(splitPoint.has_value());
        auto* deleteAction = menu.addAction(tr("Delete Wire"));
        const auto* selectedAction = menu.exec(event->globalPos());
        if (selectedAction == splitAction && splitPoint) {
            emit splitWireRequested(*tag, *splitPoint);
        } else if (selectedAction == addSourceAction) {
            emit addExcitationRequested(*tag, segmentAt(*tag, event->pos()));
        } else if (selectedAction == addLoadAction) {
            emit addLoadRequested(*tag, segmentAt(*tag, event->pos()));
        } else if (selectedAction == lineEndpointAction) {
            emit transmissionLineEndpointRequested(*tag, segmentAt(*tag, event->pos()));
        } else if (cancelLineAction != nullptr && selectedAction == cancelLineAction) {
            emit cancelTransmissionLineRequested();
        } else if (selectedAction == deleteAction) {
            emit deleteWireRequested(*tag);
        } else if (selectedAction == propertiesAction) {
            emit wirePropertiesRequested(*tag);
        }
    } else {
        auto* addAction = menu.addAction(tr("Add Wire Here"));
        auto* cancelLineAction = pendingTransmissionLineEndpoint_
            ? menu.addAction(tr("Cancel Transmission Line")) : nullptr;
        const auto* selectedAction = menu.exec(event->globalPos());
        if (selectedAction == addAction) {
            const auto start2D = snappedPoint(mapToWorld(event->pos()), -1);
            const geometry::Point2D end2D{start2D.horizontal + gridSpacing(), start2D.vertical};
            emit addWireRequested(geometry::withProjectedCoordinates({}, start2D, plane_),
                geometry::withProjectedCoordinates({}, end2D, plane_));
        } else if (cancelLineAction != nullptr && selectedAction == cancelLineAction) {
            emit cancelTransmissionLineRequested();
        }
    }
    event->accept();
}

void GeometryView::mouseMoveEvent(QMouseEvent* event)
{
    cursorWorld_ = mapToWorld(event->position());
    if (draggedEndpoint_) {
        updateDraggedEndpoint(event->position());
        event->accept();
        return;
    }
    if (draggingWire_) {
        updateDraggedWire(event->position());
        event->accept();
        return;
    }
    if (panning_) {
        const auto delta = event->position() - lastMousePosition_;
        worldCenter_.rx() -= delta.x() / pixelsPerMeter_;
        worldCenter_.ry() += delta.y() / pixelsPerMeter_;
        lastMousePosition_ = event->position();
        cursorWorld_ = mapToWorld(event->position());
        update();
        event->accept();
        return;
    }
    if (loadAt(event->position()) || transmissionLineAt(event->position())
        || excitationAt(event->position())) {
        setCursor(Qt::PointingHandCursor);
    } else if (endpointAt(event->position())) {
        setCursor(Qt::OpenHandCursor);
    } else if (wireAt(event->position())) {
        setCursor(Qt::SizeAllCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
    update();
    QWidget::mouseMoveEvent(event);
}

void GeometryView::leaveEvent(QEvent* event)
{
    cursorWorld_.reset();
    update();
    QWidget::leaveEvent(event);
}

void GeometryView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::AltModifier))) {
        panning_ = true;
        lastMousePosition_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (const auto sourceLine = loadAt(event->position())) {
            selectLoad(*sourceLine);
            emit loadSelected(*sourceLine);
            event->accept();
            return;
        }
        if (const auto sourceLine = transmissionLineAt(event->position())) {
            selectTransmissionLine(*sourceLine);
            emit transmissionLineSelected(*sourceLine);
            event->accept();
            return;
        }
        if (const auto sourceLine = excitationAt(event->position())) {
            selectExcitation(*sourceLine);
            emit excitationSelected(*sourceLine);
            event->accept();
            return;
        }
        if (const auto endpoint = endpointAt(event->position())) {
            draggedWireTag_ = endpoint->first;
            draggedEndpoint_ = endpoint->second;
            selectedWireTag_ = draggedWireTag_;
            dragPressPosition_ = event->position();
            const auto* wire = model_.wireByTag(draggedWireTag_);
            dragOriginal_ = *draggedEndpoint_ == model::WireEndpoint::Start ? wire->start : wire->end;
            setCursor(Qt::ClosedHandCursor);
            emit wireSelected(draggedWireTag_);
            update();
            event->accept();
            return;
        }
        if (const auto tag = wireAt(event->position())) {
            draggedWireTag_ = *tag;
            draggingWire_ = true;
            selectedWireTag_ = draggedWireTag_;
            const auto* wire = model_.wireByTag(draggedWireTag_);
            wireDragOriginalStart_ = wire->start;
            wireDragOriginalEnd_ = wire->end;
            wireDragAnchor_ = mapToWorld(event->position());
            dragPressPosition_ = event->position();
            setCursor(Qt::ClosedHandCursor);
            emit wireSelected(draggedWireTag_);
            update();
            event->accept();
            return;
        }
        selectAt(event->position());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GeometryView::mouseReleaseEvent(QMouseEvent* event)
{
    if (draggedEndpoint_ && event->button() == Qt::LeftButton) {
        const auto* wire = model_.wireByTag(draggedWireTag_);
        const auto updated = *draggedEndpoint_ == model::WireEndpoint::Start ? wire->start : wire->end;
        if (updated != dragOriginal_) {
            emit endpointMoveFinished(draggedWireTag_, *draggedEndpoint_, dragOriginal_, updated);
        }
        draggedEndpoint_.reset();
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    if (draggingWire_ && event->button() == Qt::LeftButton) {
        const auto* wire = model_.wireByTag(draggedWireTag_);
        if (wire != nullptr
            && (wire->start != wireDragOriginalStart_ || wire->end != wireDragOriginalEnd_)) {
            emit wireMoveFinished(draggedWireTag_, wireDragOriginalStart_, wireDragOriginalEnd_,
                wire->start, wire->end);
        }
        draggingWire_ = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void GeometryView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    drawGrid(painter);
    drawWires(painter);
    drawAttachments(painter);
    drawExcitations(painter);
    drawOverlay(painter);
}

void GeometryView::wheelEvent(QWheelEvent* event)
{
    const auto worldBeforeZoom = mapToWorld(event->position());
    const auto zoomFactor = std::pow(1.0015, event->angleDelta().y());
    pixelsPerMeter_ = std::clamp(pixelsPerMeter_ * zoomFactor, 0.02, 1.0e6);
    const auto worldAfterZoom = mapToWorld(event->position());
    worldCenter_.rx() += worldBeforeZoom.horizontal - worldAfterZoom.horizontal;
    worldCenter_.ry() += worldBeforeZoom.vertical - worldAfterZoom.vertical;
    update();
    event->accept();
}

auto GeometryView::mapToScreen(const geometry::Point2D& point) const -> QPointF
{
    return {width() / 2.0 + (point.horizontal - worldCenter_.x()) * pixelsPerMeter_,
        height() / 2.0 - (point.vertical - worldCenter_.y()) * pixelsPerMeter_};
}

auto GeometryView::mapToWorld(const QPointF& point) const -> geometry::Point2D
{
    return {worldCenter_.x() + (point.x() - width() / 2.0) / pixelsPerMeter_,
        worldCenter_.y() - (point.y() - height() / 2.0) / pixelsPerMeter_};
}

auto GeometryView::gridSpacing() const -> double
{
    if (!settings_.automaticGridSpacing) {
        return settings_.manualGridSpacingMeters;
    }
    const auto rawSpacing = model::fromMeters(80.0 / pixelsPerMeter_, settings_.lengthUnit);
    return model::toMeters(model::niceEngineeringStep(rawSpacing), settings_.lengthUnit);
}

auto GeometryView::formattedDistance(double meters) const -> QString
{
    const auto symbol = model::lengthUnitSymbol(settings_.lengthUnit);
    return QStringLiteral("%1 %2")
        .arg(QString::number(model::fromMeters(meters, settings_.lengthUnit), 'g', 5))
        .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

void GeometryView::drawGrid(QPainter& painter) const
{
    const auto topLeft = mapToWorld({0.0, 0.0});
    const auto bottomRight = mapToWorld({static_cast<double>(width()), static_cast<double>(height())});
    const auto spacing = gridSpacing();
    const auto firstHorizontal = std::floor(topLeft.horizontal / spacing) * spacing;
    const auto firstVertical = std::floor(bottomRight.vertical / spacing) * spacing;

    if (settings_.showGrid) {
        const auto minorSpacing = spacing / settings_.minorGridDivisions;
        if (settings_.minorGridDivisions > 1 && minorSpacing * pixelsPerMeter_ >= 6.0) {
            painter.setPen(QPen(palette().midlight().color().lighter(108), 0.7));
            const auto firstMinorHorizontal = std::floor(topLeft.horizontal / minorSpacing) * minorSpacing;
            const auto firstMinorVertical = std::floor(bottomRight.vertical / minorSpacing) * minorSpacing;
            for (auto horizontal = firstMinorHorizontal; horizontal <= bottomRight.horizontal; horizontal += minorSpacing) {
                const auto x = mapToScreen({horizontal, 0.0}).x();
                painter.drawLine(QPointF{x, 0.0}, QPointF{x, static_cast<double>(height())});
            }
            for (auto vertical = firstMinorVertical; vertical <= topLeft.vertical; vertical += minorSpacing) {
                const auto y = mapToScreen({0.0, vertical}).y();
                painter.drawLine(QPointF{0.0, y}, QPointF{static_cast<double>(width()), y});
            }
        }

        painter.setPen(QPen(palette().midlight().color(), 1.0));
        for (auto horizontal = firstHorizontal; horizontal <= bottomRight.horizontal; horizontal += spacing) {
            const auto x = mapToScreen({horizontal, 0.0}).x();
            painter.drawLine(QPointF{x, 0.0}, QPointF{x, static_cast<double>(height())});
            if (settings_.showLabels && x >= 4.0 && x <= width() - 40.0) {
                painter.setPen(palette().placeholderText().color());
                painter.drawText(QPointF{x + 3.0, height() - 7.0},
                    QString::number(model::fromMeters(horizontal, settings_.lengthUnit), 'g', 4));
                painter.setPen(QPen(palette().midlight().color(), 1.0));
            }
        }
        for (auto vertical = firstVertical; vertical <= topLeft.vertical; vertical += spacing) {
            const auto y = mapToScreen({0.0, vertical}).y();
            painter.drawLine(QPointF{0.0, y}, QPointF{static_cast<double>(width()), y});
            if (settings_.showLabels && y >= 15.0 && y <= height() - 8.0) {
                painter.setPen(palette().placeholderText().color());
                painter.drawText(QPointF{5.0, y - 3.0},
                    QString::number(model::fromMeters(vertical, settings_.lengthUnit), 'g', 4));
                painter.setPen(QPen(palette().midlight().color(), 1.0));
            }
        }
    }

    if (settings_.showAxes) {
        auto axisColor = palette().windowText().color();
        axisColor.setAlphaF(0.7);
        QPen axisPen(axisColor, 2.25);
        painter.setPen(axisPen);
        const auto origin = mapToScreen({0.0, 0.0});
        if (origin.y() >= 0.0 && origin.y() <= height()) {
            painter.drawLine(QPointF{0.0, origin.y()}, QPointF{static_cast<double>(width()), origin.y()});
            if (settings_.showLabels) {
                painter.drawText(width() - 24, origin.y() - 6, horizontalAxis(plane_));
            }
        }
        if (origin.x() >= 0.0 && origin.x() <= width()) {
            painter.drawLine(QPointF{origin.x(), 0.0}, QPointF{origin.x(), static_cast<double>(height())});
            if (settings_.showLabels) {
                painter.drawText(origin.x() + 7, 17, verticalAxis(plane_));
            }
        }
    }
}

void GeometryView::drawWires(QPainter& painter) const
{
    const QColor wireColor(40, 105, 170);
    const QColor selectedColor(230, 126, 34);
    auto index = 0;
    for (const auto& wire : model_.wires()) {
        const auto start = mapToScreen(geometry::project(wire.start, plane_));
        const auto end = mapToScreen(geometry::project(wire.end, plane_));
        const bool selected = selectedWireTag_ == wire.tag;
        const auto color = selected ? selectedColor : wireColor;
        painter.setPen(QPen(color, selected ? 4.0 : 2.5, Qt::SolidLine, Qt::RoundCap));

        if (QLineF(start, end).length() < 1.0) {
            const auto radius = selected ? 8.0 : 6.0;
            painter.setBrush(palette().base());
            painter.drawEllipse(start, radius, radius);
            painter.drawLine(start + QPointF{-radius, 0.0}, start + QPointF{radius, 0.0});
            painter.drawLine(start + QPointF{0.0, -radius}, start + QPointF{0.0, radius});
        } else {
            painter.drawLine(start, end);
            painter.setBrush(color);
            painter.drawEllipse(start, selected ? 4.5 : 3.5, selected ? 4.5 : 3.5);
            painter.drawEllipse(end, selected ? 4.5 : 3.5, selected ? 4.5 : 3.5);
        }

        const auto midpoint = (start + end) / 2.0 + QPointF{8.0, -8.0 - (index % 3) * 14.0};
        const auto label = QStringLiteral("GW %1").arg(wire.tag);
        const auto labelBounds = painter.fontMetrics().boundingRect(label).adjusted(-3, -2, 3, 2);
        auto labelRectangle = QRectF(labelBounds);
        labelRectangle.moveTopLeft(midpoint);
        painter.fillRect(labelRectangle, palette().base().color());
        painter.setPen(color);
        painter.drawText(labelRectangle, Qt::AlignCenter, label);
        ++index;
    }
}

void GeometryView::drawExcitations(QPainter& painter) const
{
    for (const auto& excitation : excitations_) {
        const auto position = model::excitationPosition(model_, excitation);
        if (!position) {
            continue;
        }
        const auto screen = mapToScreen(geometry::project(*position, plane_));
        const bool selected = selectedExcitationLine_ == excitation.sourceLine;
        const auto radius = selected ? 9.0 : 7.0;
        const QPolygonF diamond{screen + QPointF{0.0, -radius}, screen + QPointF{radius, 0.0},
            screen + QPointF{0.0, radius}, screen + QPointF{-radius, 0.0}};
        painter.setPen(QPen(selected ? QColor(230, 100, 25) : QColor(190, 45, 55), selected ? 3.0 : 2.0));
        painter.setBrush(QColor(255, 235, 120));
        painter.drawPolygon(diamond);
        painter.setPen(QColor(150, 30, 40));
        painter.drawText(screen + QPointF{9.0, -7.0}, tr("EX"));
    }
}

void GeometryView::drawAttachments(QPainter& painter) const
{
    for (const auto& line : transmissionLines_) {
        const auto first = model::wireSegmentPosition(model_, line.wireTag1, line.segment1);
        const auto second = model::wireSegmentPosition(model_, line.wireTag2, line.segment2);
        if (!first || !second) continue;
        const auto start = mapToScreen(geometry::project(*first, plane_));
        const auto end = mapToScreen(geometry::project(*second, plane_));
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
            const auto screen = mapToScreen(geometry::project(*position, plane_));
            painter.setPen(QPen(QColor(125, 70, 175), 2.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(screen, 9.0, 9.0);
            painter.drawText(screen + QPointF{11.0, -8.0}, tr("TL start"));
        }
    }
    for (const auto& load : loads_) {
        const auto position = model::loadPosition(model_, load);
        if (!position) continue;
        const auto screen = mapToScreen(geometry::project(*position, plane_));
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
}

auto GeometryView::excitationAt(const QPointF& position) const -> std::optional<std::size_t>
{
    for (auto iterator = excitations_.rbegin(); iterator != excitations_.rend(); ++iterator) {
        const auto sourcePosition = model::excitationPosition(model_, *iterator);
        if (sourcePosition
            && QLineF(position, mapToScreen(geometry::project(*sourcePosition, plane_))).length() <= 11.0) {
            return iterator->sourceLine;
        }
    }
    return std::nullopt;
}

auto GeometryView::loadAt(const QPointF& position) const -> std::optional<std::size_t>
{
    for (auto iterator = loads_.rbegin(); iterator != loads_.rend(); ++iterator) {
        const auto markerPosition = model::loadPosition(model_, *iterator);
        if (markerPosition
            && QLineF(position, mapToScreen(geometry::project(*markerPosition, plane_))).length()
                <= 11.0) return iterator->sourceLine;
    }
    return std::nullopt;
}

auto GeometryView::transmissionLineAt(const QPointF& position) const
    -> std::optional<std::size_t>
{
    for (auto iterator = transmissionLines_.rbegin(); iterator != transmissionLines_.rend(); ++iterator) {
        const auto first = model::wireSegmentPosition(model_, iterator->wireTag1, iterator->segment1);
        const auto second = model::wireSegmentPosition(model_, iterator->wireTag2, iterator->segment2);
        if (!first || !second) continue;
        const auto start = mapToScreen(geometry::project(*first, plane_));
        const auto end = mapToScreen(geometry::project(*second, plane_));
        if (geometry::distanceToSegment({position.x(), position.y()}, {start.x(), start.y()},
                {end.x(), end.y()}) <= 8.0) return iterator->sourceLine;
    }
    return std::nullopt;
}

auto GeometryView::segmentAt(int wireTag, const QPointF& position) const -> int
{
    const auto* wire = model_.wireByTag(wireTag);
    if (wire == nullptr) {
        return 1;
    }
    const auto parameter = geometry::closestSegmentParameter(mapToWorld(position),
        geometry::project(wire->start, plane_), geometry::project(wire->end, plane_));
    return std::clamp(static_cast<int>(parameter * wire->segments) + 1, 1, wire->segments);
}

void GeometryView::drawOverlay(QPainter& painter) const
{
    if (model_.empty()) {
        painter.setPen(palette().placeholderText().color());
        painter.drawText(rect(), Qt::AlignCenter,
            tr("No valid wire geometry\nOpen a NEC file or add GW cards, then run Check Model."));
        return;
    }

    const auto summary = tr("%1 View  •  %2 wires  •  Grid %3  •  Snap %4")
        .arg(planeName(plane_))
        .arg(static_cast<qulonglong>(model_.wireCount()))
        .arg(formattedDistance(gridSpacing()))
        .arg(formattedDistance(settings_.snapSpacingMeters));
    const auto coordinates = cursorWorld_
        ? tr("Cursor  %1 %2  •  %3 %4")
              .arg(horizontalAxis(plane_))
              .arg(formattedDistance(cursorWorld_->horizontal))
              .arg(verticalAxis(plane_))
              .arg(formattedDistance(cursorWorld_->vertical))
        : tr("Cursor  —");
    const auto summaryWidth = painter.fontMetrics().horizontalAdvance(summary);
    const auto coordinateWidth = painter.fontMetrics().horizontalAdvance(coordinates);
    const auto lineHeight = painter.fontMetrics().height();
    auto summaryRectangle = QRectF(0.0, 0.0,
        static_cast<double>(std::max(summaryWidth, coordinateWidth) + 14),
        static_cast<double>(lineHeight * 2 + 10));
    summaryRectangle.moveTopLeft({12.0, 12.0});
    painter.fillRect(summaryRectangle, palette().window().color());
    painter.setPen(palette().windowText().color());
    painter.drawText(summaryRectangle.adjusted(7.0, 3.0, -7.0, -lineHeight - 2.0),
        Qt::AlignLeft | Qt::AlignVCenter, summary);
    painter.drawText(summaryRectangle.adjusted(7.0, lineHeight + 2.0, -7.0, -3.0),
        Qt::AlignLeft | Qt::AlignVCenter, coordinates);

}

void GeometryView::selectAt(const QPointF& position)
{
    const auto world = mapToWorld(position);
    const auto tolerance = 10.0 / pixelsPerMeter_;
    std::vector<int> candidates;
    for (const auto& wire : model_.wires()) {
        const auto start = geometry::project(wire.start, plane_);
        const auto end = geometry::project(wire.end, plane_);
        if (geometry::distanceToSegment(world, start, end) <= tolerance) {
            candidates.push_back(wire.tag);
        }
    }

    if (candidates.empty()) {
        selectedWireTag_.reset();
        selectedExcitationLine_.reset();
        selectedLoadLine_.reset();
        selectedTransmissionLine_.reset();
        update();
        emit wireSelected(-1);
        return;
    }

    auto selected = candidates.front();
    if (selectedWireTag_) {
        const auto current = std::ranges::find(candidates, *selectedWireTag_);
        if (current != candidates.end()) {
            if (current == std::prev(candidates.end())) {
                selected = candidates.front();
            } else {
                selected = *std::next(current);
            }
        }
    }
    selectedWireTag_ = selected;
    selectedExcitationLine_.reset();
    selectedLoadLine_.reset();
    selectedTransmissionLine_.reset();
    update();
    emit wireSelected(selected);
}

auto GeometryView::endpointAt(const QPointF& position) const
    -> std::optional<std::pair<int, model::WireEndpoint>>
{
    constexpr auto tolerance = 9.0;
    std::optional<std::pair<int, model::WireEndpoint>> closest;
    auto closestDistance = tolerance;

    const auto considerWire = [&](const model::Wire& wire) {
        for (const auto endpoint : {model::WireEndpoint::Start, model::WireEndpoint::End}) {
            const auto& point = endpoint == model::WireEndpoint::Start ? wire.start : wire.end;
            const auto distance = QLineF(position, mapToScreen(geometry::project(point, plane_))).length();
            if (distance <= closestDistance) {
                closestDistance = distance;
                closest = std::pair{wire.tag, endpoint};
            }
        }
    };

    if (selectedWireTag_) {
        if (const auto* selected = model_.wireByTag(*selectedWireTag_)) {
            considerWire(*selected);
        }
    }
    if (!closest) {
        for (const auto& wire : model_.wires()) {
            considerWire(wire);
        }
    }
    return closest;
}

auto GeometryView::wireAt(const QPointF& position) const -> std::optional<int>
{
    const auto world = mapToWorld(position);
    const auto tolerance = 10.0 / pixelsPerMeter_;
    if (selectedWireTag_) {
        if (const auto* selected = model_.wireByTag(*selectedWireTag_)) {
            if (geometry::distanceToSegment(world, geometry::project(selected->start, plane_),
                    geometry::project(selected->end, plane_)) <= tolerance) {
                return selected->tag;
            }
        }
    }

    std::optional<int> closestTag;
    auto closestDistance = tolerance;
    for (const auto& wire : model_.wires()) {
        const auto distance = geometry::distanceToSegment(world, geometry::project(wire.start, plane_),
            geometry::project(wire.end, plane_));
        if (distance <= closestDistance) {
            closestDistance = distance;
            closestTag = wire.tag;
        }
    }
    return closestTag;
}

auto GeometryView::snappedPoint(const geometry::Point2D& point, int excludedTag) const
    -> geometry::Point2D
{
    if (settings_.endpointSnapping) {
        if (const auto adjustment = endpointSnapAdjustment(point, point, excludedTag)) {
            return {point.horizontal + adjustment->horizontal, point.vertical + adjustment->vertical};
        }
    }
    if (settings_.gridSnapping) {
        const auto spacing = settings_.snapSpacingMeters;
        return {std::round(point.horizontal / spacing) * spacing,
            std::round(point.vertical / spacing) * spacing};
    }
    return point;
}

auto GeometryView::endpointSnapAdjustment(const geometry::Point2D& start,
    const geometry::Point2D& end, int excludedTag) const -> std::optional<geometry::Point2D>
{
    std::optional<geometry::Point2D> closestAdjustment;
    auto closestDistance = settings_.endpointTolerancePixels;
    for (const auto& wire : model_.wires()) {
        if (wire.tag == excludedTag) {
            continue;
        }
        for (const auto& target : {geometry::project(wire.start, plane_), geometry::project(wire.end, plane_)}) {
            for (const auto& source : {start, end}) {
                const auto distance = std::hypot(target.horizontal - source.horizontal,
                    target.vertical - source.vertical) * pixelsPerMeter_;
                if (distance <= closestDistance) {
                    closestDistance = distance;
                    closestAdjustment = geometry::Point2D{
                        target.horizontal - source.horizontal, target.vertical - source.vertical};
                }
            }
        }
    }
    return closestAdjustment;
}

auto GeometryView::splitPointAt(int tag, const QPointF& position) const -> std::optional<model::Point3D>
{
    const auto* wire = model_.wireByTag(tag);
    if (wire == nullptr) {
        return std::nullopt;
    }
    const auto start = geometry::project(wire->start, plane_);
    const auto end = geometry::project(wire->end, plane_);
    if (start == end) {
        return std::nullopt;
    }
    const auto parameter = geometry::closestSegmentParameter(mapToWorld(position), start, end);
    constexpr auto endpointTolerance = 1.0e-6;
    if (parameter <= endpointTolerance || parameter >= 1.0 - endpointTolerance) {
        return std::nullopt;
    }
    return model::Point3D{
        wire->start.x + parameter * (wire->end.x - wire->start.x),
        wire->start.y + parameter * (wire->end.y - wire->start.y),
        wire->start.z + parameter * (wire->end.z - wire->start.z)};
}

void GeometryView::updateDraggedEndpoint(const QPointF& position)
{
    if (QLineF(position, dragPressPosition_).length() < 3.0) {
        return;
    }
    auto* wire = model_.wireByTag(draggedWireTag_);
    if (wire == nullptr || !draggedEndpoint_) {
        return;
    }

    const auto projected = snappedPoint(mapToWorld(position), draggedWireTag_);
    const auto updated = geometry::withProjectedCoordinates(dragOriginal_, projected, plane_);
    const auto& other = *draggedEndpoint_ == model::WireEndpoint::Start ? wire->end : wire->start;
    if (updated == other) {
        return;
    }
    if (*draggedEndpoint_ == model::WireEndpoint::Start) {
        wire->start = updated;
    } else {
        wire->end = updated;
    }
    emit endpointPreviewed(draggedWireTag_, *draggedEndpoint_, updated);
    update();
}

void GeometryView::updateDraggedWire(const QPointF& position)
{
    if (QLineF(position, dragPressPosition_).length() < 3.0) {
        return;
    }
    auto* wire = model_.wireByTag(draggedWireTag_);
    if (wire == nullptr) {
        return;
    }

    const auto mouse = mapToWorld(position);
    const geometry::Point2D translation{
        mouse.horizontal - wireDragAnchor_.horizontal,
        mouse.vertical - wireDragAnchor_.vertical};
    const auto originalStart = geometry::project(wireDragOriginalStart_, plane_);
    const auto originalEnd = geometry::project(wireDragOriginalEnd_, plane_);
    geometry::Point2D movedStart{
        originalStart.horizontal + translation.horizontal,
        originalStart.vertical + translation.vertical};
    geometry::Point2D movedEnd{
        originalEnd.horizontal + translation.horizontal,
        originalEnd.vertical + translation.vertical};

    if (settings_.endpointSnapping) {
        if (const auto adjustment = endpointSnapAdjustment(movedStart, movedEnd, draggedWireTag_)) {
            movedStart.horizontal += adjustment->horizontal;
            movedStart.vertical += adjustment->vertical;
            movedEnd.horizontal += adjustment->horizontal;
            movedEnd.vertical += adjustment->vertical;
        } else if (settings_.gridSnapping) {
            const auto spacing = settings_.snapSpacingMeters;
            const geometry::Point2D adjustment{
                std::round(movedStart.horizontal / spacing) * spacing - movedStart.horizontal,
                std::round(movedStart.vertical / spacing) * spacing - movedStart.vertical};
            movedStart.horizontal += adjustment.horizontal;
            movedStart.vertical += adjustment.vertical;
            movedEnd.horizontal += adjustment.horizontal;
            movedEnd.vertical += adjustment.vertical;
        }
    } else if (settings_.gridSnapping) {
        const auto spacing = settings_.snapSpacingMeters;
        const geometry::Point2D adjustment{
            std::round(movedStart.horizontal / spacing) * spacing - movedStart.horizontal,
            std::round(movedStart.vertical / spacing) * spacing - movedStart.vertical};
        movedStart.horizontal += adjustment.horizontal;
        movedStart.vertical += adjustment.vertical;
        movedEnd.horizontal += adjustment.horizontal;
        movedEnd.vertical += adjustment.vertical;
    }

    wire->start = geometry::withProjectedCoordinates(wireDragOriginalStart_, movedStart, plane_);
    wire->end = geometry::withProjectedCoordinates(wireDragOriginalEnd_, movedEnd, plane_);
    emit wirePreviewed(draggedWireTag_, wire->start, wire->end);
    update();
}

}
