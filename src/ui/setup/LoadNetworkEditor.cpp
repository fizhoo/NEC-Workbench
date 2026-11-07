#include "ui/setup/LoadNetworkEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/PendingEditIndicator.h"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QCoreApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {
constexpr auto SourceLineRole = Qt::UserRole;
// A cell chosen from a fixed list shows the choice's label and keeps its value here.
constexpr auto ChoiceValueRole = Qt::UserRole + 1;

// Cells show plain text and get an editor only while being edited: a dropdown of fixed
// choices or of the model's wire tags, or a spin box of segment numbers on the row's wire.
class CellEditorDelegate final : public QStyledItemDelegate {
public:
    enum class Kind { Choice, Wire, Segment };

    CellEditorDelegate(Kind kind, QObject* parent) : QStyledItemDelegate(parent), kind_(kind) {}

    // Choice cells: labels and the values stored in ChoiceValueRole.
    std::vector<std::pair<QString, QVariant>> choices;
    std::function<std::vector<int>()> wireTags;
    // Segments the tag reaches as NEC counts them; zero when no wire has the tag.
    std::function<int(int wireTag)> segmentCount;
    // Segment cells: the column holding the row's wire tag.
    int wireColumn{-1};
    // Wire cells: segment columns to keep within the newly chosen wire.
    std::vector<int> segmentColumns;

    auto createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const
        -> QWidget* override
    {
        if (kind_ == Kind::Segment) {
            auto* spin = new QSpinBox(parent);
            const auto wireTag = index.siblingAtColumn(wireColumn).data().toString().toInt();
            spin->setRange(1, std::max(1, segmentCount(wireTag)));
            return spin;
        }
        auto* combo = new QComboBox(parent);
        if (kind_ == Kind::Choice) {
            for (const auto& [label, value] : choices) combo->addItem(label, value);
        } else {
            auto tags = wireTags();
            // Tag 0 (absolute segment numbers) is offered only where a card already uses it.
            const auto current = index.data().toString().toInt();
            if (std::ranges::find(tags, current) == tags.end() && segmentCount(current) > 0)
                tags.insert(tags.begin(), current);
            for (const auto tag : tags) combo->addItem(QString::number(tag), tag);
        }
        // A choice takes effect as soon as it is picked, without leaving the cell.
        connect(combo, &QComboBox::activated, this, [self = const_cast<CellEditorDelegate*>(this),
                                                        combo] {
            emit self->commitData(combo);
            emit self->closeEditor(combo);
        });
        return combo;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
            spin->setValue(index.data().toString().toInt());
        } else if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            const auto value = kind_ == Kind::Choice ? index.data(ChoiceValueRole)
                                                     : QVariant(index.data().toString().toInt());
            combo->setCurrentIndex(std::max(0, combo->findData(value)));
            QMetaObject::invokeMethod(combo, &QComboBox::showPopup, Qt::QueuedConnection);
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
        const QModelIndex& index) const override
    {
        if (const auto* spin = qobject_cast<QSpinBox*>(editor)) {
            model->setData(index, QString::number(spin->value()));
            return;
        }
        const auto* combo = qobject_cast<QComboBox*>(editor);
        if (combo == nullptr || combo->currentIndex() < 0) return;
        if (kind_ == Kind::Choice) {
            model->setData(index, combo->currentData(), ChoiceValueRole);
            model->setData(index, combo->currentText());
            return;
        }
        const auto wireTag = combo->currentData().toInt();
        model->setData(index, QString::number(wireTag));
        const auto count = segmentCount(wireTag);
        for (const auto column : segmentColumns) {
            const auto segment = index.siblingAtColumn(column);
            if (segment.data().toString().toInt() > count)
                model->setData(segment, QString::number(count));
        }
    }

private:
    Kind kind_;
};

// Tables show between two and six rows, then scroll, so an empty table stays small and the
// spare height collects below the last section instead of inside every table.
constexpr auto kMinimumVisibleRows = 2;
constexpr auto kMaximumVisibleRows = 6;

void fitTableHeight(QTableWidget* table)
{
    const auto rows = std::clamp(table->rowCount(), kMinimumVisibleRows, kMaximumVisibleRows);
    table->setFixedHeight(2 * table->frameWidth()
        + table->horizontalHeader()->sizeHint().height()
        + rows * table->verticalHeader()->defaultSectionSize()
        + table->horizontalScrollBar()->sizeHint().height());
}

// Sizes the table to its rows and, while it has none, shows how to add the first one.
void showRowsOrEmptyMessage(QTableWidget* table, const QString& message)
{
    auto* label = new QLabel(message, table->viewport());
    label->setObjectName(table->objectName() + QStringLiteral("EmptyMessage"));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout = new QVBoxLayout(table->viewport());
    layout->addWidget(label);
    const auto refresh = [table, label] {
        label->setVisible(table->rowCount() == 0);
        fitTableHeight(table);
    };
    QObject::connect(table->model(), &QAbstractItemModel::rowsInserted, label, refresh);
    QObject::connect(table->model(), &QAbstractItemModel::rowsRemoved, label, refresh);
    QObject::connect(table->model(), &QAbstractItemModel::modelReset, label, refresh);
    refresh();
}
enum LoadColumn {
    TypeColumn,
    WireColumn,
    ScopeColumn,
    FirstSegmentColumn,
    LastSegmentColumn,
    Value1Column,
    Value2Column,
    Value3Column,
    LoadColumnCount
};
enum LineColumn {
    Wire1Column,
    Segment1Column,
    Wire2Column,
    Segment2Column,
    ImpedanceColumn,
    LengthColumn,
    ShuntReal1Column,
    ShuntImaginary1Column,
    ShuntReal2Column,
    ShuntImaginary2Column,
    LineColumnCount
};
// NT rows share the port columns (Wire1Column through Segment2Column) with TL rows.
enum NetworkColumn {
    Y11RealColumn = ImpedanceColumn,
    Y11ImaginaryColumn,
    Y12RealColumn,
    Y12ImaginaryColumn,
    Y22RealColumn,
    Y22ImaginaryColumn,
    NetworkColumnCount
};

auto sourceLineAt(QTableWidget* table, int row) -> std::size_t
{
    return row < 0 || table->item(row, 0) == nullptr
        ? 0 : table->item(row, 0)->data(SourceLineRole).toULongLong();
}

