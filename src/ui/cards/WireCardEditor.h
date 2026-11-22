#pragma once

#include "model/AntennaModel.h"
#include "model/LengthUnit.h"
#include "model/SegmentSharing.h"
#include "nec/NecCardFieldEditor.h"
#include "nec/NecSymbolResolver.h"

#include <QWidget>

#include <map>

#include <vector>

class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace necwb::ui {

class ElidedLabel;
class SymbolCells;

class WireCardEditor final : public QWidget {
    Q_OBJECT

public:
    explicit WireCardEditor(QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model);
    void selectWire(int tag);
    void setLengthUnit(model::LengthUnit unit);
    void setDeckScale(double scaleToMeters, QString unitLabel);
    // The fields written as expressions, by deck line and field, with their text.
    void setSymbolicGeometryFields(nec::NecParameterFieldMap sourceFields);
    // The card that made each generated wire (GX, GM, GR), by its deck line, so a read-only
    // row says where its values come from.
    void setGeneratingCards(std::map<std::size_t, QString> cards);
    // The symbols a cell offers as you type.
    void setSymbols(const std::vector<nec::SymbolDefinition>& symbols);
    // The model's shared segments, which the line under the table explains for a selected wire.
    void setSharedSegments(std::vector<model::SharedSegment> shared);
    void focusParameterizableField();
    // Symbols, Make Variable… and expressions typed into a cell.
    [[nodiscard]] auto symbolCells() const -> SymbolCells* { return symbolCells_; }

signals:
    void wireSelected(int tag);
    void wireEdited(model::Wire original, model::Wire updated);
    void addWireRequested();
    void duplicateWireRequested(int tag);
    void deleteWireRequested(int tag);

private:
    void updateActionStates();
    void updateUnitLabels();
    void describeSelectedWire();
    void validateAndCommitRow(int row, int changedColumn);
    [[nodiscard]] auto selectedTag() const -> int;
    void setCellError(QTableWidgetItem* item, const QString& message);
    void applyGauge(int tag, int gauge);
    [[nodiscard]] auto readOnlyReason(const model::Wire& wire) const -> QString;

    QTableWidget* table_{};
    QPushButton* duplicateButton_{};
    QPushButton* deleteButton_{};
    ElidedLabel* note_{};
    std::vector<model::SharedSegment> shared_;
    model::AntennaModel model_;
    nec::NecParameterFieldMap symbolicGeometryFields_;
    std::map<std::size_t, QString> generatingCards_;
    SymbolCells* symbolCells_{};
    double scaleToMeters_{1.0};
    QString unitLabel_{QStringLiteral("m")};
    bool updating_{};
};

}
