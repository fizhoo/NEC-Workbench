#include "ui/setup/LoadNetworkEditor.h"

#include "ui/DisplayFormat.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {
constexpr auto SourceLineRole = Qt::UserRole;

auto sourceLineAt(QTableWidget* table, int row) -> std::size_t
{
    return row < 0 || table->item(row, 0) == nullptr
        ? 0 : table->item(row, 0)->data(SourceLineRole).toULongLong();
}
}

LoadNetworkEditor::LoadNetworkEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr("Edit lumped/distributed loads and transmission-line connections. Double-click a cell, then apply the selected row."), this);
    description->setWordWrap(true);
    auto* loadTypes = new QLabel(tr("LD types: 0 series RLC, 1 parallel RLC, 2 distributed series RLC, 3 distributed parallel RLC, 4 fixed complex impedance, 5 wire conductivity."), this);
    loadTypes->setWordWrap(true);
    loads_ = new QTableWidget(0, 7, this);
    loads_->setObjectName(QStringLiteral("loadNetworkLoadsTable"));
    loads_->setHorizontalHeaderLabels({tr("Type"), tr("Wire"), tr("First seg"), tr("Last seg"), tr("R / Value 1"), tr("L / X / Value 2"), tr("C / Value 3")});
    loads_->horizontalHeader()->setStretchLastSection(true);
    auto* loadButtons = new QHBoxLayout;
    auto* addLoad = new QPushButton(tr("Add Load"), this);
    auto* applyLoad = new QPushButton(tr("Apply Selected Load"), this);
    applyLoad->setObjectName(QStringLiteral("applySelectedLoadButton"));
    auto* deleteLoad = new QPushButton(tr("Delete Selected Load"), this);
    loadButtons->addWidget(addLoad); loadButtons->addWidget(applyLoad); loadButtons->addWidget(deleteLoad); loadButtons->addStretch();

    lines_ = new QTableWidget(0, 10, this);
    lines_->setObjectName(QStringLiteral("loadNetworkLinesTable"));
    lines_->setHorizontalHeaderLabels({tr("Wire 1"), tr("Seg 1"), tr("Wire 2"), tr("Seg 2"), tr("Z0 (Ω)"), tr("Length (m)"), tr("Y1 real"), tr("Y1 imag"), tr("Y2 real"), tr("Y2 imag")});
    lines_->horizontalHeader()->setStretchLastSection(true);
    auto* lineButtons = new QHBoxLayout;
    auto* addLine = new QPushButton(tr("Add Transmission Line"), this);
    auto* applyLine = new QPushButton(tr("Apply Selected Line"), this);
    applyLine->setObjectName(QStringLiteral("applySelectedLineButton"));
    auto* deleteLine = new QPushButton(tr("Delete Selected Line"), this);
    lineButtons->addWidget(addLine); lineButtons->addWidget(applyLine); lineButtons->addWidget(deleteLine); lineButtons->addStretch();
    validation_ = new QLabel(this); validation_->setStyleSheet(QStringLiteral("color: #9a3030;")); validation_->hide();
    validation_->setObjectName(QStringLiteral("loadNetworkValidation"));
    layout->addWidget(description); layout->addWidget(loadTypes); layout->addWidget(new QLabel(tr("Loads (LD 0–5)"), this)); layout->addWidget(loads_, 1); layout->addLayout(loadButtons);
    layout->addWidget(new QLabel(tr("Transmission Lines (TL)"), this)); layout->addWidget(lines_, 1); layout->addLayout(lineButtons); layout->addWidget(validation_);

    connect(addLoad, &QPushButton::clicked, this, [this] {
        if (model_.empty()) {
            return;
        }
        const auto& wire = model_.wires().front();
        emit loadChanged({4, wire.tag, 1, 1, 50.0, 0.0, 0.0, 0});
    });
    connect(applyLoad, &QPushButton::clicked, this, [this] { applySelectedLoad(); });
    connect(deleteLoad, &QPushButton::clicked, this, [this] {
        if (loads_->currentRow() >= 0) emit loadDeleteRequested(loads_->item(loads_->currentRow(), 0)->data(SourceLineRole).toULongLong());
    });
    connect(addLine, &QPushButton::clicked, this, [this] {
        if (model_.empty()) {
            return;
        }
        const auto& first = model_.wires().front();
        const auto& second = model_.wires().back();
        emit transmissionLineChanged({first.tag, 1, second.tag, 1, 50.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0});
    });
    connect(applyLine, &QPushButton::clicked, this, [this] { applySelectedLine(); });
    connect(deleteLine, &QPushButton::clicked, this, [this] {
        if (lines_->currentRow() >= 0) emit transmissionLineDeleteRequested(lines_->item(lines_->currentRow(), 0)->data(SourceLineRole).toULongLong());
    });
    connect(loads_, &QTableWidget::currentCellChanged, this,
        [this](int row) { if (const auto line = sourceLineAt(loads_, row)) emit loadSelected(line); });
    connect(lines_, &QTableWidget::currentCellChanged, this,
        [this](int row) { if (const auto line = sourceLineAt(lines_, row)) emit transmissionLineSelected(line); });
}