auto loadTypeAt(QTableWidget* table, int row) -> int
{
    if (row < 0 || table->item(row, TypeColumn) == nullptr) return -1;
    const auto value = table->item(row, TypeColumn)->data(ChoiceValueRole);
    return value.isValid() ? value.toInt() : -1;
}

auto entireWireAt(QTableWidget* table, int row) -> bool
{
    return row >= 0 && table->item(row, ScopeColumn) != nullptr
        && table->item(row, ScopeColumn)->data(ChoiceValueRole).toBool();
}

auto cellIntAt(QTableWidget* table, int row, int column, bool* valid = nullptr) -> int
{
    auto ok = false;
    const auto value = table->item(row, column) == nullptr
        ? 0 : table->item(row, column)->text().toInt(&ok);
    if (valid != nullptr) *valid = ok;
    return value;
}

// Port cells of a TL or NT row, for "transmission-line end" or "network port".
void setPortToolTips(QTableWidget* table, int row, const QString& subject)
{
    for (const auto column : {Wire1Column, Wire2Column}) {
        table->item(row, column)->setToolTip(
            QCoreApplication::translate("LoadNetworkEditor",
                "Double-click to choose the GW wire tag for this %1.").arg(subject));
    }
    for (const auto column : {Segment1Column, Segment2Column}) {
        table->item(row, column)->setToolTip(
            QCoreApplication::translate("LoadNetworkEditor",
                "Double-click to choose the segment this %1 connects across.").arg(subject));
    }
}

auto scopeLabel(bool entireWire) -> QString
{
    return entireWire ? QCoreApplication::translate("LoadNetworkEditor", "Entire wire")
                      : QCoreApplication::translate("LoadNetworkEditor", "Segment range");
}

auto displayScaleFor(int type, int column) -> double
{
    if (type >= 0 && type <= 3 && column == Value2Column) return 1.0e6;
    if (type >= 0 && type <= 3 && column == Value3Column) return 1.0e12;
    if (type == 5 && column == Value1Column) return 1.0e-6;
    return 1.0;
}

auto loadTypeChoices() -> const std::array<std::pair<int, const char*>, 6>&
{
    static const std::array choices{
        std::pair{0, QT_TRANSLATE_NOOP("LoadNetworkEditor", "0 — Series RLC")},
        std::pair{1, QT_TRANSLATE_NOOP("LoadNetworkEditor", "1 — Parallel RLC")},
        std::pair{2, QT_TRANSLATE_NOOP("LoadNetworkEditor", "2 — Distributed series RLC")},
        std::pair{3, QT_TRANSLATE_NOOP("LoadNetworkEditor", "3 — Distributed parallel RLC")},
        std::pair{4, QT_TRANSLATE_NOOP("LoadNetworkEditor", "4 — Fixed complex impedance")},
        std::pair{5, QT_TRANSLATE_NOOP("LoadNetworkEditor", "5 — Wire conductivity")},
    };
    return choices;
}
}

