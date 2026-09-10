#include "ui/setup/LoadNetworkEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/PendingEditIndicator.h"

#include <array>
#include <utility>

#include <QComboBox>
#include <QCoreApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {
constexpr auto SourceLineRole = Qt::UserRole;
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

auto sourceLineAt(QTableWidget* table, int row) -> std::size_t
{
    return row < 0 || table->item(row, 0) == nullptr
        ? 0 : table->item(row, 0)->data(SourceLineRole).toULongLong();
}

auto loadTypeAt(QTableWidget* table, int row) -> int
{
    if (row < 0) return -1;
    if (const auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, 0))) {
        if (const auto data = combo->currentData(); data.isValid()) return data.toInt();
    }
    return table->item(row, 0) == nullptr ? -1 : table->item(row, 0)->text().toInt();
}

auto entireWireAt(QTableWidget* table, int row) -> bool
{
    if (row < 0) return false;
    if (const auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, ScopeColumn))) {
        return combo->currentData().toBool();
    }
    return false;
}

auto comboIntAt(QTableWidget* table, int row, int column, bool* valid = nullptr) -> int
{
    if (const auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, column))) {
        const auto data = combo->currentData();
        if (valid != nullptr) *valid = data.isValid();
        return data.toInt();
    }
    if (valid != nullptr) *valid = false;
    return 0;
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
    auto* description = new QLabel(tr("Edit lumped/distributed loads and transmission-line connections. Double-click a cell, then apply the selected row."), this);
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
    validation_ = new QLabel(this); validation_->setStyleSheet(QStringLiteral("color: #9a3030;")); validation_->hide();
    validation_->setObjectName(QStringLiteral("loadNetworkValidation"));
    layout->addWidget(description); layout->addWidget(loadTypes); layout->addWidget(new QLabel(tr("Loads (LD 0–5)"), this)); layout->addWidget(loads_, 1); layout->addLayout(loadButtons);
    layout->addWidget(new QLabel(tr("Transmission Lines (TL)"), this)); layout->addWidget(lines_, 1); layout->addLayout(lineButtons); layout->addWidget(validation_);

    connect(addLoad, &QPushButton::clicked, this, [this] {
        if (model_.empty()) return;
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
        if (model_.empty()) return;
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
    connect(loads_, &QTableWidget::currentCellChanged, this,
        [this](int row) {
            updateLoadColumns(row);
            if (const auto line = sourceLineAt(loads_, row)) emit loadSelected(line);
        });
    connect(lines_, &QTableWidget::currentCellChanged, this,
        [this](int row) {
            updateLineApplyState();
            if (const auto line = sourceLineAt(lines_, row)) emit transmissionLineSelected(line);
        });
    connect(loads_, &QTableWidget::itemChanged, this,
        [this] { if (!updating_) setLoadPending(true); });
    connect(lines_, &QTableWidget::itemChanged, this, [this] {
        updateLineApplyState(); if (!updating_) setLinePending(true);
    });
    setLengthUnit(lengthUnit_);
}

void LoadNetworkEditor::setData(const model::AntennaModel& model, const model::ModelSetup& setup)
{
    const auto selectedLoad = sourceLineAt(loads_, loads_->currentRow());
    const auto selectedLine = sourceLineAt(lines_, lines_->currentRow());
    updating_ = true;
    model_ = model;
    setup_ = setup;
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
    selectLoad(selectedLoad);
    selectTransmissionLine(selectedLine);
    updating_ = false;
    setLoadPending(false);
    setLinePending(false);
}

auto LoadNetworkEditor::hasPendingEdits() const noexcept -> bool
{
    return loadPending_ || linePending_;
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
            if (valid) item->setText(formatDecimal(model::fromMeters(
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
            return;
        }
    }
    loads_->clearSelection();
    updateLoadColumns(-1);
}

void LoadNetworkEditor::selectTransmissionLine(std::size_t sourceLine)
{
    const QSignalBlocker blocker(lines_);
    for (auto row = 0; row < lines_->rowCount(); ++row) {
        if (sourceLineAt(lines_, row) == sourceLine) {
            lines_->selectRow(row);
            updateLineApplyState();
            return;
        }
    }
    lines_->clearSelection();
    updateLineApplyState();
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
    if (const auto* wire = model_.wireByTag(value.wireTag); wire == nullptr
        || value.firstSegment < 0 || value.lastSegment < 0
        || value.firstSegment > wire->segments || value.lastSegment > wire->segments
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
    const QStringList cells{QString::number(value.type), QString::number(value.wireTag), {},
        QString::number(value.firstSegment), QString::number(value.lastSegment),
        formatDecimal(value.value1 * displayScaleFor(value.type, Value1Column)),
        formatDecimal(value.value2 * displayScaleFor(value.type, Value2Column)),
        formatDecimal(value.value3 * displayScaleFor(value.type, Value3Column))};
    for (auto column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(cells[column]);
        item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine));
        loads_->setItem(row, column, item);
    }
    auto* type = new QComboBox(loads_);
    type->setObjectName(QStringLiteral("loadTypeCombo"));
    for (const auto& [number, label] : loadTypeChoices()) {
        type->addItem(QCoreApplication::translate("LoadNetworkEditor", label), number);
    }
    type->setCurrentIndex(type->findData(value.type));
    connect(type, &QComboBox::currentIndexChanged, this, [this, row, type] {
        const auto selectedType = type->currentData().toInt();
        loads_->item(row, 0)->setText(QString::number(selectedType));
        loads_->setCurrentCell(row, 0);
        updateLoadValueFields(row, selectedType);
        updateLoadColumns(row);
        if (!updating_) setLoadPending(true);
    });
    loads_->setCellWidget(row, 0, type);
    type->setToolTip(tr("NEC-2 LD load type. The value columns change meaning with this selection."));

    auto* scope = new QComboBox(loads_);
    scope->setObjectName(QStringLiteral("loadScopeCombo"));
    scope->addItem(tr("Entire wire"), true);
    scope->addItem(tr("Segment range"), false);
    scope->setCurrentIndex((value.firstSegment == 0 && value.lastSegment == 0) ? 0 : 1);
    scope->setToolTip(tr("Apply the load to every segment on the tagged wire or to a segment range."));
    connect(scope, &QComboBox::currentIndexChanged, this, [this, row, scope] {
        const auto entireWire = scope->currentData().toBool();
        if (!entireWire) {
            auto* first = loads_->item(row, FirstSegmentColumn);
            auto* last = loads_->item(row, LastSegmentColumn);
            if (first->text().toInt() <= 0) first->setText(QStringLiteral("1"));
            if (last->text().toInt() <= 0) last->setText(first->text());
        }
        updateLoadSegmentFields(row, entireWire);
        loads_->setCurrentCell(row, ScopeColumn);
        if (!updating_) setLoadPending(true);
    });
    loads_->setCellWidget(row, ScopeColumn, scope);
    updateLoadSegmentFields(row, entireWireAt(loads_, row));
    updateLoadValueFields(row, value.type);
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

void LoadNetworkEditor::populateSegmentChoices(
    QComboBox* control, int wireTag, int selectedSegment)
{
    const QSignalBlocker blocker(control);
    control->clear();
    const auto* wire = model_.wireByTag(wireTag);
    if (wire == nullptr) return;
    for (auto segment = 1; segment <= wire->segments; ++segment) {
        control->addItem(QString::number(segment), segment);
    }
    const auto selectedIndex = control->findData(selectedSegment);
    control->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
}

void LoadNetworkEditor::setLineRow(int row, const model::TransmissionLineDefinition& value)
{
    const QStringList cells{QString::number(value.wireTag1), QString::number(value.segment1),
        QString::number(value.wireTag2), QString::number(value.segment2),
        formatDecimal(value.characteristicImpedance),
        formatDecimal(model::fromMeters(value.lengthMeters, lengthUnit_)),
        formatDecimal(value.shuntReal1), formatDecimal(value.shuntImaginary1),
        formatDecimal(value.shuntReal2), formatDecimal(value.shuntImaginary2)};
    for (auto column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(cells[column]);
        item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine));
        lines_->setItem(row, column, item);
    }

    const auto addEndpoint = [this, row](int wireColumn, int segmentColumn,
                                 int wireTag, int segment, const QString& suffix) {
        auto* wireControl = new QComboBox(lines_);
        wireControl->setObjectName(QStringLiteral("lineWire%1Combo").arg(suffix));
        for (const auto& wire : model_.wires()) {
            wireControl->addItem(tr("Wire %1").arg(wire.tag), wire.tag);
        }
        wireControl->setCurrentIndex(wireControl->findData(wireTag));
        wireControl->setToolTip(tr("Choose the GW wire tag for this transmission-line endpoint."));

        auto* segmentControl = new QComboBox(lines_);
        segmentControl->setObjectName(QStringLiteral("lineSegment%1Combo").arg(suffix));
        populateSegmentChoices(segmentControl, wireTag, segment);
        segmentControl->setToolTip(tr("Choose the segment where this transmission-line endpoint connects."));

        connect(wireControl, &QComboBox::currentIndexChanged, this,
            [this, row, wireColumn, segmentColumn, wireControl, segmentControl] {
                const auto wireTag = wireControl->currentData().toInt();
                lines_->item(row, wireColumn)->setText(QString::number(wireTag));
                populateSegmentChoices(segmentControl, wireTag,
                    segmentControl->currentData().toInt());
                lines_->item(row, segmentColumn)->setText(
                    QString::number(segmentControl->currentData().toInt()));
                lines_->setCurrentCell(row, wireColumn);
                updateLineApplyState();
            });
        connect(segmentControl, &QComboBox::currentIndexChanged, this,
            [this, row, segmentColumn, segmentControl] {
                lines_->item(row, segmentColumn)->setText(
                    QString::number(segmentControl->currentData().toInt()));
                lines_->setCurrentCell(row, segmentColumn);
                updateLineApplyState();
            });
        lines_->setCellWidget(row, wireColumn, wireControl);
        lines_->setCellWidget(row, segmentColumn, segmentControl);
    };
    addEndpoint(Wire1Column, Segment1Column, value.wireTag1, value.segment1,
        QStringLiteral("1"));
    addEndpoint(Wire2Column, Segment2Column, value.wireTag2, value.segment2,
        QStringLiteral("2"));

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
    const auto wire1 = comboIntAt(lines_, row, Wire1Column, &validWire1);
    const auto segment1 = comboIntAt(lines_, row, Segment1Column, &validSegment1);
    const auto wire2 = comboIntAt(lines_, row, Wire2Column, &validWire2);
    const auto segment2 = comboIntAt(lines_, row, Segment2Column, &validSegment2);
    bool validImpedance = false;
    bool validLength = false;
    const auto impedance = lines_->item(row, ImpedanceColumn)->text().toDouble(&validImpedance);
    const auto length = lines_->item(row, LengthColumn)->text().toDouble(&validLength);
    const auto* first = model_.wireByTag(wire1);
    const auto* second = model_.wireByTag(wire2);
    auto validValues = true;
    for (auto column = static_cast<int>(ShuntReal1Column);
         column <= ShuntImaginary2Column; ++column) {
        bool valid = false;
        lines_->item(row, column)->text().toDouble(&valid);
        validValues = validValues && valid;
    }
    applyLineButton_->setEnabled(validWire1 && validSegment1 && validWire2
        && validSegment2 && first != nullptr && second != nullptr
        && segment1 >= 1 && segment1 <= first->segments
        && segment2 >= 1 && segment2 <= second->segments
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

void LoadNetworkEditor::applySelectedLine()
{
    const auto row = lines_->currentRow(); if (row < 0) return; bool valid = true; model::TransmissionLineDefinition value;
    value.wireTag1=comboIntAt(lines_,row,Wire1Column,&valid); if(!valid) goto invalid;
    value.segment1=comboIntAt(lines_,row,Segment1Column,&valid); if(!valid) goto invalid;
    value.wireTag2=comboIntAt(lines_,row,Wire2Column,&valid); if(!valid) goto invalid;
    value.segment2=comboIntAt(lines_,row,Segment2Column,&valid); if(!valid) goto invalid;
    value.characteristicImpedance=lines_->item(row,ImpedanceColumn)->text().toDouble(&valid); if(!valid) goto invalid;
    value.lengthMeters=model::toMeters(lines_->item(row,LengthColumn)->text().toDouble(&valid), lengthUnit_); if(!valid) goto invalid;
    value.shuntReal1=lines_->item(row,ShuntReal1Column)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntImaginary1=lines_->item(row,ShuntImaginary1Column)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntReal2=lines_->item(row,ShuntReal2Column)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntImaginary2=lines_->item(row,ShuntImaginary2Column)->text().toDouble(&valid); if(!valid) goto invalid;
    if (const auto* first = model_.wireByTag(value.wireTag1); first == nullptr
        || value.segment1 < 1 || value.segment1 > first->segments) goto invalid_reference;
    if (const auto* second = model_.wireByTag(value.wireTag2); second == nullptr
        || value.segment2 < 1 || value.segment2 > second->segments) goto invalid_reference;
    if (value.characteristicImpedance == 0.0 || value.lengthMeters < 0.0) goto invalid_reference;
    value.sourceLine=lines_->item(row,0)->data(SourceLineRole).toULongLong(); validation_->hide(); setLinePending(false); emit transmissionLineChanged(value); return;
invalid: validation_->setText(tr("The selected TL row contains an invalid number.")); validation_->show();
    return;
invalid_reference: validation_->setText(tr("TL endpoints must reference valid segments and Z0 must be nonzero.")); validation_->show();
}

}
