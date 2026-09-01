#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"

#include <QWidget>

#include <unordered_set>

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
    void setDeckScale(double scaleToMeters, QString unitLabel);
    void setSymbolicGeometryLines(std::unordered_set<std::size_t> sourceLines);

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
    void applyGauge(int tag, int gauge);

    QTableWidget* table_{};
    QLabel* instructions_{};
    QPushButton* duplicateButton_{};
    QPushButton* deleteButton_{};
    model::AntennaModel model_;
    std::unordered_set<std::size_t> symbolicGeometryLines_;
    double scaleToMeters_{1.0};
    QString unitLabel_{QStringLiteral("m")};
    bool updating_{};
};

}