LoadNetworkEditor::LoadNetworkEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr("Edit lumped/distributed loads, transmission-line connections, and two-port networks. Double-click a cell, then apply the selected row."), this);
    description->setWordWrap(true);
    auto* loadTypes = new QLabel(tr("LD types: 0 series RLC, 1 parallel RLC, 2 distributed series RLC, 3 distributed parallel RLC, 4 fixed complex impedance, 5 wire conductivity."), this);
    loadTypes->setWordWrap(true);
    loads_ = new QTableWidget(0, LoadColumnCount, this);
    loads_->setObjectName(QStringLiteral("loadNetworkLoadsTable"));
    loads_->setHorizontalHeaderLabels({tr("Type"), tr("Wire"), tr("Scope"),
        tr("First seg"), tr("Last seg"), tr("Value 1"), tr("Value 2"), tr("Value 3")});
    loads_->horizontalHeader()->setStretchLastSection(true);
    auto* loadButtons = new QHBoxLayout;
    auto* addLoad = new QPushButton(tr("Add Load"), this);
    addLoad->setObjectName(QStringLiteral("addLoadButton"));
    applyLoadButton_ = new QPushButton(tr("Apply Selected Load"), this);
    applyLoadButton_->setObjectName(QStringLiteral("applySelectedLoadButton"));
    auto* deleteLoad = new QPushButton(tr("Delete Selected Load"), this);
    loadButtons->addWidget(addLoad); loadButtons->addWidget(applyLoadButton_); loadButtons->addWidget(deleteLoad); loadButtons->addStretch();

    lines_ = new QTableWidget(0, LineColumnCount, this);
    lines_->setObjectName(QStringLiteral("loadNetworkLinesTable"));
    lines_->setHorizontalHeaderLabels({tr("Wire 1"), tr("Seg 1"), tr("Wire 2"),
        tr("Seg 2"), tr("Z0 (Ω)"), tr("Length (m)"), tr("End 1 G (S)"),
        tr("End 1 B (S)"), tr("End 2 G (S)"), tr("End 2 B (S)")});
    lines_->horizontalHeader()->setStretchLastSection(true);
    auto* lineButtons = new QHBoxLayout;
    auto* addLine = new QPushButton(tr("Add Transmission Line"), this);
    addLine->setObjectName(QStringLiteral("addTransmissionLineButton"));
    applyLineButton_ = new QPushButton(tr("Apply Selected Line"), this);
    applyLineButton_->setObjectName(QStringLiteral("applySelectedLineButton"));
    applyLineButton_->setEnabled(false);
    auto* deleteLine = new QPushButton(tr("Delete Selected Line"), this);
    lineButtons->addWidget(addLine); lineButtons->addWidget(applyLineButton_); lineButtons->addWidget(deleteLine); lineButtons->addStretch();
    networks_ = new QTableWidget(0, NetworkColumnCount, this);
    networks_->setObjectName(QStringLiteral("loadNetworkNetworksTable"));
    networks_->setHorizontalHeaderLabels({tr("Wire 1"), tr("Seg 1"), tr("Wire 2"),
        tr("Seg 2"), tr("Y11 G (S)"), tr("Y11 B (S)"), tr("Y12 G (S)"), tr("Y12 B (S)"),
        tr("Y22 G (S)"), tr("Y22 B (S)")});
    networks_->horizontalHeader()->setStretchLastSection(true);
    networks_->horizontalHeaderItem(Wire1Column)->setToolTip(tr("GW tag at network port one."));
    networks_->horizontalHeaderItem(Segment1Column)->setToolTip(tr("Segment whose gap port one connects across."));
    networks_->horizontalHeaderItem(Wire2Column)->setToolTip(tr("GW tag at network port two."));
    networks_->horizontalHeaderItem(Segment2Column)->setToolTip(tr("Segment whose gap port two connects across."));
    for (auto column = static_cast<int>(Y11RealColumn); column <= Y22ImaginaryColumn; ++column) {
        networks_->horizontalHeaderItem(column)->setToolTip(tr(
            "Short-circuit admittance matrix element in siemens (G real, B imaginary). Y21 "
            "equals Y12. Fixed values: they do not change with frequency."));
    }
    auto* networkButtons = new QHBoxLayout;
    auto* addNetwork = new QPushButton(tr("Add Network"), this);
    addNetwork->setObjectName(QStringLiteral("addNetworkButton"));
    applyNetworkButton_ = new QPushButton(tr("Apply Selected Network"), this);
    applyNetworkButton_->setObjectName(QStringLiteral("applySelectedNetworkButton"));
    applyNetworkButton_->setEnabled(false);
    auto* deleteNetwork = new QPushButton(tr("Delete Selected Network"), this);
    deleteNetwork->setObjectName(QStringLiteral("deleteSelectedNetworkButton"));
    networkButtons->addWidget(addNetwork); networkButtons->addWidget(applyNetworkButton_); networkButtons->addWidget(deleteNetwork); networkButtons->addStretch();
    const auto wireTags = [this] {
        std::vector<int> tags;
        for (const auto& wire : model_.wires())
            if (std::ranges::find(tags, wire.tag) == tags.end()) tags.push_back(wire.tag);
        return tags;
    };
    const auto segmentCount = [this](int wireTag) {
        return model::referencedSegmentCount(model_, wireTag);
    };
    const auto wireEditor = [&](QTableWidget* table, int column, std::vector<int> segments) {
        auto* delegate = new CellEditorDelegate(CellEditorDelegate::Kind::Wire, table);
        delegate->wireTags = wireTags;
        delegate->segmentCount = segmentCount;
        delegate->segmentColumns = std::move(segments);
        table->setItemDelegateForColumn(column, delegate);
    };
    const auto segmentEditor = [&](QTableWidget* table, int column, int wireColumn) {
        auto* delegate = new CellEditorDelegate(CellEditorDelegate::Kind::Segment, table);
        delegate->segmentCount = segmentCount;
        delegate->wireColumn = wireColumn;
        table->setItemDelegateForColumn(column, delegate);
    };
    auto* typeEditor = new CellEditorDelegate(CellEditorDelegate::Kind::Choice, loads_);
    for (const auto& [number, label] : loadTypeChoices())
        typeEditor->choices.emplace_back(QCoreApplication::translate("LoadNetworkEditor", label), number);
    loads_->setItemDelegateForColumn(TypeColumn, typeEditor);
    auto* scopeEditor = new CellEditorDelegate(CellEditorDelegate::Kind::Choice, loads_);
    scopeEditor->choices = {{scopeLabel(true), true}, {scopeLabel(false), false}};
    loads_->setItemDelegateForColumn(ScopeColumn, scopeEditor);
    wireEditor(loads_, WireColumn, {FirstSegmentColumn, LastSegmentColumn});
    segmentEditor(loads_, FirstSegmentColumn, WireColumn);
    segmentEditor(loads_, LastSegmentColumn, WireColumn);
    for (auto* table : {loads_, lines_, networks_}) table->setWordWrap(false);
    for (auto* table : {lines_, networks_}) {
        wireEditor(table, Wire1Column, {Segment1Column});
        segmentEditor(table, Segment1Column, Wire1Column);
        wireEditor(table, Wire2Column, {Segment2Column});
        segmentEditor(table, Segment2Column, Wire2Column);
    }
    showRowsOrEmptyMessage(loads_,
        tr("No loads. Click Add Load, or right-click a wire in Geometry."));
    showRowsOrEmptyMessage(lines_, tr("No transmission lines. Click Add Transmission Line, or "
                                      "right-click a wire in Geometry to pick each end."));
    showRowsOrEmptyMessage(networks_, tr("No two-port networks. Click Add Network."));
    validation_ = new QLabel(this); validation_->setStyleSheet(QStringLiteral("color: #9a3030;")); validation_->hide();
    validation_->setObjectName(QStringLiteral("loadNetworkValidation"));
    sharingNotes_ = new QLabel(this);
    sharingNotes_->setObjectName(QStringLiteral("loadNetworkSharingNotes"));
    sharingNotes_->setWordWrap(true);
    sharingNotes_->setTextFormat(Qt::RichText);
    sharingNotes_->hide();
    layout->addWidget(description); layout->addWidget(loadTypes); layout->addWidget(new QLabel(tr("Loads (LD 0–5)"), this)); layout->addWidget(loads_); layout->addLayout(loadButtons);
    layout->addWidget(new QLabel(tr("Transmission Lines (TL)"), this)); layout->addWidget(lines_); layout->addLayout(lineButtons);
    layout->addWidget(new QLabel(tr("Two-Port Networks (NT)"), this)); layout->addWidget(networks_); layout->addLayout(networkButtons);
    layout->addWidget(sharingNotes_); layout->addWidget(validation_);
    layout->addStretch(1);

    connect(addLoad, &QPushButton::clicked, this, [this] {
        if (model_.wires().empty()) return;
        for (auto row = 0; row < loads_->rowCount(); ++row) {
            if (sourceLineAt(loads_, row) != 0) continue;
            loads_->setCurrentCell(row, 0);
            loads_->selectRow(row);
            return;
        }
        const auto& wire = model_.wires().front();
        const auto row = loads_->rowCount();
        loads_->insertRow(row);
        setLoadRow(row, {4, wire.tag, 1, 1, 50.0, 0.0, 0.0, 0});
        loads_->setCurrentCell(row, 0);
        loads_->selectRow(row);
        updateLoadColumns(row);
        setLoadPending(true);
    });
    connect(applyLoadButton_, &QPushButton::clicked, this, [this] { applySelectedLoad(); });
    connect(deleteLoad, &QPushButton::clicked, this, [this] {
        const auto row = loads_->currentRow();
        if (row < 0) return;
        const auto sourceLine = sourceLineAt(loads_, row);
        if (sourceLine == 0) {
            loads_->removeRow(row);
            setLoadPending(false);
        }
        else emit loadDeleteRequested(sourceLine);
    });
    connect(addLine, &QPushButton::clicked, this, [this] {
        if (model_.wires().empty()) return;
        for (auto row = 0; row < lines_->rowCount(); ++row) {
            if (sourceLineAt(lines_, row) != 0) continue;
            lines_->setCurrentCell(row, 0);
            lines_->selectRow(row);
            updateLineApplyState();
            return;
        }
        const auto& first = model_.wires().front();
        const auto& second = model_.wires().back();
        const auto row = lines_->rowCount();
        lines_->insertRow(row);
        setLineRow(row, {first.tag, 1, second.tag, 1, 50.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0});
        lines_->setCurrentCell(row, 0);
        lines_->selectRow(row);
        updateLineApplyState();
        setLinePending(true);
    });
    connect(applyLineButton_, &QPushButton::clicked, this, [this] { applySelectedLine(); });
    connect(deleteLine, &QPushButton::clicked, this, [this] {
        const auto row = lines_->currentRow();
        if (row < 0) return;
        const auto sourceLine = sourceLineAt(lines_, row);
        if (sourceLine == 0) {
            lines_->removeRow(row);
            setLinePending(false);
        }
        else emit transmissionLineDeleteRequested(sourceLine);
        updateLineApplyState();
    });
    connect(addNetwork, &QPushButton::clicked, this, [this] {
        if (model_.wires().empty()) return;
        for (auto row = 0; row < networks_->rowCount(); ++row) {
            if (sourceLineAt(networks_, row) != 0) continue;
            networks_->setCurrentCell(row, 0);
            networks_->selectRow(row);
            updateNetworkApplyState();
            return;
        }
        const auto& first = model_.wires().front();
        const auto& second = model_.wires().back();
        const auto row = networks_->rowCount();
        networks_->insertRow(row);
        setNetworkRow(row, {first.tag, 1, second.tag, 1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0});
        networks_->setCurrentCell(row, 0);
        networks_->selectRow(row);
        updateNetworkApplyState();
        setNetworkPending(true);
    });
    connect(applyNetworkButton_, &QPushButton::clicked, this, [this] { applySelectedNetwork(); });
    connect(deleteNetwork, &QPushButton::clicked, this, [this] {
        const auto row = networks_->currentRow();
        if (row < 0) return;
        const auto sourceLine = sourceLineAt(networks_, row);
        if (sourceLine == 0) {
            networks_->removeRow(row);
            setNetworkPending(false);
        }
        else emit networkDeleteRequested(sourceLine);
        updateNetworkApplyState();
    });
    connect(networks_, &QTableWidget::currentCellChanged, this,
        [this](int row) {
            updateNetworkApplyState();
            showSharingNotes(model::InsertionKind::NetworkPort, row);
            if (const auto line = sourceLineAt(networks_, row)) emit networkSelected(line);
        });
    connect(networks_, &QTableWidget::itemChanged, this, [this] {
        updateNetworkApplyState(); if (!updating_) setNetworkPending(true);
    });
    connect(loads_, &QTableWidget::currentCellChanged, this,
        [this](int row) {
            updateLoadColumns(row);
            showSharingNotes(model::InsertionKind::Load, row);
            if (const auto line = sourceLineAt(loads_, row)) emit loadSelected(line);
        });
    connect(lines_, &QTableWidget::currentCellChanged, this,
        [this](int row) {
            updateLineApplyState();
            showSharingNotes(model::InsertionKind::LineEnd, row);
            if (const auto line = sourceLineAt(lines_, row)) emit transmissionLineSelected(line);
        });
    connect(loads_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_) return;
        const auto row = item->row();
        if (item->column() == TypeColumn) {
            updateLoadValueFields(row, loadTypeAt(loads_, row));
            updateLoadColumns(row);
            loads_->resizeColumnToContents(TypeColumn);
        } else if (item->column() == ScopeColumn) {
            const auto entireWire = entireWireAt(loads_, row);
            auto* first = loads_->item(row, FirstSegmentColumn);
            auto* last = loads_->item(row, LastSegmentColumn);
            if (!entireWire && first != nullptr && last != nullptr) {
                if (first->text().toInt() <= 0) first->setText(QStringLiteral("1"));
                if (last->text().toInt() <= 0) last->setText(first->text());
            }
            updateLoadSegmentFields(row, entireWire);
        }
        setLoadPending(true);
    });
    connect(lines_, &QTableWidget::itemChanged, this, [this] {
        updateLineApplyState(); if (!updating_) setLinePending(true);
    });
    setLengthUnit(lengthUnit_);
}

