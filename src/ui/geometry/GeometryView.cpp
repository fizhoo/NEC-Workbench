#include "ui/geometry/GeometryView.h"

#include "ui/ColorScale.h"
#include "ui/DisplayFormat.h"
#include "model/WireGeometry.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QLineF>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
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

// Enough decimals to tell neighbouring ticks apart and no more: the fewest that write the
// step exactly, or one past its first significant digit when none does.
auto tickDecimals(double step) -> int
{
    for (auto decimals = 0; decimals <= 6; ++decimals) {
        const auto scaled = step * std::pow(10.0, decimals);
        if (std::abs(scaled - std::round(scaled)) <= 1.0e-6 * scaled) return decimals;
    }
    return std::clamp(static_cast<int>(std::ceil(-std::log10(step))) + 1, 0, 9);
}

}

auto gridTickLabel(double meters, double spacingMeters, model::LengthUnit unit) -> QString
{
    // Stepping across the grid accumulates rounding, so the tick at zero can arrive as
    // -2.776e-16; anything that close to zero is zero.
    if (std::abs(meters) < 1.0e-6 * spacingMeters) meters = 0.0;
    return QString::number(model::fromMeters(meters, unit), 'f',
        tickDecimals(model::fromMeters(spacingMeters, unit)));
}

namespace {

auto wireLabel(const model::Wire& wire) -> QString
{
    const auto mnemonic = wire.geometryKind == model::WireGeometryKind::Arc ? "GA"
        : wire.geometryKind == model::WireGeometryKind::Helix ? "GH" : "GW";
    return QStringLiteral("%1 %2").arg(QString::fromLatin1(mnemonic)).arg(wire.tag);
}

}

GeometryView::GeometryView(geometry::ProjectionPlane plane, QWidget* parent)
    : GeometryInteractionView(parent)
    , plane_(plane)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    // Narrow enough that a widened table column leaves the geometry on screen.
    setMinimumSize(200, 240);
    setAccessibleName(planeName(plane_) + tr(" geometry view"));
    setToolTip(tr("Drag endpoint to reshape; drag wire to move; middle/Alt+drag to pan; wheel to zoom"));
}

void GeometryView::setModel(const model::AntennaModel& model)
{
    setInteractionModel(model, true);
    fitToView();
}

void GeometryView::updateModel(const model::AntennaModel& model)
{
    setInteractionModel(model, false);
    changed();
}

void GeometryView::fitToView()
{
    keepFitted_ = true;
    const auto bounds = geometry::projectedBounds(model_, plane_);
    if (!bounds) {
        worldCenter_ = {};
        pixelsPerMeter_ = 50.0;
        changed();
        return;
    }

    const auto center = bounds->center();
    worldCenter_ = {center.horizontal, center.vertical};
    // A plane that sees the model end-on, as XZ sees a wire along Y, shows it at the scale of
    // the whole model rather than zooming in on a point.
    const auto largestExtent = std::max({bounds->width(), bounds->height(),
        geometry::modelExtent(model_), 1.0e-6});
    const auto horizontalExtent = std::max(bounds->width(), largestExtent * 0.1);
    const auto verticalExtent = std::max(bounds->height(), largestExtent * 0.1);
    const auto availableWidth = std::max(100, width() - 100);
    const auto availableHeight = std::max(100, height() - 100);
    pixelsPerMeter_ = std::clamp(std::min(availableWidth / horizontalExtent, availableHeight / verticalExtent),
        0.02, 1.0e6);
    changed();
}

void GeometryView::setLengthUnit(model::LengthUnit unit)
{
    settings_.lengthUnit = unit;
    changed();
}

void GeometryView::setSnapSpacing(double meters)
{
    settings_.snapSpacingMeters = std::max(meters, 1.0e-12);
    changed();
}

void GeometryView::setSettings(const GeometrySettings& settings)
{
    settings_ = settings;
    settings_.manualGridSpacingMeters = std::max(settings_.manualGridSpacingMeters, 1.0e-12);
    settings_.snapSpacingMeters = std::max(settings_.snapSpacingMeters, 1.0e-12);
    settings_.minorGridDivisions = std::max(settings_.minorGridDivisions, 1);
    changed();
}

