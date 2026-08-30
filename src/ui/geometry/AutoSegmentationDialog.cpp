#include "ui/geometry/AutoSegmentationDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <ranges>

namespace necwb::ui {

AutoSegmentationDialog::AutoSegmentationDialog(const model::AntennaModel& model,
    const std::vector<model::Excitation>& excitations, double maximumFrequencyMHz,
    QWidget* parent)
    : QDialog(parent)
    , model_(model)
    , excitations_(excitations)
    , maximumFrequencyMHz_(maximumFrequencyMHz)
{
    setWindowTitle(tr("Automatic Segmentation"));
    resize(760, 520);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr(
        "Propose wire segment counts from the highest analysis frequency. Review the changes before applying them as one undoable model edit."), this);
    description->setWordWrap(true);
    auto* form = new QFormLayout;
    auto* frequency = new QLabel(tr("%1 MHz").arg(maximumFrequencyMHz_, 0, 'g', 12), this);
    segmentsPerWavelengthControl_ = new QSpinBox(this);
    segmentsPerWavelengthControl_->setRange(5, 100);
    segmentsPerWavelengthControl_->setValue(20);
    oddExcitedWiresControl_ = new QCheckBox(
        tr("Use odd counts on excited wires to preserve a center segment"), this);
    oddExcitedWiresControl_->setChecked(true);
    form->addRow(tr("Highest frequency"), frequency);
    form->addRow(tr("Segments per wavelength"), segmentsPerWavelengthControl_);
    form->addRow({}, oddExcitedWiresControl_);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels({tr("Wire"), tr("Length (m)"), tr("Current"),
        tr("Proposed"), tr("Segment length (m)"), tr("Source remap")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    auto* applyButton = buttons->button(QDialogButtonBox::Apply);
    applyButton->setObjectName(QStringLiteral("applySegmentationButton"));
    applyButton->setText(tr("Apply Segmentation"));
    connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(segmentsPerWavelengthControl_, &QSpinBox::valueChanged, this,
        [this] { refreshProposal(); });
    connect(oddExcitedWiresControl_, &QCheckBox::toggled, this,
        [this] { refreshProposal(); });
    layout->addWidget(description);
    layout->addLayout(form);
    layout->addWidget(summary_);
    layout->addWidget(table_, 1);
    layout->addWidget(buttons);
    refreshProposal();
}

auto AutoSegmentationDialog::proposal() const -> model::SegmentationProposal
{
    return proposal_;
}

void AutoSegmentationDialog::refreshProposal()
{
    proposal_ = model::proposeSegmentation(model_, excitations_, maximumFrequencyMHz_,
        {segmentsPerWavelengthControl_->value(), oddExcitedWiresControl_->isChecked()});
    table_->setRowCount(static_cast<int>(proposal_.wires.size()));
    auto changedCount = 0;
    for (auto row = 0; row < static_cast<int>(proposal_.wires.size()); ++row) {
        const auto& wire = proposal_.wires[row];
        changedCount += wire.oldSegments != wire.newSegments ? 1 : 0;
        QStringList remaps;
        for (std::size_t index = 0; index < excitations_.size(); ++index) {
            if (excitations_[index].wireTag == wire.wireTag
                && excitations_[index].segment != proposal_.remappedExcitations[index].segment) {
                remaps.append(tr("%1 → %2").arg(excitations_[index].segment)
                    .arg(proposal_.remappedExcitations[index].segment));
            }
        }
        const QStringList values{
            QString::number(wire.wireTag),
            QString::number(wire.wireLengthMeters, 'g', 8),
            QString::number(wire.oldSegments),
            QString::number(wire.newSegments),
            QString::number(wire.segmentLengthMeters, 'g', 8),
            remaps.isEmpty() ? tr("—") : remaps.join(QStringLiteral(", ")),
        };
        for (auto column = 0; column < values.size(); ++column) {
            table_->setItem(row, column, new QTableWidgetItem(values[column]));
        }
    }
    summary_->setText(tr("%1 of %2 wire(s) will change. Existing voltage sources are remapped by their relative position along each wire.")
        .arg(changedCount).arg(proposal_.wires.size()));
    table_->resizeColumnsToContents();
}

}
