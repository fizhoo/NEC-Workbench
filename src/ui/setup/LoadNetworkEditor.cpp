#include "ui/setup/LoadNetworkEditor.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace necwb::ui {
namespace { constexpr auto SourceLineRole = Qt::UserRole; }

LoadNetworkEditor::LoadNetworkEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr("Edit lumped/distributed loads and transmission-line connections. Double-click a cell, then apply the selected row."), this);
    description->setWordWrap(true);
    auto* loadTypes = new QLabel(tr("LD types: 0 series RLC, 1 parallel RLC, 2 distributed series RLC, 3 distributed parallel RLC, 4 fixed complex impedance, 5 wire conductivity."), this);
    loadTypes->setWordWrap(true);
    loads_ = new QTableWidget(0, 7, this);
    loads_->setHorizontalHeaderLabels({tr("Type"), tr("Wire"), tr("First seg"), tr("Last seg"), tr("R / Value 1"), tr("L / X / Value 2"), tr("C / Value 3")});
    loads_->horizontalHeader()->setStretchLastSection(true);
    auto* loadButtons = new QHBoxLayout;
    auto* addLoad = new QPushButton(tr("Add Load"), this);
    auto* applyLoad = new QPushButton(tr("Apply Selected Load"), this);
    auto* deleteLoad = new QPushButton(tr("Delete Selected Load"), this);
    loadButtons->addWidget(addLoad); loadButtons->addWidget(applyLoad); loadButtons->addWidget(deleteLoad); loadButtons->addStretch();

    lines_ = new QTableWidget(0, 10, this);
    lines_->setHorizontalHeaderLabels({tr("Wire 1"), tr("Seg 1"), tr("Wire 2"), tr("Seg 2"), tr("Z0 (Ω)"), tr("Length (m)"), tr("Y1 real"), tr("Y1 imag"), tr("Y2 real"), tr("Y2 imag")});
    lines_->horizontalHeader()->setStretchLastSection(true);
    auto* lineButtons = new QHBoxLayout;
    auto* addLine = new QPushButton(tr("Add Transmission Line"), this);
    auto* applyLine = new QPushButton(tr("Apply Selected Line"), this);
    auto* deleteLine = new QPushButton(tr("Delete Selected Line"), this);
    lineButtons->addWidget(addLine); lineButtons->addWidget(applyLine); lineButtons->addWidget(deleteLine); lineButtons->addStretch();
    validation_ = new QLabel(this); validation_->setStyleSheet(QStringLiteral("color: #9a3030;")); validation_->hide();
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
}

void LoadNetworkEditor::setData(const model::AntennaModel& model, const model::ModelSetup& setup)
{
    model_ = model; loads_->setRowCount(static_cast<int>(setup.loads.size()));
    for (auto row = 0; row < static_cast<int>(setup.loads.size()); ++row) {
        const auto& value = setup.loads[row]; const QStringList cells{QString::number(value.type), QString::number(value.wireTag), QString::number(value.firstSegment), QString::number(value.lastSegment), QString::number(value.value1, 'g', 12), QString::number(value.value2, 'g', 12), QString::number(value.value3, 'g', 12)};
        for (auto column = 0; column < cells.size(); ++column) { auto* item = new QTableWidgetItem(cells[column]); item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine)); loads_->setItem(row, column, item); }
    }
    lines_->setRowCount(static_cast<int>(setup.transmissionLines.size()));
    for (auto row = 0; row < static_cast<int>(setup.transmissionLines.size()); ++row) {
        const auto& value = setup.transmissionLines[row]; const QStringList cells{QString::number(value.wireTag1), QString::number(value.segment1), QString::number(value.wireTag2), QString::number(value.segment2), QString::number(value.characteristicImpedance, 'g', 12), QString::number(value.lengthMeters, 'g', 12), QString::number(value.shuntReal1, 'g', 12), QString::number(value.shuntImaginary1, 'g', 12), QString::number(value.shuntReal2, 'g', 12), QString::number(value.shuntImaginary2, 'g', 12)};
        for (auto column = 0; column < cells.size(); ++column) { auto* item = new QTableWidgetItem(cells[column]); item->setData(SourceLineRole, static_cast<qulonglong>(value.sourceLine)); lines_->setItem(row, column, item); }
    }
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
    value.sourceLine = loads_->item(row,0)->data(SourceLineRole).toULongLong(); validation_->hide(); emit loadChanged(value); return;
invalid: validation_->setText(tr("The selected LD row contains an invalid number.")); validation_->show();
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
    value.sourceLine=lines_->item(row,0)->data(SourceLineRole).toULongLong(); validation_->hide(); emit transmissionLineChanged(value); return;
invalid: validation_->setText(tr("The selected TL row contains an invalid number.")); validation_->show();
}

}
