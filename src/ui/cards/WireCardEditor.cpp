#include "ui/cards/WireCardEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/ParameterFieldStyle.h"
#include "ui/cards/CompactNumberDelegate.h"
#include "ui/cards/VariableMenu.h"

#include "model/WireGauge.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {

enum Column {
    Tag,
    Segments,
    X1,
    Y1,
    Z1,
    X2,
    Y2,
    Z2,
    Radius,
    Gauge,
    ColumnCount
};

constexpr auto WireTagRole = Qt::UserRole;

}

WireCardEditor::WireCardEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(4, 0, 4, 0);
    auto* addButton = new QPushButton(tr("Add Wire"), this);
    duplicateButton_ = new QPushButton(tr("Duplicate"), this);
    deleteButton_ = new QPushButton(tr("Delete"), this);
    actions->addWidget(addButton);
    actions->addWidget(duplicateButton_);
    actions->addWidget(deleteButton_);
    actions->addStretch();

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("wireCardTable"));
    table_->setItemDelegate(new CompactNumberDelegate(table_));
    table_->setColumnCount(ColumnCount);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);

    layout->addLayout(actions);
    layout->addWidget(table_, 1);

    connect(table_, &QTableWidget::itemActivated, this, [this] {
        if (selectedTag() >= 0) emit wireActivated(selectedTag());
    });
    connect(addButton, &QPushButton::clicked, this, [this] { emit addWireRequested(); });
    connect(duplicateButton_, &QPushButton::clicked, this, [this] {
        if (const auto tag = selectedTag(); tag >= 0) {
            emit duplicateWireRequested(tag);
        }
    });
    connect(deleteButton_, &QPushButton::clicked, this, [this] {
        if (const auto tag = selectedTag(); tag >= 0) {
            emit deleteWireRequested(tag);
        }
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        updateActionStates();
        if (!updating_) {
            emit wireSelected(selectedTag());
        }
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!updating_) {
            validateAndCommitRow(item->row(), item->column());
        }
    });
    connect(table_, &QTableWidget::customContextMenuRequested, this,
        [this](const QPoint& position) {
            auto* item = table_->itemAt(position);
            if (item == nullptr || item->column() < Segments || item->column() > Radius) return;
            const auto* wire = model_.wireByTag(
                table_->item(item->row(), Tag)->data(WireTagRole).toInt());
            if (wire == nullptr) return;
            const auto parameterLine = parameterControlledFields_.find(wire->sourceLine);
            const auto parameterControlled = parameterLine != parameterControlledFields_.end()
                && parameterLine->second.contains(static_cast<std::size_t>(item->column()));
            if (!parameterControlled && !(item->flags() & Qt::ItemIsEditable)) return;
            const auto field = static_cast<std::size_t>(item->column());
            const auto label = wireFieldName(field);
            switch (execVariableMenu(this, table_->viewport()->mapToGlobal(position), parameterControlled)) {
            case VariableChoice::Make:
                emit fieldParameterizationRequested(wire->sourceLine, field, label);
                break;
            case VariableChoice::Detach:
                emit fieldDetachmentRequested(wire->sourceLine, field, label);
                break;
            case VariableChoice::None: break;
            }
        });
    updateUnitLabels();
    updateActionStates();
}

