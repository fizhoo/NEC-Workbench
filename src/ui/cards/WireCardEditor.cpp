#include "ui/cards/WireCardEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/ElidedLabel.h"
#include "ui/cards/CompactNumberDelegate.h"
#include "ui/cards/RowHeightCap.h"
#include "ui/cards/SymbolCells.h"
#include "ui/cards/VariableMenu.h"

#include "model/WireGauge.h"
#include "model/WireGeometry.h"

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
// The AWG sizes the gauge column offers, 0000 (-3) to 40.
constexpr auto ThickestGauge = -3;
constexpr auto ThinnestGauge = 40;

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
    auto* delegate = new CompactNumberDelegate(table_);
    table_->setItemDelegate(delegate);
    symbolCells_ = new SymbolCells(table_, [this](const QTableWidgetItem& item) -> std::optional<CellField> {
        const auto* tag = table_->item(item.row(), Tag);
        const auto* wire = tag == nullptr ? nullptr : model_.wireByTag(tag->data(WireTagRole).toInt());
        if (wire == nullptr || item.column() < Segments || item.column() > Radius) return std::nullopt;
        const auto field = static_cast<std::size_t>(item.column());
        return CellField{wire->sourceLine, field, wireFieldName(field)};
    });
    delegate->setSymbolCells(symbolCells_);
    table_->setColumnCount(ColumnCount);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);

    // What the selected wire is: its diameter, length and shared segments.
    note_ = new ElidedLabel(this);
    note_->setObjectName(QStringLiteral("cardRowNote"));
    note_->setMinimumHeight(note_->fontMetrics().lineSpacing());
    note_->setContentsMargins(4, 0, 4, 0);

    layout->addLayout(actions);
    layout->addWidget(table_, 1);
    layout->addWidget(note_);
    layout->addStretch();
    new RowHeightCap(table_, table_);
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
        describeSelectedWire();
        if (!updating_) {
            emit wireSelected(selectedTag());
        }
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!updating_) {
            validateAndCommitRow(item->row(), item->column());
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
                item->setToolTip(readOnlyReason(wire));
            }
            // An expression shows its value and opens for editing as written.
            const auto symbolicLine = symbolicGeometryFields_.find(wire.sourceLine);
            const auto symbolic = symbolicLine == symbolicGeometryFields_.end() ? nullptr
                : [&symbolicLine, column] {
                      const auto found = symbolicLine->second.find(static_cast<std::size_t>(column));
                      return found == symbolicLine->second.end() ? nullptr : &found->second;
                  }();
            if (symbolic != nullptr) {
                SymbolCells::showExpression(item, table_, QString::fromStdString(*symbolic), values[column]);
                if (!wire.editable) item->setToolTip(readOnlyReason(wire));
            }
            table_->setItem(row, column, item);
        }
        auto* gaugeControl = new QComboBox(table_);
        gaugeControl->setObjectName(QStringLiteral("wireGaugeEditor"));
        gaugeControl->addItem(tr("Custom radius"));
        for (auto gauge = ThickestGauge; gauge <= ThinnestGauge; ++gauge)
            gaugeControl->addItem(QString::fromStdString(model::awgLabel(gauge)), gauge);
        if (const auto gauge = model::matchingAwg(wire.radius);
            gauge && *gauge >= ThickestGauge && *gauge <= ThinnestGauge)
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
            if (!(item->flags() & Qt::ItemIsEditable) && !SymbolCells::usesExpression(*item)) continue;
            table_->setCurrentCell(row, column);
            table_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            table_->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
    table_->setFocus(Qt::OtherFocusReason);
}

void WireCardEditor::setSharedSegments(std::vector<model::SharedSegment> shared)
{
    shared_ = std::move(shared);
    describeSelectedWire();
}