void LoadNetworkEditor::setData(const model::AntennaModel& model, const model::ModelSetup& setup)
{
    const auto selectedLoad = sourceLineAt(loads_, loads_->currentRow());
    const auto selectedLine = sourceLineAt(lines_, lines_->currentRow());
    const auto selectedNetwork = sourceLineAt(networks_, networks_->currentRow());
    updating_ = true;
    model_ = model;
    setup_ = setup;
    sharedSegments_ = model::describeSharedSegments(model, setup);
    const QSignalBlocker loadBlocker(loads_);
    loads_->setRowCount(static_cast<int>(setup.loads.size()));
    for (auto row = 0; row < static_cast<int>(setup.loads.size()); ++row) {
        setLoadRow(row, setup.loads[row]);
    }
    const QSignalBlocker lineBlocker(lines_);
    lines_->setRowCount(static_cast<int>(setup.transmissionLines.size()));
    for (auto row = 0; row < static_cast<int>(setup.transmissionLines.size()); ++row) {
        setLineRow(row, setup.transmissionLines[row]);
    }
    const QSignalBlocker networkBlocker(networks_);
    networks_->setRowCount(static_cast<int>(setup.networks.size()));
    for (auto row = 0; row < static_cast<int>(setup.networks.size()); ++row) {
        setNetworkRow(row, setup.networks[row]);
    }
    loads_->resizeColumnToContents(TypeColumn);
    loads_->resizeColumnToContents(ScopeColumn);
    markSharedRows();
    selectLoad(selectedLoad);
    selectTransmissionLine(selectedLine);
    selectNetwork(selectedNetwork);
    updating_ = false;
    setLoadPending(false);
    setLinePending(false);
    setNetworkPending(false);
}