void LoadNetworkEditor::setData(const model::AntennaModel& model, const model::ModelSetup& setup)
{
    const auto selectedLoad = sourceLineAt(loads_, loads_->currentRow());
    const auto selectedLine = sourceLineAt(lines_, lines_->currentRow());
    model_ = model; loads_->setRowCount(static_cast<int>(setup.loads.size()));
    for (auto row = 0; row < static_cast<int>(setup.loads.size()); ++row) {
        const auto& value = setup.loads[row]; const QStringList cells{QString::number(value.type), QString::number(value.wireTag), QString::number(value.firstSegment), QString::number(value.lastSegment), formatDecimal(value.value1), formatDecimal(value.value2), formatDecimal(value.value3)};
        for (auto column = 0; column < cells.size(); ++column) { auto* item = new QTableWidgetItem(cells[column]); item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine)); loads_->setItem(row, column, item); }
    }
    lines_->setRowCount(static_cast<int>(setup.transmissionLines.size()));
    for (auto row = 0; row < static_cast<int>(setup.transmissionLines.size()); ++row) {
        const auto& value = setup.transmissionLines[row]; const QStringList cells{QString::number(value.wireTag1), QString::number(value.segment1), QString::number(value.wireTag2), QString::number(value.segment2), formatDecimal(value.characteristicImpedance), formatDecimal(value.lengthMeters), formatDecimal(value.shuntReal1), formatDecimal(value.shuntImaginary1), formatDecimal(value.shuntReal2), formatDecimal(value.shuntImaginary2)};
        for (auto column = 0; column < cells.size(); ++column) { auto* item = new QTableWidgetItem(cells[column]); item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine)); lines_->setItem(row, column, item); }
    }
    selectLoad(selectedLoad);
    selectTransmissionLine(selectedLine);
}

void LoadNetworkEditor::selectLoad(std::size_t sourceLine)
{
    const QSignalBlocker blocker(loads_);
    for (auto row = 0; row < loads_->rowCount(); ++row) {
        if (sourceLineAt(loads_, row) == sourceLine) {
            loads_->selectRow(row);
            return;
        }
    }
    loads_->clearSelection();
}

void LoadNetworkEditor::selectTransmissionLine(std::size_t sourceLine)
{
    const QSignalBlocker blocker(lines_);
    for (auto row = 0; row < lines_->rowCount(); ++row) {
        if (sourceLineAt(lines_, row) == sourceLine) {
            lines_->selectRow(row);
            return;
        }
    }
    lines_->clearSelection();
}

void LoadNetworkEditor::applySelectedLoad()
{
    const auto row = loads_->currentRow(); if (row < 0) return; bool valid = true;
    model::LoadDefinition value; value.type = loads_->item(row,0)->text().toInt(&valid); if (!valid) goto invalid;
    value.wireTag = loads_->item(row,1)->text().toInt(&valid); if (!valid) goto invalid;
    value.firstSegment = loads_->item(row,2)->text().toInt(&valid); if (!valid) goto invalid;
    value.lastSegment = loads_->item(row,3)->text().toInt(&valid); if (!valid) goto invalid;
    value.value1 = loads_->item(row,4)->text().toDouble(&valid); if (!valid) goto invalid;
    value.value2 = loads_->item(row,5)->text().toDouble(&valid); if (!valid) goto invalid;
    value.value3 = loads_->item(row,6)->text().toDouble(&valid); if (!valid) goto invalid;
    if (value.type < 0 || value.type > 5) goto invalid_reference;
    if (const auto* wire = model_.wireByTag(value.wireTag); wire == nullptr
        || value.firstSegment < 0 || value.lastSegment < 0
        || value.firstSegment > wire->segments || value.lastSegment > wire->segments
        || (value.firstSegment != 0 && value.lastSegment != 0
            && value.firstSegment > value.lastSegment)) goto invalid_reference;
    value.sourceLine = loads_->item(row,0)->data(SourceLineRole).toULongLong(); validation_->hide(); emit loadChanged(value); return;
invalid: validation_->setText(tr("The selected LD row contains an invalid number.")); validation_->show();
    return;
invalid_reference: validation_->setText(tr("LD type, wire tag, or segment range is invalid.")); validation_->show();
}

void LoadNetworkEditor::applySelectedLine()
{
    const auto row = lines_->currentRow(); if (row < 0) return; bool valid = true; model::TransmissionLineDefinition value;
    value.wireTag1=lines_->item(row,0)->text().toInt(&valid); if(!valid) goto invalid;
    value.segment1=lines_->item(row,1)->text().toInt(&valid); if(!valid) goto invalid;
    value.wireTag2=lines_->item(row,2)->text().toInt(&valid); if(!valid) goto invalid;
    value.segment2=lines_->item(row,3)->text().toInt(&valid); if(!valid) goto invalid;
    value.characteristicImpedance=lines_->item(row,4)->text().toDouble(&valid); if(!valid) goto invalid;
    value.lengthMeters=lines_->item(row,5)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntReal1=lines_->item(row,6)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntImaginary1=lines_->item(row,7)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntReal2=lines_->item(row,8)->text().toDouble(&valid); if(!valid) goto invalid;
    value.shuntImaginary2=lines_->item(row,9)->text().toDouble(&valid); if(!valid) goto invalid;
    if (const auto* first = model_.wireByTag(value.wireTag1); first == nullptr
        || value.segment1 < 1 || value.segment1 > first->segments) goto invalid_reference;
    if (const auto* second = model_.wireByTag(value.wireTag2); second == nullptr
        || value.segment2 < 1 || value.segment2 > second->segments) goto invalid_reference;
    if (value.characteristicImpedance == 0.0) goto invalid_reference;
    value.sourceLine=lines_->item(row,0)->data(SourceLineRole).toULongLong(); validation_->hide(); emit transmissionLineChanged(value); return;
invalid: validation_->setText(tr("The selected TL row contains an invalid number.")); validation_->show();
    return;
invalid_reference: validation_->setText(tr("TL endpoints must reference valid segments and Z0 must be nonzero.")); validation_->show();
}

}
