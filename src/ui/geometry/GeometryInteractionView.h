#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QWidget>

#include <optional>
#include <utility>

class QContextMenuEvent;
class QMouseEvent;

namespace necwb::ui {

enum class GeometryContextAction {
    None,
    Properties,
    EditInSetup,
    Delete,
    AddExcitation,
    AddLoad,
    TransmissionLineEndpoint,
    CancelTransmissionLine,
    SplitWire,
    FitView,
};

struct GeometryWireMenuOptions {
    bool editable{};
    bool transmissionLinePending{};
    bool splitAvailable{};
    bool allowSplit{};
    bool allowDelete{};
    bool allowFit{};
};

class GeometryInteractionView : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;

    virtual void fitToView() = 0;
    void selectWire(int tag);
    void setExcitations(const std::vector<model::Excitation>& excitations);
    void setAttachments(const std::vector<model::LoadDefinition>& loads,
        const std::vector<model::TransmissionLineDefinition>& transmissionLines);
    void setPendingTransmissionLineEndpoint(std::optional<std::pair<int, int>> endpoint);
    void selectExcitation(std::size_t sourceLine);
    void selectLoad(std::size_t sourceLine);
    void selectTransmissionLine(std::size_t sourceLine);

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
    void contextMenuEvent(QContextMenuEvent* event) final;
    void mouseDoubleClickEvent(QMouseEvent* event) final;
    virtual auto wireAt(const QPointF& position) const -> std::optional<int> = 0;
    virtual auto excitationAt(const QPointF& position) const -> std::optional<std::size_t> = 0;
    virtual auto loadAt(const QPointF& position) const -> std::optional<std::size_t> = 0;
    virtual auto transmissionLineAt(const QPointF& position) const
        -> std::optional<std::size_t> = 0;
    virtual auto segmentAt(int wireTag, const QPointF& position) const -> int = 0;
    virtual auto wireMenuOptions(int tag, const QPointF& position) const
        -> GeometryWireMenuOptions = 0;
    virtual void handleWireContextAction(
        GeometryContextAction action, int tag, const QPointF& position) = 0;
    virtual void showEmptyContextMenu(QContextMenuEvent* event) = 0;

    void clearSelection();
    void setInteractionModel(const model::AntennaModel& model, bool resetWireSelection);

    model::AntennaModel model_;
    std::vector<model::Excitation> excitations_;
    std::vector<model::LoadDefinition> loads_;
    std::vector<model::TransmissionLineDefinition> transmissionLines_;
    std::optional<int> selectedWireTag_;
    std::optional<std::size_t> selectedExcitationLine_;
    std::optional<std::size_t> selectedLoadLine_;
    std::optional<std::size_t> selectedTransmissionLine_;
    std::optional<std::pair<int, int>> pendingTransmissionLineEndpoint_;
};

}