auto LoadNetworkEditor::hasPendingEdits() const noexcept -> bool
{
    return loadPending_ || linePending_ || networkPending_;
}

void LoadNetworkEditor::discardPendingEdits()
{
    if (hasPendingEdits()) setData(model_, setup_);
}

void LoadNetworkEditor::setLengthUnit(model::LengthUnit unit)
{
    const auto wasUpdating = updating_;
    updating_ = true;
    if (unit != lengthUnit_) {
        for (auto row = 0; row < lines_->rowCount(); ++row) {
            auto* item = lines_->item(row, LengthColumn);
            if (item == nullptr) continue;
            bool valid = false;
            const auto displayed = item->text().toDouble(&valid);
            if (valid) item->setText(formatEditableDecimal(model::fromMeters(
                model::toMeters(displayed, lengthUnit_), unit)));
        }
        lengthUnit_ = unit;
    }
    const auto symbol = model::lengthUnitSymbol(lengthUnit_);
    lines_->horizontalHeaderItem(LengthColumn)->setText(tr("Length (%1)").arg(
        QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size()))));
    lines_->horizontalHeaderItem(Wire1Column)->setToolTip(tr("GW tag at transmission-line end one."));
    lines_->horizontalHeaderItem(Segment1Column)->setToolTip(tr("Segment connected to transmission-line end one."));
    lines_->horizontalHeaderItem(Wire2Column)->setToolTip(tr("GW tag at transmission-line end two."));
    lines_->horizontalHeaderItem(Segment2Column)->setToolTip(tr("Segment connected to transmission-line end two."));
    lines_->horizontalHeaderItem(ImpedanceColumn)->setToolTip(tr("Characteristic impedance in ohms. Use a negative value for a crossed line with 180° phase reversal."));
    lines_->horizontalHeaderItem(LengthColumn)->setToolTip(tr("Physical line length in display units. Zero tells NEC to use straight-line endpoint distance."));
    lines_->horizontalHeaderItem(ShuntReal1Column)->setToolTip(tr("Real shunt admittance (conductance) at end one, in siemens."));
    lines_->horizontalHeaderItem(ShuntImaginary1Column)->setToolTip(tr("Imaginary shunt admittance (susceptance) at end one, in siemens."));
    lines_->horizontalHeaderItem(ShuntReal2Column)->setToolTip(tr("Real shunt admittance (conductance) at end two, in siemens."));
    lines_->horizontalHeaderItem(ShuntImaginary2Column)->setToolTip(tr("Imaginary shunt admittance (susceptance) at end two, in siemens."));
    updating_ = wasUpdating;
}

void LoadNetworkEditor::selectLoad(std::size_t sourceLine)
{
    const QSignalBlocker blocker(loads_);
    for (auto row = 0; row < loads_->rowCount(); ++row) {
        if (sourceLineAt(loads_, row) == sourceLine) {
            loads_->selectRow(row);
            updateLoadColumns(row);
            showSharingNotes(model::InsertionKind::Load, row);
            return;
        }
    }
    loads_->clearSelection();
    updateLoadColumns(-1);
    showSharingNotes(model::InsertionKind::Load, -1);
}

void LoadNetworkEditor::selectTransmissionLine(std::size_t sourceLine)
{
    const QSignalBlocker blocker(lines_);
    for (auto row = 0; row < lines_->rowCount(); ++row) {
        if (sourceLineAt(lines_, row) == sourceLine) {
            lines_->selectRow(row);
            updateLineApplyState();
            showSharingNotes(model::InsertionKind::LineEnd, row);
            return;
        }
    }
    lines_->clearSelection();
    updateLineApplyState();
}

void LoadNetworkEditor::selectNetwork(std::size_t sourceLine)
{
    const QSignalBlocker blocker(networks_);
    for (auto row = 0; row < networks_->rowCount(); ++row) {
        if (sourceLineAt(networks_, row) == sourceLine) {
            networks_->selectRow(row);
            updateNetworkApplyState();
            showSharingNotes(model::InsertionKind::NetworkPort, row);
            return;
        }
    }
    networks_->clearSelection();
    updateNetworkApplyState();
}

void LoadNetworkEditor::focusNetworks()
{
    networks_->setFocus(Qt::OtherFocusReason);
    if (networks_->rowCount() > 0 && networks_->currentRow() < 0) {
        networks_->setCurrentCell(0, 0);
        networks_->selectRow(0);
    }
}

auto LoadNetworkEditor::sharingNotes(model::InsertionKind kind, int number) const -> QStringList
{
    QStringList notes;
    for (const auto& shared : sharedSegments_) {
        if (std::ranges::any_of(shared.objects, [kind, number](const auto& object) {
                return object.kind == kind && object.number == number;
            })) notes.append(QString::fromStdString(shared.description));
    }
    return notes;
}

// Rows are numbered as the descriptions name them (LD 1, TL 1), so the row header carries the
// marker. A line end sharing only with its source is the usual feed and is left unmarked.
void LoadNetworkEditor::markSharedRows()
{
    const auto mark = [this](QTableWidget* table, model::InsertionKind kind, int count) {
        for (auto row = 0; row < table->rowCount(); ++row) {
            auto* header = new QTableWidgetItem(QString::number(row + 1));
            if (row < count) {
                const auto number = row + 1;
                auto withLoad = false;
                auto withPort = false;
                for (const auto& shared : sharedSegments_) {
                    if (std::ranges::none_of(shared.objects, [kind, number](const auto& object) {
                            return object.kind == kind && object.number == number;
                        })) continue;
                    for (const auto& object : shared.objects) {
                        withLoad = withLoad || object.kind == model::InsertionKind::Load;
                        withPort = withPort || object.kind == model::InsertionKind::LineEnd
                            || object.kind == model::InsertionKind::NetworkPort;
                    }
                }
                // A load row always counts itself, so withLoad means "shares with something".
                if (withLoad) {
                    header->setIcon(style()->standardIcon(withPort
                            ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxInformation));
                    header->setToolTip(sharingNotes(kind, number).join(QStringLiteral("\n\n")));
                }
            }
            table->setVerticalHeaderItem(row, header);
        }
    };
    mark(loads_, model::InsertionKind::Load, static_cast<int>(setup_.loads.size()));
    mark(lines_, model::InsertionKind::LineEnd, static_cast<int>(setup_.transmissionLines.size()));
    mark(networks_, model::InsertionKind::NetworkPort, static_cast<int>(setup_.networks.size()));
}