auto GeometryView::wireMenuOptions(int tag, const QPointF& position) const
    -> GeometryWireMenuOptions
{
    const auto* wire = model_.wireByTag(tag);
    return {
        .editable = wire != nullptr && wire->editable,
        .transmissionLinePending = pendingTransmissionLineEndpoint_.has_value(),
        .splitAvailable = splitPointAt(tag, position).has_value(),
        .allowSplit = true,
        .allowDelete = true,
    };
}

void GeometryView::handleWireContextAction(
    GeometryContextAction action, int tag, const QPointF& position)
{
    if (action == GeometryContextAction::SplitWire) {
        if (const auto splitPoint = splitPointAt(tag, position)) {
            emit splitWireRequested(tag, *splitPoint);
        }
    } else if (action == GeometryContextAction::Delete) {
        emit deleteWireRequested(tag);
    }
}

void GeometryView::showEmptyContextMenu(QContextMenuEvent* event)
{
    QMenu menu(this);
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
        keepFitted_ = false;
        lastMousePosition_ = event->position();
        cursorWorld_ = mapToWorld(event->position());
        changed();
        event->accept();
        return;
    }
    if (loadAt(event->position()) || transmissionLineAt(event->position())
        || networkAt(event->position()) || excitationAt(event->position())) {
        setCursor(Qt::PointingHandCursor);
    } else if (endpointAt(event->position())) {
        setCursor(Qt::OpenHandCursor);
    } else if (wireAt(event->position())) {
        setCursor(Qt::SizeAllCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
    changed();
    QWidget::mouseMoveEvent(event);
}

void GeometryView::leaveEvent(QEvent* event)
{
    cursorWorld_.reset();
    changed();
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
        if (const auto sourceLine = networkAt(event->position())) {
            selectNetwork(*sourceLine);
            emit networkSelected(*sourceLine);
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
            changed();
            event->accept();
            return;
        }
        if (const auto tag = wireAt(event->position())) {
            const auto* selected = model_.wireByTag(*tag);
            if (selected == nullptr || !selected->editable) {
                selectAt(event->position());
                event->accept();
                return;
            }
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
            changed();
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
    drawSurfacePatches(painter);
    // Labels stay inside the view, the later ones moving clear of the earlier.
    labels_.reset(QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0));
    drawWires(painter);
    drawAttachments(painter);
    drawExcitations(painter);
    drawEmptyMessage(painter);
}

void GeometryView::wheelEvent(QWheelEvent* event)
{
    const auto worldBeforeZoom = mapToWorld(event->position());
    const auto zoomFactor = std::pow(1.0015, event->angleDelta().y());
    pixelsPerMeter_ = std::clamp(pixelsPerMeter_ * zoomFactor, 0.02, 1.0e6);
    keepFitted_ = false;
    const auto worldAfterZoom = mapToWorld(event->position());
    worldCenter_.rx() += worldBeforeZoom.horizontal - worldAfterZoom.horizontal;
    worldCenter_.ry() += worldBeforeZoom.vertical - worldAfterZoom.vertical;
    changed();
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
        .arg(formatDecimal(model::fromMeters(meters, settings_.lengthUnit)))
        .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

auto GeometryView::labelledTicks(Qt::Orientation orientation) const -> std::vector<Tick>
{
    std::vector<Tick> ticks;
    if (!settings_.showGrid || !settings_.showLabels) return ticks;
    const auto topLeft = mapToWorld({0.0, 0.0});
    const auto bottomRight = mapToWorld({static_cast<double>(width()), static_cast<double>(height())});
    const auto spacing = gridSpacing();
    if (orientation == Qt::Horizontal) {
        for (auto horizontal = std::floor(topLeft.horizontal / spacing) * spacing;
             horizontal <= bottomRight.horizontal; horizontal += spacing) {
            const auto x = mapToScreen({horizontal, 0.0}).x();
            if (x >= 4.0 && x <= width() - 40.0)
                ticks.push_back({x, gridTickLabel(horizontal, spacing, settings_.lengthUnit)});
        }
    } else {
        for (auto vertical = std::floor(bottomRight.vertical / spacing) * spacing;
             vertical <= topLeft.vertical; vertical += spacing) {
            const auto y = mapToScreen({0.0, vertical}).y();
            if (y >= 15.0 && y <= height() - 8.0)
                ticks.push_back({y, gridTickLabel(vertical, spacing, settings_.lengthUnit)});
        }
    }
    return ticks;
}

auto GeometryView::tickLabels(Qt::Orientation orientation) const -> QStringList
{
    QStringList labels;
    for (const auto& tick : labelledTicks(orientation)) labels << tick.label;
    return labels;
}

auto GeometryView::screenPoint(const model::Point3D& point) const -> QPointF
{
    return mapToScreen(geometry::project(point, plane_));
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
        }
        for (auto vertical = firstVertical; vertical <= topLeft.vertical; vertical += spacing) {
            const auto y = mapToScreen({0.0, vertical}).y();
            painter.drawLine(QPointF{0.0, y}, QPointF{static_cast<double>(width()), y});
        }
        painter.setPen(palette().placeholderText().color());
        for (const auto& tick : labelledTicks(Qt::Horizontal))
            painter.drawText(QPointF{tick.screen + 3.0, height() - 7.0}, tick.label);
        for (const auto& tick : labelledTicks(Qt::Vertical))
            painter.drawText(QPointF{5.0, tick.screen - 3.0}, tick.label);
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

void GeometryView::drawSurfacePatches(QPainter& painter) const
{
    auto fill = QColor(45, 165, 145, 80);
    const auto outline = QColor(25, 125, 110);
    painter.setPen(QPen(outline, 1.5));
    painter.setBrush(fill);
    for (const auto& patch : model_.surfacePatches()) {
        QPolygonF polygon;
        polygon.reserve(static_cast<qsizetype>(patch.corners.size()));
        for (const auto& corner : patch.corners)
            polygon << mapToScreen(geometry::project(corner, plane_));
        if (polygon.size() >= 3) painter.drawPolygon(polygon);
    }
}

void GeometryView::drawWires(QPainter& painter) const
{
    const QColor wireColor(40, 105, 170);
    const QColor selectedColor(230, 126, 34);
    const auto currents = showsCurrents();
    // Under the current colours the wires are a neutral base, so a segment without a current
    // still shows; the selected wire gets a halo instead of a hue.
    auto plainColor = palette().color(QPalette::Mid);
    const auto colorOf = [&](bool selected) {
        return currents ? plainColor : selected ? selectedColor : wireColor;
    };
    const auto drawPath = [&](const model::Wire& wire) {
        const auto pointCount = model::wirePathPointCount(wire);
        for (auto point = std::size_t{1}; point < pointCount; ++point)
            painter.drawLine(mapToScreen(geometry::project(model::wirePathPoint(wire, point - 1), plane_)),
                mapToScreen(geometry::project(model::wirePathPoint(wire, point), plane_)));
    };
    for (const auto& wire : model_.wires()) {
        const bool selected = selectedWireTag_ == wire.tag;
        if (selected && currents) {
            painter.setPen(selectionHaloPen(10.0));
            drawPath(wire);
        }
        painter.setPen(QPen(colorOf(selected), selected ? 4.0 : 2.5, Qt::SolidLine, Qt::RoundCap));
        drawPath(wire);
    }
    if (currents) {
        for (const auto& segment : currents_->segments) {
            const auto selected = selectedWireTag_ == segment.wireTag;
            painter.setPen(QPen(currentColor(segment.magnitude, currents_->maximumMagnitude),
                selected ? 5.0 : 4.0, Qt::SolidLine, Qt::FlatCap));
            painter.drawLine(mapToScreen(geometry::project(segment.start, plane_)),
                mapToScreen(geometry::project(segment.end, plane_)));
        }
    }
    for (const auto& wire : model_.wires()) {
        const bool selected = selectedWireTag_ == wire.tag;
        const auto color = currents && selected ? palette().color(QPalette::Text) : colorOf(selected);
        const auto pointCount = model::wirePathPointCount(wire);
        const auto start = mapToScreen(geometry::project(model::wirePathPoint(wire, 0), plane_));
        const auto end = mapToScreen(geometry::project(
            model::wirePathPoint(wire, pointCount - 1), plane_));
        painter.setPen(QPen(color, 1.0));
        painter.setBrush(color);
        painter.drawEllipse(start, selected ? 4.5 : 3.5, selected ? 4.5 : 3.5);
        painter.drawEllipse(end, selected ? 4.5 : 3.5, selected ? 4.5 : 3.5);

        const auto middlePoint = model::wirePathPoint(wire, pointCount / 2);
        const auto midpoint = mapToScreen(geometry::project(middlePoint, plane_)) + QPointF{8.0, -8.0};
        const auto label = wireLabel(wire);
        auto labelRectangle = QRectF(painter.fontMetrics().boundingRect(label).adjusted(-3, -2, 3, 2));
        labelRectangle.moveTopLeft(midpoint);
        labelRectangle = labels_.place(labelRectangle);
        painter.fillRect(labelRectangle, palette().base().color());
        if (selected && currents) {
            painter.setPen(selectionHaloPen(1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(labelRectangle);
        }
        painter.setPen(color);
        painter.drawText(labelRectangle, Qt::AlignCenter, label);
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
        if (selected && showsCurrents()) {
            painter.setPen(selectionHaloPen(7.0));
            painter.drawPolygon(diamond);
        }
        painter.setPen(QPen(selected ? QColor(230, 100, 25) : QColor(190, 45, 55), selected ? 3.0 : 2.0));
        painter.setBrush(QColor(255, 235, 120));
        painter.drawPolygon(diamond);
        painter.setPen(QColor(150, 30, 40));
        drawMarkerLabel(painter, screen, tr("EX"));
    }
}

void GeometryView::drawMarkerLabel(QPainter& painter, const QPointF& marker, const QString& text) const
{
    const auto wanted = QRectF(painter.fontMetrics().boundingRect(text)).translated(marker + QPointF{9.0, -7.0});
    painter.drawText(labels_.place(wanted), Qt::AlignCenter, text);
}

void GeometryView::drawAttachments(QPainter& painter) const
{
    drawConnectors(painter, [this](const model::Point3D& point) {
        return mapToScreen(geometry::project(point, plane_));
    });
    for (const auto& load : loads_) {
        const auto position = model::loadPosition(model_, load);
        if (!position) continue;
        const auto screen = mapToScreen(geometry::project(*position, plane_));
        const auto selected = selectedLoadLine_ == load.sourceLine;
        const auto halfSize = selected ? 8.0 : 6.0;
        const QRectF marker(screen.x() - halfSize, screen.y() - halfSize,
            halfSize * 2.0, halfSize * 2.0);
        if (selected && showsCurrents()) {
            painter.setPen(selectionHaloPen(7.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(marker);
        }
        painter.setPen(QPen(selected ? QColor(225, 105, 25) : QColor(45, 145, 115),
            selected ? 3.0 : 2.0));
        painter.setBrush(QColor(205, 245, 225));
        painter.drawRect(marker);
        painter.setPen(QColor(25, 105, 80));
        drawMarkerLabel(painter, screen, tr("LD"));
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

auto GeometryView::networkAt(const QPointF& position) const -> std::optional<std::size_t>
{
    for (auto iterator = networks_.rbegin(); iterator != networks_.rend(); ++iterator) {
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
    if (wire->path.size() == static_cast<std::size_t>(wire->segments + 1)) {
        auto closestSegment = 1;
        auto closestDistance = std::numeric_limits<double>::infinity();
        const auto world = mapToWorld(position);
        for (auto segment = 1; segment <= wire->segments; ++segment) {
            const auto endpoints = model::wireSegmentEndpoints(*wire, segment);
            const auto distance = geometry::distanceToSegment(world,
                geometry::project(endpoints->first, plane_),
                geometry::project(endpoints->second, plane_));
            if (distance < closestDistance) {
                closestDistance = distance;
                closestSegment = segment;
            }
        }
        return closestSegment;
    }
    const auto parameter = geometry::closestSegmentParameter(mapToWorld(position),
        geometry::project(wire->start, plane_), geometry::project(wire->end, plane_));
    return std::clamp(static_cast<int>(parameter * wire->segments) + 1, 1, wire->segments);
}

void GeometryView::drawEmptyMessage(QPainter& painter) const
{
    if (!model_.empty()) return;
    painter.setPen(palette().placeholderText().color());
    painter.drawText(rect(), Qt::AlignCenter,
        tr("No valid geometry\nOpen a NEC file or add geometry cards, then run Check Model."));
}

auto GeometryView::infoText() const -> QString
{
    if (model_.empty()) return {};
    QStringList parts{geometryCounts(model_)};
    parts << tr("Grid %1").arg(formattedDistance(gridSpacing()))
          << tr("Snap %1").arg(formattedDistance(settings_.snapSpacingMeters));
    // A model this plane sees as a single point, shown at the whole model's scale.
    if (const auto bounds = geometry::projectedBounds(model_, plane_)) {
        const auto flat = 1.0e-6 * std::max(geometry::modelExtent(model_), 1.0e-12);
        if (std::max(bounds->width(), bounds->height()) <= flat) parts << tr("seen end-on");
    }
    // Before the cursor, which is the first to go when the line is too long.
    if (!currentsNote().isEmpty()) parts << currentsNote();
    parts << (cursorWorld_
        ? tr("%1 %2  %3 %4").arg(horizontalAxis(plane_), formattedDistance(cursorWorld_->horizontal),
              verticalAxis(plane_), formattedDistance(cursorWorld_->vertical))
        : tr("Cursor —"));
    return parts.join(QStringLiteral(" · "));
}

void GeometryView::changed()
{
    update();
    emit infoChanged();
}

void GeometryView::selectAt(const QPointF& position)
{
    const auto world = mapToWorld(position);
    const auto tolerance = 10.0 / pixelsPerMeter_;
    std::vector<int> candidates;
    for (const auto& wire : model_.wires()) {
        for (auto point = std::size_t{1}; point < model::wirePathPointCount(wire); ++point) {
            const auto start = geometry::project(model::wirePathPoint(wire, point - 1), plane_);
            const auto end = geometry::project(model::wirePathPoint(wire, point), plane_);
            if (geometry::distanceToSegment(world, start, end) <= tolerance) {
                candidates.push_back(wire.tag);
                break;
            }
        }
    }

    if (candidates.empty()) {
        selectedWireTag_.reset();
        selectedExcitationLine_.reset();
        selectedLoadLine_.reset();
        selectedTransmissionLine_.reset();
        changed();
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
    changed();
    emit wireSelected(selected);
}

auto GeometryView::endpointAt(const QPointF& position) const
    -> std::optional<std::pair<int, model::WireEndpoint>>
{
    constexpr auto tolerance = 9.0;
    std::optional<std::pair<int, model::WireEndpoint>> closest;
    auto closestDistance = tolerance;

    const auto considerWire = [&](const model::Wire& wire) {
        if (!wire.editable) return;
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
    std::optional<int> closestTag;
    auto closestDistance = tolerance;
    for (const auto& wire : model_.wires()) {
        for (auto point = std::size_t{1}; point < model::wirePathPointCount(wire); ++point) {
            const auto distance = geometry::distanceToSegment(world,
                geometry::project(model::wirePathPoint(wire, point - 1), plane_),
                geometry::project(model::wirePathPoint(wire, point), plane_));
            if (distance <= closestDistance) {
                closestDistance = distance;
                closestTag = wire.tag;
            }
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
    if (wire == nullptr || !wire->editable) {
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
    changed();
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
    changed();
}

}
