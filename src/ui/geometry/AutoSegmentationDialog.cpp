#include "ui/geometry/AutoSegmentationDialog.h"

#include "nec/SourcePositions.h"
#include "ui/DisplayFormat.h"

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

AutoSegmentationDialog::AutoSegmentationDialog(std::string source,
    const model::AntennaModel& model, const model::ModelSetup& setup,
    double maximumFrequencyMHz, QWidget* parent)
    : QDialog(parent)
    , source_(std::move(source))
    , model_(model)
    , setup_(setup)
    , maximumFrequencyMHz_(maximumFrequencyMHz)
{
    setWindowTitle(tr("Automatic Segmentation"));
    resize(760, 520);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr(
        "Propose wire segment counts from the highest analysis frequency. Review the changes "
        "before applying them as one undoable model edit. Fields written with a variable or an "
        "expression are never overwritten; a wire that would need one changed is skipped."), this);
    description->setWordWrap(true);
    auto* form = new QFormLayout;
    auto* frequency = new QLabel(
        tr("%1 MHz").arg(formatDecimal(maximumFrequencyMHz_)), this);
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
    summary_->setObjectName(QStringLiteral("segmentationSummary"));
    summary_->setWordWrap(true);
    table_ = new QTableWidget(0, 6, this);
    table_->setObjectName(QStringLiteral("segmentationTable"));
    table_->setHorizontalHeaderLabels({tr("Wire"), tr("Length (m)"), tr("Current"),
        tr("Proposed"), tr("Segment length (m)"), tr("Changes")});
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

auto AutoSegmentationDialog::edit() const -> const nec::SegmentationEdit&
{
    return edit_;
}

void AutoSegmentationDialog::refreshProposal()
{
    edit_ = nec::planSegmentationEdit(source_, model_, setup_, maximumFrequencyMHz_,
        {segmentsPerWavelengthControl_->value(), oddExcitedWiresControl_->isChecked()});
    const auto& proposal = edit_.proposal;
    table_->setRowCount(static_cast<int>(proposal.wires.size()));
    auto changedCount = 0;
    for (auto row = 0; row < static_cast<int>(proposal.wires.size()); ++row) {
        const auto& wire = proposal.wires[row];
        const auto* skipped = edit_.skippedReason(wire.wireTag);
        changedCount += wire.oldSegments != wire.newSegments && skipped == nullptr ? 1 : 0;
        QStringList remaps;
        for (std::size_t index = 0; index < setup_.excitations.size(); ++index) {
            const auto& before = setup_.excitations[index];
            const auto& after = proposal.remappedExcitations[index];
            if (before.wireTag != wire.wireTag || before.segment == after.segment) continue;
            if (const auto& mark = edit_.excitationMarks[index])
                remaps.append(tr("EX at %1%: %2 → %3")
                    .arg(QString::fromStdString(nec::sourcePositionText(*mark)))
                    .arg(before.segment).arg(after.segment));
            else remaps.append(tr("EX %1 → %2").arg(before.segment).arg(after.segment));
        }
        for (std::size_t index = 0; index < setup_.loads.size(); ++index) {
            if (setup_.loads[index].wireTag != wire.wireTag) continue;
            const auto& before = setup_.loads[index];
            const auto& after = proposal.remappedLoads[index];
            if (before.firstSegment != after.firstSegment
                || before.lastSegment != after.lastSegment) {
                remaps.append(tr("LD %1–%2 → %3–%4").arg(before.firstSegment)
                    .arg(before.lastSegment).arg(after.firstSegment).arg(after.lastSegment));
            }
        }
        const auto ports = [&remaps, &wire](const QString& card, const auto& before, const auto& after) {
            if (before.wireTag1 == wire.wireTag && before.segment1 != after.segment1)
                remaps.append(tr("%1 %2 → %3").arg(card).arg(before.segment1).arg(after.segment1));
            if (before.wireTag2 == wire.wireTag && before.segment2 != after.segment2)
                remaps.append(tr("%1 %2 → %3").arg(card).arg(before.segment2).arg(after.segment2));
        };
        for (std::size_t index = 0; index < setup_.transmissionLines.size(); ++index)
            ports(QStringLiteral("TL"), setup_.transmissionLines[index],
                proposal.remappedTransmissionLines[index]);
        for (std::size_t index = 0; index < setup_.networks.size(); ++index)
            ports(QStringLiteral("NT"), setup_.networks[index], proposal.remappedNetworks[index]);
        const auto changes = skipped != nullptr
            ? tr("Skipped: %1").arg(QString::fromStdString(*skipped))
            : remaps.isEmpty() ? tr("—") : remaps.join(QStringLiteral(", "));
        const QStringList values{
            QString::number(wire.wireTag),
            formatDecimal(wire.wireLengthMeters),
            QString::number(wire.oldSegments),
            skipped != nullptr ? tr("%1 (skipped)").arg(wire.newSegments)
                               : QString::number(wire.newSegments),
            formatDecimal(wire.segmentLengthMeters),
            changes,
        };
        for (auto column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            if (skipped != nullptr) item->setToolTip(changes);
            table_->setItem(row, column, item);
        }
    }
    auto summary = tr("%1 of %2 wire(s) will change. Sources, loads, transmission-line endpoints, "
        "and network ports are remapped by their relative wire position; a source placed along "
        "its wire follows its position.").arg(changedCount).arg(proposal.wires.size());
    if (!edit_.skipped.empty())
        summary += tr(" %1 wire(s) are skipped because a segment field they need changed is "
            "written with a variable or an expression; change the variable under Variables "
            "instead.").arg(edit_.skipped.size());
    if (!edit_.error.empty())
        summary += QLatin1Char(' ') + QString::fromStdString(edit_.error) + QLatin1Char('.');
    summary_->setText(summary);
    table_->resizeColumnsToContents();
}

}
