#include "ui/setup/ExcitationPropertiesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace necwb::ui {

ExcitationPropertiesDialog::ExcitationPropertiesDialog(const model::Excitation& excitation,
    const model::AntennaModel& model, QWidget* parent)
    : QDialog(parent)
    , original_(excitation)
    , model_(model)
{
    setWindowTitle(tr("Voltage Source Properties"));
    setMinimumWidth(410);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    wireControl_ = new QComboBox(this);
    for (const auto& wire : model_.wires()) {
        wireControl_->addItem(tr("Wire %1").arg(wire.tag), wire.tag);
    }
    wireControl_->setCurrentIndex(wireControl_->findData(excitation.wireTag));
    segmentControl_ = new QSpinBox(this);
    segmentControl_->setRange(1, 1);
    const auto* initialWire = model_.wireByTag(excitation.wireTag);
    segmentControl_->setMaximum(initialWire == nullptr ? 1 : initialWire->segments);
    segmentControl_->setValue(excitation.segment);
    magnitudeControl_ = new QDoubleSpinBox(this);
    magnitudeControl_->setDecimals(9);
    magnitudeControl_->setRange(0.0, 1.0e12);
    magnitudeControl_->setValue(excitation.magnitude);
    magnitudeControl_->setKeyboardTracking(false);
    phaseControl_ = new QDoubleSpinBox(this);
    phaseControl_->setDecimals(6);
    phaseControl_->setRange(-360.0, 360.0);
    phaseControl_->setSuffix(tr("°"));
    phaseControl_->setValue(excitation.phaseDegrees);
    phaseControl_->setKeyboardTracking(false);
    form->addRow(tr("Source type"), new QLabel(tr("Applied-field voltage source (EX 0)"), this));
    form->addRow(tr("Wire"), wireControl_);
    form->addRow(tr("Segment"), segmentControl_);
    form->addRow(tr("Magnitude"), magnitudeControl_);
    form->addRow(tr("Phase"), phaseControl_);

    validationLabel_ = new QLabel(this);
    validationLabel_->setWordWrap(true);
    validationLabel_->setStyleSheet(QStringLiteral("color: #b03030;"));
    validationLabel_->hide();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ExcitationPropertiesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(wireControl_, &QComboBox::currentIndexChanged, this, [this] {
        const auto* wire = model_.wireByTag(wireControl_->currentData().toInt());
        segmentControl_->setMaximum(wire == nullptr ? 1 : wire->segments);
    });

    layout->addLayout(form);
    layout->addWidget(validationLabel_);
    layout->addWidget(buttons);
}

auto ExcitationPropertiesDialog::excitation() const -> model::Excitation
{
    return {0, wireControl_->currentData().toInt(), segmentControl_->value(),
        magnitudeControl_->value(), phaseControl_->value(), original_.sourceLine};
}

void ExcitationPropertiesDialog::accept()
{
    const auto updated = excitation();
    const auto* wire = model_.wireByTag(updated.wireTag);
    if (wire == nullptr || updated.segment < 1 || updated.segment > wire->segments) {
        validationLabel_->setText(tr("Choose a valid wire segment."));
        validationLabel_->show();
        return;
    }
    if (!std::isfinite(updated.magnitude) || updated.magnitude < 0.0
        || !std::isfinite(updated.phaseDegrees)) {
        validationLabel_->setText(tr("Magnitude and phase must be finite values."));
        validationLabel_->show();
        return;
    }
    QDialog::accept();
}

}