void WireCardEditor::describeSelectedWire()
{
    const auto* wire = model_.wireByTag(selectedTag());
    QStringList parts;
    if (wire != nullptr) {
        const auto gauge = model::matchingAwg(wire->radius);
        const auto diameter = QString::number(2.0 * wire->radius / scaleToMeters_, 'g', 4);
        parts << (gauge ? tr("Ø %1 %2 (%3)").arg(diameter, unitLabel_,
                              QString::fromStdString(model::awgLabel(*gauge)))
                        : tr("Ø %1 %2").arg(diameter, unitLabel_))
              << tr("%1 %2 long").arg(formatDecimal(model::wireLength(*wire) / scaleToMeters_), unitLabel_);
        if (!wire->editable)
            parts << tr("set by line %1: edit it in Other or the NEC Deck").arg(wire->sourceLine);
        for (const auto& segment : shared_)
            if (segment.wireTag == wire->tag) parts << QString::fromStdString(segment.description);
    }
    note_->setText(parts.join(QStringLiteral(" · ")));
    note_->setToolTip(parts.join(QLatin1Char('\n')));
}

void WireCardEditor::setSymbolicGeometryFields(nec::NecParameterFieldMap sourceFields)
{
    symbolicGeometryFields_ = std::move(sourceFields);
}

void WireCardEditor::setGeneratingCards(std::map<std::size_t, QString> cards)
{
    generatingCards_ = std::move(cards);
}

// A read-only row is a wire another card made, or a GW whose GC card tapers it.
auto WireCardEditor::readOnlyReason(const model::Wire& wire) const -> QString
{
    if (const auto card = generatingCards_.find(wire.sourceLine); card != generatingCards_.end())
        return tr("Made by the %1 card on line %2 from another wire. Edit that wire, or the %1 "
                  "card: Other → Other NEC-2 Geometry Cards, or Raw Source.")
            .arg(card->second).arg(wire.sourceLine);
    return tr("This GW uses a following GC taper card. Edit the GC card in the Other card table "
              "or in Raw Source.");
}

void WireCardEditor::setSymbols(const std::vector<nec::SymbolDefinition>& symbols)
{
    symbolCells_->setSymbols(symbols);
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
        describeSelectedWire();
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
    describeSelectedWire();
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
    if (symbolCells_->takeExpressionEdit(changedItem, [this] { setModel(model_); })) return;

    // Only the edited field changes; the others keep their exact deck values.
    auto updated = *original;
    const auto text = changedItem->text().trimmed();
    bool ok = false;
    const auto setLength = [&](double& length) {
        const auto value = text.toDouble(&ok) * scaleToMeters_;
        ok = ok && std::isfinite(value);
        if (ok) length = value;
    };
    switch (changedColumn) {
    case Tag: updated.tag = text.toInt(&ok); ok = ok && updated.tag > 0; break;
    case Segments: updated.segments = text.toInt(&ok); ok = ok && updated.segments > 0; break;
    case X1: setLength(updated.start.x); break;
    case Y1: setLength(updated.start.y); break;
    case Z1: setLength(updated.start.z); break;
    case X2: setLength(updated.end.x); break;
    case Y2: setLength(updated.end.y); break;
    case Z2: setLength(updated.end.z); break;
    case Radius: setLength(updated.radius); ok = ok && updated.radius > 0.0; break;
    default: return;
    }
    const auto error = !ok ? (changedColumn == Tag ? tr("Wire tag must be a positive integer.")
            : changedColumn == Segments ? tr("Segments must be a positive integer.")
            : changedColumn == Radius ? tr("Radius must be a finite number greater than zero.")
                                      : tr("Coordinates must be finite numbers."))
        : updated.tag != originalTag && model_.wireByTag(updated.tag) != nullptr ? tr("Wire tags must be unique.")
        : updated.start == updated.end ? tr("Wire endpoints must be different.")
                                       : QString{};
    if (!error.isEmpty()) {
        setCellError(changedItem, error);
        return;
    }
    changedItem->setBackground({});
    changedItem->setToolTip({});
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
