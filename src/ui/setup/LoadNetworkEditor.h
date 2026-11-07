#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/ModelSetup.h"
#include "model/SegmentSharing.h"

#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace necwb::ui {

class LoadNetworkEditor final : public QWidget {
    Q_OBJECT
public:
    explicit LoadNetworkEditor(QWidget* parent = nullptr);
    void setData(const model::AntennaModel& model, const model::ModelSetup& setup);
    void setLengthUnit(model::LengthUnit unit);
    void selectLoad(std::size_t sourceLine);
    void selectTransmissionLine(std::size_t sourceLine);
    void selectNetwork(std::size_t sourceLine);
    // Brings the NT table into view, as the dashboard's Networks row does.
    void focusNetworks();
    // Selects the unapplied NT row, adding one with ports filled in and blank admittances.
    void addNetworkDraft();
    [[nodiscard]] auto hasPendingEdits() const noexcept -> bool;
    void discardPendingEdits();
signals:
    void loadChanged(model::LoadDefinition load);
    void loadDeleteRequested(std::size_t sourceLine);
    void transmissionLineChanged(model::TransmissionLineDefinition line);
    void transmissionLineDeleteRequested(std::size_t sourceLine);
    void loadSelected(std::size_t sourceLine);
    void transmissionLineSelected(std::size_t sourceLine);
    void networkChanged(model::NetworkDefinition network);
    void networkDeleteRequested(std::size_t sourceLine);
    void networkSelected(std::size_t sourceLine);
private:
    void applySelectedLoad();
    void applySelectedLine();
    void applySelectedNetwork();
    void setLoadRow(int row, const model::LoadDefinition& load);
    void setLineRow(int row, const model::TransmissionLineDefinition& line);
    void setNetworkRow(int row, const model::NetworkDefinition& network);
    void updateLineApplyState();
    void updateNetworkApplyState();
    void updateLoadColumns(int row);
    void updateLoadSegmentFields(int row, bool entireWire);
    void updateLoadValueFields(int row, int type);
    void setLoadPending(bool pending);
    void setLinePending(bool pending);
    void setNetworkPending(bool pending);
    [[nodiscard]] auto sharingNotes(model::InsertionKind kind, int number) const -> QStringList;
    void markSharedRows();
    void showSharingNotes(model::InsertionKind kind, int row);
    model::AntennaModel model_;
    model::ModelSetup setup_;
    QTableWidget* loads_{};
    QTableWidget* lines_{};
    QTableWidget* networks_{};
    QPushButton* applyLoadButton_{};
    QPushButton* applyLineButton_{};
    QPushButton* applyNetworkButton_{};
    QLabel* validation_{};
    QLabel* sharingNotes_{};
    std::vector<model::SharedSegment> sharedSegments_;
    model::LengthUnit lengthUnit_{model::LengthUnit::Meter};
    bool loadPending_{};
    bool linePending_{};
    bool networkPending_{};
    bool updating_{};
};

}
