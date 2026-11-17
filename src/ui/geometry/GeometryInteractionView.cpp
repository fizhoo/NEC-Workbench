#include "ui/geometry/GeometryInteractionView.h"

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>
#include <QToolTip>

#include <algorithm>

namespace necwb::ui {
namespace {

void addResultAction(QMenu& menu, const QString& text, GeometryContextAction result,
    bool enabled = true)
{
    auto* action = menu.addAction(text);
    action->setData(static_cast<int>(result));
    action->setEnabled(enabled);
}

auto execute(QMenu& menu, const QPoint& globalPosition) -> GeometryContextAction
{
    const auto* action = menu.exec(globalPosition);
    return action == nullptr ? GeometryContextAction::None
                             : static_cast<GeometryContextAction>(action->data().toInt());
}

auto showAttachmentContextMenu(QWidget* parent, const QPoint& position, const QString& deleteText)
    -> GeometryContextAction
{
    QMenu menu(parent);
    addResultAction(menu, QWidget::tr("Properties…"), GeometryContextAction::Properties);
    menu.addSeparator();
    addResultAction(menu, deleteText, GeometryContextAction::Delete);
    return execute(menu, position);
}

auto showExcitationContextMenu(QWidget* parent, const QPoint& position) -> GeometryContextAction
{
    QMenu menu(parent);
    addResultAction(menu, QWidget::tr("Properties…"), GeometryContextAction::Properties);
    menu.addSeparator();
    addResultAction(menu, QWidget::tr("Delete Voltage Source"), GeometryContextAction::Delete);
    return execute(menu, position);
}

auto showWireContextMenu(QWidget* parent, const QPoint& position,
    const GeometryWireMenuOptions& options) -> GeometryContextAction
{
    QMenu menu(parent);
    addResultAction(menu, QWidget::tr("Properties…"), GeometryContextAction::Properties,
        options.editable);
    menu.addSeparator();
    addResultAction(menu, QWidget::tr("Add Voltage Source Here"),
        GeometryContextAction::AddExcitation);
    addResultAction(menu, QWidget::tr("Add Load Here"), GeometryContextAction::AddLoad);
    addResultAction(menu, options.transmissionLinePending
            ? QWidget::tr("Complete Transmission Line Here")
            : QWidget::tr("Start Transmission Line Here"),
        GeometryContextAction::TransmissionLineEndpoint);
    if (options.transmissionLinePending) {
        addResultAction(menu, QWidget::tr("Cancel Transmission Line"),
            GeometryContextAction::CancelTransmissionLine);
    }
    if (options.allowSplit) {
        addResultAction(menu, QWidget::tr("Split Wire Here"), GeometryContextAction::SplitWire,
            options.editable && options.splitAvailable);
    }
    if (options.allowDelete) {
        addResultAction(menu, QWidget::tr("Delete Wire"), GeometryContextAction::Delete,
            options.editable);
    }
    if (options.allowFit) {
        addResultAction(menu, QWidget::tr("Fit 3D View"), GeometryContextAction::FitView);
    }
    return execute(menu, position);
}

}

void GeometryInteractionView::selectWire(int tag)
{
    clearSelection();
    selectedWireTag_ = model_.wireByTag(tag) == nullptr ? std::nullopt : std::optional<int>{tag};
    update();
}

void GeometryInteractionView::setExcitations(const std::vector<model::Excitation>& excitations)
{
    excitations_ = excitations;
    if (selectedExcitationLine_
        && std::ranges::find(excitations_, *selectedExcitationLine_, &model::Excitation::sourceLine)
            == excitations_.end()) {
        selectedExcitationLine_.reset();
    }
    update();
}

void GeometryInteractionView::setAttachments(const std::vector<model::LoadDefinition>& loads,
    const std::vector<model::TransmissionLineDefinition>& transmissionLines,
    const std::vector<model::NetworkDefinition>& networks)
{
    loads_ = loads;
    transmissionLines_ = transmissionLines;
    networks_ = networks;
    if (selectedLoadLine_
        && std::ranges::find(loads_, *selectedLoadLine_, &model::LoadDefinition::sourceLine)
            == loads_.end()) selectedLoadLine_.reset();
    if (selectedTransmissionLine_
        && std::ranges::find(transmissionLines_, *selectedTransmissionLine_,
            &model::TransmissionLineDefinition::sourceLine) == transmissionLines_.end()) {
        selectedTransmissionLine_.reset();
    }
    if (selectedNetworkLine_
        && std::ranges::find(networks_, *selectedNetworkLine_,
            &model::NetworkDefinition::sourceLine) == networks_.end()) {
        selectedNetworkLine_.reset();
    }
    update();
}

void GeometryInteractionView::setSharedSegments(
    const std::vector<model::SharedSegment>& sharedSegments)
{
    sharedSegments_ = sharedSegments;
}

auto GeometryInteractionView::event(QEvent* event) -> bool
{
    if (event->type() == QEvent::ToolTip && !sharedSegments_.empty()) {
        const auto* help = static_cast<QHelpEvent*>(event);
        if (const auto tag = wireAt(help->pos())) {
            const auto segment = segmentAt(*tag, help->pos());
            QStringList notes;
            for (const auto& shared : sharedSegments_) {
                // Rich text so Qt wraps the long descriptions.
                if (shared.wireTag == *tag && shared.wireSegment == segment) {
                    notes.append(QStringLiteral("<p>%1</p>").arg(
                        QString::fromStdString(shared.description).toHtmlEscaped()));
                }
            }
            if (!notes.isEmpty()) {
                QToolTip::showText(help->globalPos(), notes.join(QString()), this);
                return true;
            }
        }
    }
    return QWidget::event(event);
}

void GeometryInteractionView::setPendingTransmissionLineEndpoint(
    std::optional<std::pair<int, int>> endpoint)
{
    pendingTransmissionLineEndpoint_ = endpoint;
    update();
}

void GeometryInteractionView::selectExcitation(std::size_t sourceLine)
{
    clearSelection();
    const auto found = std::ranges::find(excitations_, sourceLine, &model::Excitation::sourceLine);
    selectedExcitationLine_ = found == excitations_.end()
        ? std::nullopt : std::optional<std::size_t>{sourceLine};
    update();
}

void GeometryInteractionView::selectLoad(std::size_t sourceLine)
{
    clearSelection();
    const auto found = std::ranges::find(loads_, sourceLine, &model::LoadDefinition::sourceLine);
    selectedLoadLine_ = found == loads_.end() ? std::nullopt : std::optional{sourceLine};
    update();
}

void GeometryInteractionView::selectTransmissionLine(std::size_t sourceLine)
{
    clearSelection();
    const auto found = std::ranges::find(transmissionLines_, sourceLine,
        &model::TransmissionLineDefinition::sourceLine);
    selectedTransmissionLine_ = found == transmissionLines_.end()
        ? std::nullopt : std::optional{sourceLine};
    update();
}

void GeometryInteractionView::selectNetwork(std::size_t sourceLine)
{
    clearSelection();
    const auto found = std::ranges::find(networks_, sourceLine,
        &model::NetworkDefinition::sourceLine);
    selectedNetworkLine_ = found == networks_.end() ? std::nullopt : std::optional{sourceLine};
    update();
}

void GeometryInteractionView::clearSelection()
{
    selectedWireTag_.reset();
    selectedExcitationLine_.reset();
    selectedLoadLine_.reset();
    selectedTransmissionLine_.reset();
    selectedNetworkLine_.reset();
}

void GeometryInteractionView::setInteractionModel(
    const model::AntennaModel& model, bool resetWireSelection)
{
    model_ = model;
    if (resetWireSelection || (selectedWireTag_ && model_.wireByTag(*selectedWireTag_) == nullptr)) {
        selectedWireTag_.reset();
    }
}

void GeometryInteractionView::contextMenuEvent(QContextMenuEvent* event)
{
    if (const auto sourceLine = loadAt(event->pos())) {
        selectLoad(*sourceLine);
        emit loadSelected(*sourceLine);
        const auto action = showAttachmentContextMenu(this, event->globalPos(), tr("Delete Load"));
        if (action == GeometryContextAction::Properties) emit editLoadRequested(*sourceLine);
        else if (action == GeometryContextAction::Delete) emit deleteLoadRequested(*sourceLine);
    } else if (const auto sourceLine = transmissionLineAt(event->pos())) {
        selectTransmissionLine(*sourceLine);
        emit transmissionLineSelected(*sourceLine);
        const auto action = showAttachmentContextMenu(
            this, event->globalPos(), tr("Delete Transmission Line"));
        if (action == GeometryContextAction::Properties) {
            emit editTransmissionLineRequested(*sourceLine);
        } else if (action == GeometryContextAction::Delete) {
            emit deleteTransmissionLineRequested(*sourceLine);
        }
    } else if (const auto sourceLine = networkAt(event->pos())) {
        selectNetwork(*sourceLine);
        emit networkSelected(*sourceLine);
        const auto action = showAttachmentContextMenu(
            this, event->globalPos(), tr("Delete Network"));
        if (action == GeometryContextAction::Properties) emit editNetworkRequested(*sourceLine);
        else if (action == GeometryContextAction::Delete) emit deleteNetworkRequested(*sourceLine);
    } else if (const auto sourceLine = excitationAt(event->pos())) {
        selectExcitation(*sourceLine);
        emit excitationSelected(*sourceLine);
        const auto action = showExcitationContextMenu(this, event->globalPos());
        if (action == GeometryContextAction::Properties) {
            emit editExcitationRequested(*sourceLine);
        } else if (action == GeometryContextAction::Delete) {
            emit deleteExcitationRequested(*sourceLine);
        }
    } else if (const auto tag = wireAt(event->pos())) {
        selectWire(*tag);
        emit wireSelected(*tag);
        const auto action = showWireContextMenu(
            this, event->globalPos(), wireMenuOptions(*tag, event->pos()));
        if (action == GeometryContextAction::Properties) {
            emit wirePropertiesRequested(*tag);
        } else if (action == GeometryContextAction::AddExcitation) {
            emit addExcitationRequested(*tag, segmentAt(*tag, event->pos()));
        } else if (action == GeometryContextAction::AddLoad) {
            emit addLoadRequested(*tag, segmentAt(*tag, event->pos()));
        } else if (action == GeometryContextAction::TransmissionLineEndpoint) {
            emit transmissionLineEndpointRequested(*tag, segmentAt(*tag, event->pos()));
        } else if (action == GeometryContextAction::CancelTransmissionLine) {
            emit cancelTransmissionLineRequested();
        } else {
            handleWireContextAction(action, *tag, event->pos());
        }
    } else {
        showEmptyContextMenu(event);
    }
    event->accept();
}

void GeometryInteractionView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        fitToView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void GeometryInteractionView::drawConnectors(QPainter& painter,
    const std::function<QPointF(const model::Point3D&)>& toScreen) const
{
    const auto connector = [&](int tag1, int segment1, int tag2, int segment2, bool selected,
                               const QColor& color, Qt::PenStyle style, QPointF labelOffset,
                               const QString& label) {
        const auto first = model::wireSegmentPosition(model_, tag1, segment1);
        const auto second = model::wireSegmentPosition(model_, tag2, segment2);
        if (!first || !second) return;
        const auto start = toScreen(*first);
        const auto end = toScreen(*second);
        const auto radius = selected ? 5.0 : 4.0;
        painter.setPen(QPen(selected ? QColor(225, 105, 25) : color, selected ? 3.0 : 2.0, style));
        painter.setBrush(palette().base());
        painter.drawLine(start, end);
        painter.drawEllipse(start, radius, radius);
        painter.drawEllipse(end, radius, radius);
        painter.drawText((start + end) / 2.0 + labelOffset, label);
    };
    for (const auto& line : transmissionLines_)
        connector(line.wireTag1, line.segment1, line.wireTag2, line.segment2,
            selectedTransmissionLine_ == line.sourceLine, QColor(125, 70, 175), Qt::DashLine,
            {6.0, -6.0}, tr("TL"));
    for (const auto& network : networks_)
        connector(network.wireTag1, network.segment1, network.wireTag2, network.segment2,
            selectedNetworkLine_ == network.sourceLine, QColor(20, 140, 140), Qt::DashDotLine,
            {6.0, 12.0}, tr("NT"));
    if (!pendingTransmissionLineEndpoint_) return;
    const auto position = model::wireSegmentPosition(model_,
        pendingTransmissionLineEndpoint_->first, pendingTransmissionLineEndpoint_->second);
    if (!position) return;
    const auto screen = toScreen(*position);
    painter.setPen(QPen(QColor(125, 70, 175), 2.5, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(screen, 9.0, 9.0);
    painter.drawText(screen + QPointF{11.0, -8.0}, tr("TL start"));
}

}
