#include "ui/geometry/Geometry3DView.h"

#include <QContextMenuEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
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
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(420, 320);
    setCursor(Qt::OpenHandCursor);
    setAccessibleName(tr("3D model geometry view"));
    setToolTip(tr("Left-drag to orbit; middle/Shift+drag to pan; wheel to zoom; click a wire to select"));
}

void Geometry3DView::setModel(const model::AntennaModel& model)
{
    model_ = model;
    selectedWireTag_.reset();
    updateModelCenter();
    fitToView();
}

void Geometry3DView::updateModel(const model::AntennaModel& model)
{
    model_ = model;
    if (selectedWireTag_ && model_.wireByTag(*selectedWireTag_) == nullptr) {
        selectedWireTag_.reset();
    }
    updateModelCenter();
    update();
}

void Geometry3DView::selectWire(int tag)
{
    selectedWireTag_ = model_.wireByTag(tag) == nullptr ? std::nullopt : std::optional<int>{tag};
    selectedExcitationLine_.reset();
    update();
}

void Geometry3DView::setLengthUnit(model::LengthUnit unit)
{
    lengthUnit_ = unit;
    update();
}

void Geometry3DView::setExcitations(const std::vector<model::Excitation>& excitations)
{
    excitations_ = excitations;
    if (selectedExcitationLine_
        && std::ranges::find(excitations_, *selectedExcitationLine_, &model::Excitation::sourceLine)
            == excitations_.end()) {
        selectedExcitationLine_.reset();
    }
    update();
}

void Geometry3DView::selectExcitation(std::size_t sourceLine)
{
    const auto found = std::ranges::find(excitations_, sourceLine, &model::Excitation::sourceLine);
    selectedExcitationLine_ = found == excitations_.end()
        ? std::nullopt : std::optional<std::size_t>{sourceLine};
    selectedWireTag_.reset();
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
        for (const auto& point : {wire.start, wire.end}) {
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

void Geometry3DView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    if (const auto sourceLine = excitationAt(event->pos())) {
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
        emit wireSelected(*tag);
        auto* propertiesAction = menu.addAction(tr("Properties…"));
        menu.addSeparator();
        auto* addSourceAction = menu.addAction(tr("Add Voltage Source Here"));
        auto* fitAction = menu.addAction(tr("Fit 3D View"));
        const auto* selectedAction = menu.exec(event->globalPos());
        if (selectedAction == propertiesAction) {
            emit wirePropertiesRequested(*tag);
        } else if (selectedAction == addSourceAction) {
            emit addExcitationRequested(*tag, segmentAt(*tag, event->pos()));
        } else if (selectedAction == fitAction) {
            fitToView();
        }
    } else {
        auto* fitAction = menu.addAction(tr("Fit 3D View"));
        auto* isometricAction = menu.addAction(tr("Isometric View"));
        const auto* selectedAction = menu.exec(event->globalPos());
        if (selectedAction == fitAction) {
            fitToView();
        } else if (selectedAction == isometricAction) {
            setIsometricView();
        }
    }
    event->accept();
}

void Geometry3DView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        fitToView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
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
            if (const auto sourceLine = excitationAt(event->position())) {
                selectExcitation(*sourceLine);
                emit excitationSelected(*sourceLine);
            } else {
                const auto tag = wireAt(event->position());
                selectedWireTag_ = tag;
                selectedExcitationLine_.reset();
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
            tr("No valid wire geometry\nOpen a NEC file or add GW cards, then run Check Model."));
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

    struct RenderWire {
        const model::Wire* wire{};
        CameraPoint start;
        CameraPoint end;
    };
    std::vector<RenderWire> renderWires;
    renderWires.reserve(model_.wireCount());
    for (const auto& wire : model_.wires()) {
        renderWires.push_back({&wire, project(wire.start), project(wire.end)});
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
        painter.drawEllipse(rendered.start.screen, endpointRadius, endpointRadius);
        painter.drawEllipse(rendered.end.screen, endpointRadius, endpointRadius);
        if (selected) {
            const auto midpoint = (rendered.start.screen + rendered.end.screen) / 2.0;
            painter.drawText(midpoint + QPointF{8.0, -8.0}, QStringLiteral("GW %1").arg(rendered.wire->tag));
        }
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

    const auto overlay = tr("3D Model  •  %1 wires  •  Extent %2\nLeft-drag orbit  •  Shift/middle-drag pan  •  Wheel zoom  •  Click select")
        .arg(static_cast<qulonglong>(model_.wireCount()))
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
        const auto distance = distanceToSegment(position, project(wire.start).screen, project(wire.end).screen);
        if (distance < closestDistance) {
            closestDistance = distance;
            closestTag = wire.tag;
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

auto Geometry3DView::segmentAt(int wireTag, const QPointF& position) const -> int
{
    const auto* wire = model_.wireByTag(wireTag);
    if (wire == nullptr) {
        return 1;
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
        .arg(QString::number(model::fromMeters(meters, lengthUnit_), 'g', 5))
        .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

void Geometry3DView::updateModelCenter()
{
    if (model_.empty()) {
        modelCenter_ = {};
        modelExtent_ = 1.0;
        return;
    }
    auto minimum = model_.wires().front().start;
    auto maximum = minimum;
    for (const auto& wire : model_.wires()) {
        for (const auto& point : {wire.start, wire.end}) {
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