void WireCardEditor::setModel(const model::AntennaModel& model)
{
    const auto previousTag = selectedTag();
    updating_ = true;
    const QSignalBlocker blocker(table_);
    model_ = model;
    const auto straightWireCount = std::ranges::count_if(model_.wires(), [](const auto& wire) {
        return wire.geometryKind == model::WireGeometryKind::Straight;
    });
    table_->setRowCount(static_cast<int>(straightWireCount));
    auto row = 0;
    for (const auto& wire : model_.wires()) {
        if (wire.geometryKind != model::WireGeometryKind::Straight) continue;
        const QStringList values{QString::number(wire.tag), QString::number(wire.segments),
            formatEditableDecimal(wire.start.x / scaleToMeters_),
            formatEditableDecimal(wire.start.y / scaleToMeters_),
            formatEditableDecimal(wire.start.z / scaleToMeters_),
            formatEditableDecimal(wire.end.x / scaleToMeters_),
            formatEditableDecimal(wire.end.y / scaleToMeters_),
            formatEditableDecimal(wire.end.z / scaleToMeters_),
            formatEditableDecimal(wire.radius / scaleToMeters_)};
        for (auto column = 0; column <= Radius; ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setData(WireTagRole, wire.tag);
            if (!wire.editable) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setToolTip(tr(
                    "This GW uses a following GC taper card. Edit the GC card in the Other card table or in Raw Source."));
            }
            // An expression shows its value and opens for editing as written.
            const auto symbolicLine = symbolicGeometryFields_.find(wire.sourceLine);
            const auto symbolic = symbolicLine == symbolicGeometryFields_.end() ? nullptr
                : [&symbolicLine, column] {
                      const auto found = symbolicLine->second.find(static_cast<std::size_t>(column));
                      return found == symbolicLine->second.end() ? nullptr : &found->second;
                  }();
            if (symbolic != nullptr) {
                const auto expression = QString::fromStdString(*symbolic);
                item->setData(CellExpressionRole, expression);
                const auto parameterLine = parameterControlledFields_.find(wire.sourceLine);
                if (parameterLine != parameterControlledFields_.end()
                    && parameterLine->second.contains(static_cast<std::size_t>(column)))
                    styleParameterControlledField(item, table_, expression);
                if (wire.editable)
                    item->setToolTip(tr("ƒx %1 = %2\nType over it to change it; change a variable "
                        "under Variables.").arg(expression, values[column]));
            }
            table_->setItem(row, column, item);
        }
        auto* gaugeControl = new QComboBox(table_);
        gaugeControl->setObjectName(QStringLiteral("wireGaugeEditor"));
        gaugeControl->addItem(tr("Custom radius"));
        for (auto gauge = 10; gauge <= 30; ++gauge)
            gaugeControl->addItem(QString::fromStdString(model::awgLabel(gauge)), gauge);
        if (const auto gauge = model::matchingAwg(wire.radius); gauge && *gauge >= 10 && *gauge <= 30)
            gaugeControl->setCurrentIndex(gaugeControl->findData(*gauge));
        if (!wire.editable) gaugeControl->setEnabled(false);
        const auto symbolicLine = symbolicGeometryFields_.find(wire.sourceLine);
        if (symbolicLine != symbolicGeometryFields_.end()
            && symbolicLine->second.contains(static_cast<std::size_t>(Radius))) {
            gaugeControl->setEnabled(false);
            gaugeControl->setToolTip(tr(
                "This radius is controlled by an SY expression. Edit the parameter or raw source."));
        } else {
            gaugeControl->setToolTip(tr(
                "Selecting AWG updates the NEC wire radius; manual radius values remain Custom."));
        }
        connect(gaugeControl, &QComboBox::currentIndexChanged, this,
            [this, gaugeControl, row, tag = wire.tag](int) {
                if (updating_ || !gaugeControl->currentData().isValid()) return;
                table_->selectRow(row);
                applyGauge(tag, gaugeControl->currentData().toInt());
            });
        table_->setCellWidget(row, Gauge, gaugeControl);
        ++row;
    }
    updating_ = false;
    selectWire(previousTag);
    updateActionStates();
}

