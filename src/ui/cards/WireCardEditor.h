#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"

#include <QWidget>

class QPushButton;
class QLabel;
class QTableWidget;
class QTableWidgetItem;

namespace necwb::ui {

class WireCardEditor final : public QWidget {
    Q_OBJECT

public:
    explicit WireCardEditor(QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model);
    void selectWire(int tag);
    void setLengthUnit(model::LengthUnit unit);

signals:
    void wireSelected(int tag);
    void wireEdited(model::Wire original, model::Wire updated);
    void addWireRequested();
    void duplicateWireRequested(int tag);
    void deleteWireRequested(int tag);

private:
    void updateActionStates();
    void updateUnitLabels();
    void validateAndCommitRow(int row, int changedColumn);
    [[nodiscard]] auto selectedTag() const -> int;
    void setCellError(QTableWidgetItem* item, const QString& message);

    QTableWidget* table_{};
    QLabel* instructions_{};
    QPushButton* duplicateButton_{};
    QPushButton* deleteButton_{};
    model::AntennaModel model_;
    model::LengthUnit lengthUnit_{model::LengthUnit::Meter};
    bool updating_{};
};

}