void LoadNetworkEditor::showSharingNotes(model::InsertionKind kind, int row)
{
    const auto count = kind == model::InsertionKind::Load ? setup_.loads.size()
        : kind == model::InsertionKind::NetworkPort ? setup_.networks.size()
                                                    : setup_.transmissionLines.size();
    const auto notes = row >= 0 && static_cast<std::size_t>(row) < count
        ? sharingNotes(kind, row + 1) : QStringList{};
    if (notes.isEmpty()) {
        sharingNotes_->hide();
        return;
    }
    QString text = tr("<b>How NEC connects this segment</b>");
    for (const auto& note : notes) text += QStringLiteral("<p>%1</p>").arg(note.toHtmlEscaped());
    sharingNotes_->setText(text);
    sharingNotes_->show();
}

void LoadNetworkEditor::applySelectedLoad()
{
    const auto row = loads_->currentRow(); if (row < 0) return; bool valid = true;
    model::LoadDefinition value; value.type = loadTypeAt(loads_, row); if (value.type < 0) goto invalid;
    value.wireTag = loads_->item(row, WireColumn)->text().toInt(&valid); if (!valid) goto invalid;
    if (entireWireAt(loads_, row)) {
        value.firstSegment = 0;
        value.lastSegment = 0;
    } else {
        value.firstSegment = loads_->item(row, FirstSegmentColumn)->text().toInt(&valid); if (!valid) goto invalid;
        value.lastSegment = loads_->item(row, LastSegmentColumn)->text().toInt(&valid); if (!valid) goto invalid;
    }
    value.value1 = loads_->item(row, Value1Column)->text().toDouble(&valid)
        / displayScaleFor(value.type, Value1Column); if (!valid) goto invalid;
    value.value2 = value.type >= 5 ? 0.0
        : loads_->item(row, Value2Column)->text().toDouble(&valid)
            / displayScaleFor(value.type, Value2Column); if (!valid) goto invalid;
    value.value3 = value.type >= 4 ? 0.0
        : loads_->item(row, Value3Column)->text().toDouble(&valid)
            / displayScaleFor(value.type, Value3Column); if (!valid) goto invalid;
    if (value.type < 0 || value.type > 5) goto invalid_reference;
    if (const auto count = model::referencedSegmentCount(model_, value.wireTag); count == 0
        || value.firstSegment < 0 || value.lastSegment < 0
        || value.firstSegment > count || value.lastSegment > count
        || (value.firstSegment != 0 && value.lastSegment != 0
            && value.firstSegment > value.lastSegment)) goto invalid_reference;
    if (value.type == 5 && value.value1 <= 0.0) goto invalid_value;
    value.sourceLine = loads_->item(row,0)->data(SourceLineRole).toULongLong(); validation_->hide(); setLoadPending(false); emit loadChanged(value); return;
invalid: validation_->setText(tr("The selected LD row contains an invalid number.")); validation_->show();
    return;
invalid_reference: validation_->setText(tr("LD type, wire tag, or segment range is invalid.")); validation_->show();
    return;
invalid_value: validation_->setText(tr("Wire conductivity must be greater than zero.")); validation_->show();
}

void LoadNetworkEditor::setLoadRow(int row, const model::LoadDefinition& value)
{
    const auto wasUpdating = updating_;
    updating_ = true;
    const auto entireWire = value.firstSegment == 0 && value.lastSegment == 0;
    const auto type = std::ranges::find(loadTypeChoices(), value.type,
        &std::pair<int, const char*>::first);
    const QStringList cells{
        type == loadTypeChoices().end() ? QString::number(value.type)
            : QCoreApplication::translate("LoadNetworkEditor", type->second),
        QString::number(value.wireTag), scopeLabel(entireWire),
        QString::number(value.firstSegment), QString::number(value.lastSegment),
        formatEditableDecimal(value.value1 * displayScaleFor(value.type, Value1Column)),
        formatEditableDecimal(value.value2 * displayScaleFor(value.type, Value2Column)),
        formatEditableDecimal(value.value3 * displayScaleFor(value.type, Value3Column))};
    for (auto column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(cells[column]);
        item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine));
        loads_->setItem(row, column, item);
    }
    loads_->item(row, TypeColumn)->setData(ChoiceValueRole, value.type);
    loads_->item(row, TypeColumn)->setToolTip(
        tr("NEC-2 LD load type. The value columns change meaning with this selection."));
    loads_->item(row, WireColumn)->setToolTip(tr("Double-click to choose the GW wire tag."));
    loads_->item(row, ScopeColumn)->setData(ChoiceValueRole, entireWire);
    loads_->item(row, ScopeColumn)->setToolTip(
        tr("Apply the load to every segment on the tagged wire or to a segment range."));
    updateLoadSegmentFields(row, entireWire);
    updateLoadValueFields(row, value.type);
    updating_ = wasUpdating;
}

