#include "ui/geometry/GeometryInteractionView.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>

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
    addResultAction(menu, QWidget::tr("Edit in Model Setup"), GeometryContextAction::EditInSetup);
    menu.addSeparator();
    addResultAction(menu, deleteText, GeometryContextAction::Delete);
    return execute(menu, position);
}

auto showExcitationContextMenu(QWidget* parent, const QPoint& position) -> GeometryContextAction
{
    QMenu menu(parent);
    addResultAction(menu, QWidget::tr("Properties…"), GeometryContextAction::Properties);
    addResultAction(menu, QWidget::tr("Edit in Setup"), GeometryContextAction::EditInSetup);
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
    const std::vector<model::TransmissionLineDefinition>& transmissionLines)
{
    loads_ = loads;
    transmissionLines_ = transmissionLines;
    if (selectedLoadLine_
        && std::ranges::find(loads_, *selectedLoadLine_, &model::LoadDefinition::sourceLine)
            == loads_.end()) selectedLoadLine_.reset();
    if (selectedTransmissionLine_
        && std::ranges::find(transmissionLines_, *selectedTransmissionLine_,
            &model::TransmissionLineDefinition::sourceLine) == transmissionLines_.end()) {
        selectedTransmissionLine_.reset();
    }
    update();
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

void GeometryInteractionView::clearSelection()
{
    selectedWireTag_.reset();
    selectedExcitationLine_.reset();
    selectedLoadLine_.reset();
    selectedTransmissionLine_.reset();
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
        if (action == GeometryContextAction::EditInSetup) emit editLoadRequested(*sourceLine);
        else if (action == GeometryContextAction::Delete) emit deleteLoadRequested(*sourceLine);
    } else if (const auto sourceLine = transmissionLineAt(event->pos())) {
        selectTransmissionLine(*sourceLine);
        emit transmissionLineSelected(*sourceLine);
        const auto action = showAttachmentContextMenu(
            this, event->globalPos(), tr("Delete Transmission Line"));
        if (action == GeometryContextAction::EditInSetup) {
            emit editTransmissionLineRequested(*sourceLine);
        } else if (action == GeometryContextAction::Delete) {
            emit deleteTransmissionLineRequested(*sourceLine);
        }
    } else if (const auto sourceLine = excitationAt(event->pos())) {
        selectExcitation(*sourceLine);
        emit excitationSelected(*sourceLine);
        const auto action = showExcitationContextMenu(this, event->globalPos());
        if (action == GeometryContextAction::Properties) {
            emit editExcitationRequested(*sourceLine);
        } else if (action == GeometryContextAction::EditInSetup) {
            emit openExcitationSetupRequested(*sourceLine);
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

}
