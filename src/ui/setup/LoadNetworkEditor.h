#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/ModelSetup.h"

#include <QWidget>

class QLabel;
class QComboBox;
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
    [[nodiscard]] auto hasPendingEdits() const noexcept -> bool;
    void discardPendingEdits();
signals:
    void loadChanged(model::LoadDefinition load);
    void loadDeleteRequested(std::size_t sourceLine);
    void transmissionLineChanged(model::TransmissionLineDefinition line);
    void transmissionLineDeleteRequested(std::size_t sourceLine);
    void loadSelected(std::size_t sourceLine);
    void transmissionLineSelected(std::size_t sourceLine);
private:
    void applySelectedLoad();
    void applySelectedLine();
    void setLoadRow(int row, const model::LoadDefinition& load);
    void setLineRow(int row, const model::TransmissionLineDefinition& line);
    void populateSegmentChoices(QComboBox* control, int wireTag, int selectedSegment);
    void updateLineApplyState();
    void updateLoadColumns(int row);
    void updateLoadSegmentFields(int row, bool entireWire);
    void updateLoadValueFields(int row, int type);
    void setLoadPending(bool pending);
    void setLinePending(bool pending);
    model::AntennaModel model_;
    model::ModelSetup setup_;
    QTableWidget* loads_{};
    QTableWidget* lines_{};
    QPushButton* applyLoadButton_{};
    QPushButton* applyLineButton_{};
    QLabel* validation_{};
    model::LengthUnit lengthUnit_{model::LengthUnit::Meter};
    bool loadPending_{};
    bool linePending_{};
    bool updating_{};
};

}