void LoadNetworkEditor::updateLoadColumns(int row)
{
    QStringList headers{tr("Type"), tr("Wire"), tr("Scope"), tr("First seg"), tr("Last seg")};
    switch (loadTypeAt(loads_, row)) {
    case 0:
    case 1:
        headers.append({tr("Resistance (Ω)"), tr("Inductance (µH)"), tr("Capacitance (pF)")});
        break;
    case 2:
    case 3:
        headers.append({tr("Resistance (Ω/m)"), tr("Inductance (µH/m)"), tr("Capacitance (pF/m)")});
        break;
    case 4:
        headers.append({tr("Resistance (Ω)"), tr("Reactance (Ω)"), tr("Unused")});
        break;
    case 5:
        headers.append({tr("Conductivity (MS/m)"), tr("Unused"), tr("Unused")});
        break;
    default:
        headers.append({tr("Value 1"), tr("Value 2"), tr("Value 3")});
        break;
    }
    loads_->setHorizontalHeaderLabels(headers);
    loads_->horizontalHeaderItem(TypeColumn)->setToolTip(tr("NEC-2 LD type 0 through 5."));
    loads_->horizontalHeaderItem(WireColumn)->setToolTip(tr("GW tag number receiving the load."));
    loads_->horizontalHeaderItem(ScopeColumn)->setToolTip(tr("Load the entire tagged wire or a selected segment range."));
    loads_->horizontalHeaderItem(FirstSegmentColumn)->setToolTip(tr("First loaded segment when Segment range is selected."));
    loads_->horizontalHeaderItem(LastSegmentColumn)->setToolTip(tr("Last loaded segment when Segment range is selected."));
    for (auto column = static_cast<int>(Value1Column); column <= Value3Column; ++column) {
        loads_->horizontalHeaderItem(column)->setToolTip(headers[column]);
    }
}

void LoadNetworkEditor::updateLoadSegmentFields(int row, bool entireWire)
{
    for (const auto column : {FirstSegmentColumn, LastSegmentColumn}) {
        auto* item = loads_->item(row, column);
        if (item == nullptr) continue;
        item->setFlags(entireWire ? item->flags() & ~Qt::ItemIsEditable
                                  : item->flags() | Qt::ItemIsEditable);
        item->setForeground(entireWire ? palette().brush(QPalette::PlaceholderText)
                                       : palette().brush(QPalette::Text));
        item->setToolTip(entireWire
            ? tr("Ignored because Entire wire is selected; NEC writes 0 for both segment fields.")
            : tr("Inclusive segment number on the selected wire."));
    }
}

void LoadNetworkEditor::updateLoadValueFields(int row, int type)
{
    const QStringList tooltips = type <= 1
        ? QStringList{tr("Resistance in ohms."), tr("Inductance in microhenries; written to NEC in henries."), tr("Capacitance in picofarads; written to NEC in farads.")}
        : type <= 3
            ? QStringList{tr("Resistance per meter."), tr("Inductance in microhenries per meter; written to NEC in henries per meter."), tr("Capacitance in picofarads per meter; written to NEC in farads per meter.")}
            : type == 4
                ? QStringList{tr("Fixed resistance in ohms."), tr("Fixed reactance in ohms."), tr("Not used by LD type 4.")}
                : QStringList{tr("Wire conductivity in megasiemens per meter; written to NEC in siemens per meter."), tr("Not used by LD type 5."), tr("Not used by LD type 5.")};
    for (auto column = static_cast<int>(Value1Column); column <= Value3Column; ++column) {
        auto* item = loads_->item(row, column);
        if (item == nullptr) continue;
        const auto used = column == Value1Column
            || (column == Value2Column && type < 5)
            || (column == Value3Column && type < 4);
        item->setFlags(used ? item->flags() | Qt::ItemIsEditable
                            : item->flags() & ~Qt::ItemIsEditable);
        item->setForeground(used ? palette().brush(QPalette::Text)
                                 : palette().brush(QPalette::PlaceholderText));
        item->setToolTip(tooltips[column - Value1Column]);
    }
}

void LoadNetworkEditor::setLineRow(int row, const model::TransmissionLineDefinition& value)
{
    const QStringList cells{QString::number(value.wireTag1), QString::number(value.segment1),
        QString::number(value.wireTag2), QString::number(value.segment2),
        formatEditableDecimal(value.characteristicImpedance),
        formatEditableDecimal(model::fromMeters(value.lengthMeters, lengthUnit_)),
        formatEditableDecimal(value.shuntReal1), formatEditableDecimal(value.shuntImaginary1),
        formatEditableDecimal(value.shuntReal2), formatEditableDecimal(value.shuntImaginary2)};
    for (auto column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(cells[column]);
        item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine));
        lines_->setItem(row, column, item);
    }

    setPortToolTips(lines_, row, tr("transmission-line end"));

    lines_->item(row, ImpedanceColumn)->setToolTip(
        tr("Characteristic impedance in ohms. A negative value creates a crossed line."));
    lines_->item(row, LengthColumn)->setToolTip(
        tr("Physical length in display units. Enter zero to use endpoint distance."));
    for (auto column = static_cast<int>(ShuntReal1Column);
         column <= ShuntImaginary2Column; ++column) {
        lines_->item(row, column)->setToolTip(
            tr("Optional endpoint shunt admittance component in siemens."));
    }
}

void LoadNetworkEditor::setNetworkRow(int row, const model::NetworkDefinition& value)
{
    const QStringList cells{QString::number(value.wireTag1), QString::number(value.segment1),
        QString::number(value.wireTag2), QString::number(value.segment2),
        formatEditableDecimal(value.y11Real), formatEditableDecimal(value.y11Imaginary),
        formatEditableDecimal(value.y12Real), formatEditableDecimal(value.y12Imaginary),
        formatEditableDecimal(value.y22Real), formatEditableDecimal(value.y22Imaginary)};
    for (auto column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(cells[column]);
        item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine));
        networks_->setItem(row, column, item);
    }
    setPortToolTips(networks_, row, tr("network port"));
    for (auto column = static_cast<int>(Y11RealColumn); column <= Y22ImaginaryColumn; ++column)
        networks_->item(row, column)->setToolTip(tr("Admittance in siemens."));
}

void LoadNetworkEditor::updateNetworkApplyState()
{
    const auto row = networks_->currentRow();
    if (row < 0 || networks_->item(row, 0) == nullptr) {
        applyNetworkButton_->setEnabled(false);
        return;
    }
    auto valid = true;
    for (const auto& [wireColumn, segmentColumn] :
        {std::pair{Wire1Column, Segment1Column}, std::pair{Wire2Column, Segment2Column}}) {
        bool validWire = false;
        bool validSegment = false;
        const auto wireTag = cellIntAt(networks_, row, wireColumn, &validWire);
        const auto segment = cellIntAt(networks_, row, segmentColumn, &validSegment);
        valid = valid && validWire && validSegment
            && model::referencedSegment(model_, wireTag, segment).has_value();
    }
    for (auto column = static_cast<int>(Y11RealColumn); column <= Y22ImaginaryColumn; ++column) {
        bool number = false;
        networks_->item(row, column)->text().toDouble(&number);
        valid = valid && number;
    }
    applyNetworkButton_->setEnabled(valid);
}