void WireCardEditor::focusParameterizableField()
{
    for (auto row = 0; row < table_->rowCount(); ++row) {
        for (auto column = static_cast<int>(X1);
             column <= static_cast<int>(Radius); ++column) {
            auto* item = table_->item(row, column);
            if (item == nullptr) continue;
            const auto* wire = model_.wireByTag(
                table_->item(row, Tag)->data(WireTagRole).toInt());
            if (wire == nullptr) continue;
            const auto parameterLine = parameterControlledFields_.find(wire->sourceLine);
            const auto parameterControlled = parameterLine != parameterControlledFields_.end()
                && parameterLine->second.contains(static_cast<std::size_t>(column));
            if (!(item->flags() & Qt::ItemIsEditable) && !parameterControlled) continue;
            table_->setCurrentCell(row, column);
            table_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            table_->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
    table_->setFocus(Qt::OtherFocusReason);
}

void WireCardEditor::setSymbolicGeometryFields(nec::NecParameterFieldMap sourceFields)
{
    symbolicGeometryFields_ = std::move(sourceFields);
}

void WireCardEditor::setSymbols(const std::vector<nec::SymbolDefinition>& symbols)
{
    QList<std::pair<QString, double>> offered;
    for (const auto& symbol : symbols) offered.append({QString::fromStdString(symbol.name), symbol.value});
    static_cast<CompactNumberDelegate*>(table_->itemDelegate())->setSymbols(std::move(offered));
}

void WireCardEditor::setParameterControlledFields(nec::NecParameterFieldMap sourceFields)
{
    parameterControlledFields_ = std::move(sourceFields);
}

void WireCardEditor::setLengthUnit(model::LengthUnit unit)
{
    const auto symbol = model::lengthUnitSymbol(unit);
    setDeckScale(model::metersPerUnit(unit),
        QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

void WireCardEditor::setDeckScale(double scaleToMeters, QString unitLabel)
{
    if (!std::isfinite(scaleToMeters) || scaleToMeters <= 0.0) return;
    if (scaleToMeters_ == scaleToMeters && unitLabel_ == unitLabel) return;
    scaleToMeters_ = scaleToMeters;
    unitLabel_ = std::move(unitLabel);
    updateUnitLabels();
    setModel(model_);
}

void WireCardEditor::selectWire(int tag)
{
    const QSignalBlocker blocker(table_);
    table_->clearSelection();
    if (tag < 0) {
        updateActionStates();
        return;
    }
    for (auto row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, Tag)->data(WireTagRole).toInt() == tag) {
            table_->selectRow(row);
            table_->scrollToItem(table_->item(row, Tag));
            break;
        }
    }
    updateActionStates();
}

void WireCardEditor::updateActionStates()
{
    const bool selected = selectedTag() >= 0;
    duplicateButton_->setEnabled(selected);
    deleteButton_->setEnabled(selected);
}

void WireCardEditor::updateUnitLabels()
{
    table_->setHorizontalHeaderLabels({tr("Tag"), tr("Segments"), tr("X1 %1").arg(unitLabel_),
        tr("Y1 %1").arg(unitLabel_), tr("Z1 %1").arg(unitLabel_),
        tr("X2 %1").arg(unitLabel_), tr("Y2 %1").arg(unitLabel_),
        tr("Z2 %1").arg(unitLabel_), tr("Radius %1").arg(unitLabel_), tr("Wire Gauge")});
    table_->setToolTip(tr("Right-click a cell → Make Variable…, or type a name in the cell.\n"
        "Values use the NEC deck geometry unit (%1); GS converts them to meters for the solver.")
            .arg(unitLabel_));
}

void WireCardEditor::applyGauge(int tag, int gauge)
{
    const auto* original = model_.wireByTag(tag);
    if (original == nullptr || gauge < 10 || gauge > 30) return;
    auto updated = *original;
    updated.radius = model::awgRadiusMeters(gauge);
    if (updated.radius == original->radius) return;
    const auto originalWire = *original;
    QTimer::singleShot(0, this, [this, originalWire, updated] {
        emit wireEdited(originalWire, updated);
    });
}

void WireCardEditor::validateAndCommitRow(int row, int changedColumn)
{
    auto* changedItem = table_->item(row, changedColumn);
    const auto originalTag = table_->item(row, Tag)->data(WireTagRole).toInt();
    const auto* original = model_.wireByTag(originalTag);
    if (original == nullptr) {
        setCellError(changedItem, tr("This wire no longer exists."));
        return;
    }
    // A name or expression, or anything typed over one, goes to the deck as written; the
    // cell shows the value again until the deck changes.
    const auto typed = changedItem->text().trimmed();
    const auto written = changedItem->data(CellExpressionRole).toString();
    auto plainNumber = false;
    static_cast<void>(typed.toDouble(&plainNumber));
    if (changedColumn >= Segments && changedColumn <= Radius && (!written.isEmpty() || !plainNumber)) {
        const auto below = table_->viewport()->mapToGlobal(
            table_->visualItemRect(changedItem).bottomLeft());
        const auto label = table_->horizontalHeaderItem(changedColumn)->text();
        QTimer::singleShot(0, this, [this, sourceLine = original->sourceLine, changedColumn, label,
                                        typed, written, below] {
            if (typed != written) {
                emit fieldExpressionEntered(sourceLine, static_cast<std::size_t>(changedColumn),
                    label, typed, below);
            }
            setModel(model_);
        });
        return;
    }

    bool tagOk = false;
    bool segmentsOk = false;
    const auto tag = table_->item(row, Tag)->text().toInt(&tagOk);
    const auto segments = table_->item(row, Segments)->text().toInt(&segmentsOk);
    bool valuesOk = true;
    const auto readDouble = [&](Column column) {
        bool ok = false;
        const auto displayedValue = table_->item(row, column)->text().toDouble(&ok);
        const auto value = displayedValue * scaleToMeters_;
        valuesOk = valuesOk && ok && std::isfinite(value);
        return value;
    };
    const model::Point3D start{readDouble(X1), readDouble(Y1), readDouble(Z1)};
    const model::Point3D end{readDouble(X2), readDouble(Y2), readDouble(Z2)};
    const auto radius = readDouble(Radius);

    if (!tagOk || tag <= 0) {
        setCellError(changedItem, tr("Wire tag must be a positive integer."));
        return;
    }
    if (tag != originalTag && model_.wireByTag(tag) != nullptr) {
        setCellError(changedItem, tr("Wire tags must be unique."));
        return;
    }
    if (!segmentsOk || segments <= 0) {
        setCellError(changedItem, tr("Segments must be a positive integer."));
        return;
    }
    if (!valuesOk) {
        setCellError(changedItem, tr("Coordinates and radius must be finite numbers."));
        return;
    }
    if (start == end) {
        setCellError(changedItem, tr("Wire endpoints must be different."));
        return;
    }
    if (radius <= 0.0) {
        setCellError(changedItem, tr("Radius must be greater than zero."));
        return;
    }

    changedItem->setBackground({});
    changedItem->setToolTip({});
    const model::Wire updated{tag, start, end, segments, radius, original->sourceLine};
    if (updated != *original) {
        const auto originalWire = *original;
        QTimer::singleShot(0, this, [this, originalWire, updated] {
            emit wireEdited(originalWire, updated);
        });
    }
}

auto WireCardEditor::selectedTag() const -> int
{
    const auto row = table_->currentRow();
    return table_->selectedItems().empty() || row < 0 || table_->item(row, Tag) == nullptr
        ? -1
        : table_->item(row, Tag)->data(WireTagRole).toInt();
}

void WireCardEditor::setCellError(QTableWidgetItem* item, const QString& message)
{
    item->setBackground(QColor(255, 205, 205));
    item->setToolTip(message);
}

}
