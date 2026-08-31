#include "ui/cards/WireCardEditor.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

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
    ColumnCount
};

constexpr auto WireTagRole = Qt::UserRole;

auto number(double value) -> QString
{
    return QString::number(value, 'g', 15);
}

}

WireCardEditor::WireCardEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* heading = new QLabel(tr("Wires (GW)"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    heading->setFont(headingFont);
    instructions_ = new QLabel(this);
    instructions_->setWordWrap(true);

    auto* actions = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Wire"), this);
    duplicateButton_ = new QPushButton(tr("Duplicate Wire"), this);
    deleteButton_ = new QPushButton(tr("Delete Wire"), this);
    actions->addWidget(addButton);
    actions->addWidget(duplicateButton_);
    actions->addWidget(deleteButton_);
    actions->addStretch();

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColumnCount);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);

    layout->addWidget(heading);
    layout->addWidget(instructions_);
    layout->addLayout(actions);
    layout->addWidget(table_, 1);

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
    updateUnitLabels();
    updateActionStates();
}

void WireCardEditor::setModel(const model::AntennaModel& model)
{
    const auto previousTag = selectedTag();
    updating_ = true;
    const QSignalBlocker blocker(table_);
    model_ = model;
    table_->setRowCount(static_cast<int>(model_.wireCount()));
    auto row = 0;
    for (const auto& wire : model_.wires()) {
        const QStringList values{QString::number(wire.tag), QString::number(wire.segments),
            number(wire.start.x / scaleToMeters_),
            number(wire.start.y / scaleToMeters_),
            number(wire.start.z / scaleToMeters_),
            number(wire.end.x / scaleToMeters_),
            number(wire.end.y / scaleToMeters_),
            number(wire.end.z / scaleToMeters_),
            number(wire.radius / scaleToMeters_)};
        for (auto column = 0; column < ColumnCount; ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setData(WireTagRole, wire.tag);
            table_->setItem(row, column, item);
        }
        ++row;
    }
    updating_ = false;
    selectWire(previousTag);
    updateActionStates();
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
    table_->setHorizontalHeaderLabels({tr("Tag"), tr("Segments"), tr("X1 (%1)").arg(unitLabel_),
        tr("Y1 (%1)").arg(unitLabel_), tr("Z1 (%1)").arg(unitLabel_),
        tr("X2 (%1)").arg(unitLabel_), tr("Y2 (%1)").arg(unitLabel_),
        tr("Z2 (%1)").arg(unitLabel_), tr("Radius (%1)").arg(unitLabel_)});
    instructions_->setText(tr("Double-click a cell to edit it. Values use the NEC deck geometry unit (%1); GS converts them to meters for the solver.")
            .arg(unitLabel_));
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