void LoadNetworkEditor::updateLineApplyState()
{
    const auto row = lines_->currentRow();
    if (row < 0) {
        applyLineButton_->setEnabled(false);
        return;
    }
    bool validWire1 = false;
    bool validSegment1 = false;
    bool validWire2 = false;
    bool validSegment2 = false;
    const auto wire1 = cellIntAt(lines_, row, Wire1Column, &validWire1);
    const auto segment1 = cellIntAt(lines_, row, Segment1Column, &validSegment1);
    const auto wire2 = cellIntAt(lines_, row, Wire2Column, &validWire2);
    const auto segment2 = cellIntAt(lines_, row, Segment2Column, &validSegment2);
    bool validImpedance = false;
    bool validLength = false;
    const auto impedance = lines_->item(row, ImpedanceColumn)->text().toDouble(&validImpedance);
    const auto length = lines_->item(row, LengthColumn)->text().toDouble(&validLength);
    auto validValues = true;
    for (auto column = static_cast<int>(ShuntReal1Column);
         column <= ShuntImaginary2Column; ++column) {
        bool valid = false;
        lines_->item(row, column)->text().toDouble(&valid);
        validValues = validValues && valid;
    }
    applyLineButton_->setEnabled(validWire1 && validSegment1 && validWire2
        && validSegment2 && model::referencedSegment(model_, wire1, segment1).has_value()
        && model::referencedSegment(model_, wire2, segment2).has_value()
        && validImpedance && impedance != 0.0 && validLength && length >= 0.0
        && validValues);
}

void LoadNetworkEditor::setLoadPending(bool pending)
{
    loadPending_ = pending;
    setPendingEditIndicator(applyLoadButton_, pending);
}

void LoadNetworkEditor::setLinePending(bool pending)
{
    linePending_ = pending;
    setPendingEditIndicator(applyLineButton_, pending);
}

void LoadNetworkEditor::setNetworkPending(bool pending)
{
    networkPending_ = pending;
    setPendingEditIndicator(applyNetworkButton_, pending);
}

void LoadNetworkEditor::applySelectedNetwork()
{
    const auto row = networks_->currentRow();
    if (row < 0) return;
    model::NetworkDefinition value;
    auto valid = true;
    const auto integer = [this, row, &valid](int column) {
        bool ok = false;
        const auto result = cellIntAt(networks_, row, column, &ok);
        valid = valid && ok;
        return result;
    };
    const auto number = [this, row, &valid](int column) {
        bool ok = false;
        const auto result = networks_->item(row, column)->text().toDouble(&ok);
        valid = valid && ok;
        return result;
    };
    value.wireTag1 = integer(Wire1Column);
    value.segment1 = integer(Segment1Column);
    value.wireTag2 = integer(Wire2Column);
    value.segment2 = integer(Segment2Column);
    value.y11Real = number(Y11RealColumn);
    value.y11Imaginary = number(Y11ImaginaryColumn);
    value.y12Real = number(Y12RealColumn);
    value.y12Imaginary = number(Y12ImaginaryColumn);
    value.y22Real = number(Y22RealColumn);
    value.y22Imaginary = number(Y22ImaginaryColumn);
    if (!valid) {
        validation_->setText(tr("The selected NT row contains an invalid number."));
        validation_->show();
        return;
    }
    const auto validPort = [this](int wireTag, int segment) {
        return model::referencedSegment(model_, wireTag, segment).has_value();
    };
    if (!validPort(value.wireTag1, value.segment1) || !validPort(value.wireTag2, value.segment2)) {
        validation_->setText(tr("NT ports must reference valid segments."));
        validation_->show();
        return;
    }
    value.sourceLine = sourceLineAt(networks_, row);
    validation_->hide();
    setNetworkPending(false);
    emit networkChanged(value);
}

void LoadNetworkEditor::applySelectedLine()
{
    const auto row = lines_->currentRow(); if (row < 0) return; bool valid = true; model::TransmissionLineDefinition value;
    value.wireTag1=cellIntAt(lines_,row,Wire1Column,&valid); if(!valid) goto invalid;
    value.segment1=cellIntAt(lines_,row,Segment1Column,&valid); if(!valid) goto invalid;
    value.wireTag2=cellIntAt(lines_,row,Wire2Column,&valid); if(!valid) goto invalid;
    value.segment2=cellIntAt(lines_,row,Segment2Column,&valid); if(!valid) goto invalid;
    value.characteristicImpedance=lines_->item(row,ImpedanceColumn)->text().toDouble(&valid); if(!valid) goto invalid;
    value.lengthMeters=model::toMeters(lines_->item(row,LengthColumn)->text().toDouble(&valid), lengthUnit_); if(!valid) goto invalid;
    value.shuntReal1=lines_->item(row,ShuntReal1Column)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntImaginary1=lines_->item(row,ShuntImaginary1Column)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntReal2=lines_->item(row,ShuntReal2Column)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntImaginary2=lines_->item(row,ShuntImaginary2Column)->text().toDouble(&valid); if(!valid) goto invalid;
    if (!model::referencedSegment(model_, value.wireTag1, value.segment1)
        || !model::referencedSegment(model_, value.wireTag2, value.segment2)) goto invalid_reference;
    if (value.characteristicImpedance == 0.0 || value.lengthMeters < 0.0) goto invalid_reference;
    value.sourceLine=lines_->item(row,0)->data(SourceLineRole).toULongLong(); validation_->hide(); setLinePending(false); emit transmissionLineChanged(value); return;
invalid: validation_->setText(tr("The selected TL row contains an invalid number.")); validation_->show();
    return;
invalid_reference: validation_->setText(tr("TL endpoints must reference valid segments and Z0 must be nonzero.")); validation_->show();
}

}
