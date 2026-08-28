#include "ui/geometry/WirePropertiesDialog.h"

#include "model/WireGauge.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace necwb::ui {
namespace {

auto createLengthControl(QWidget* parent) -> QDoubleSpinBox*
{
    auto* control = new QDoubleSpinBox(parent);
    control->setDecimals(9);
    control->setRange(-1.0e12, 1.0e12);
    control->setKeyboardTracking(false);
    return control;
}

auto unitSuffix(model::LengthUnit unit) -> QString
{
    const auto symbol = model::lengthUnitSymbol(unit);
    return QStringLiteral(" %1").arg(
        QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

}

WirePropertiesDialog::WirePropertiesDialog(const model::Wire& wire,
    const model::AntennaModel& model, model::LengthUnit lengthUnit, QWidget* parent)
    : QDialog(parent)
    , original_(wire)
    , model_(model)
    , lengthUnit_(lengthUnit)
{
    setWindowTitle(tr("Wire %1 Properties").arg(wire.tag));
    setMinimumWidth(460);
    auto* layout = new QVBoxLayout(this);

    auto* identityGroup = new QGroupBox(tr("NEC Wire"), this);
    auto* identityLayout = new QFormLayout(identityGroup);
    tagControl_ = new QSpinBox(identityGroup);
    tagControl_->setRange(1, std::numeric_limits<int>::max());
    tagControl_->setValue(wire.tag);
    segmentsControl_ = new QSpinBox(identityGroup);
    segmentsControl_->setRange(1, std::numeric_limits<int>::max());
    segmentsControl_->setValue(wire.segments);
    identityLayout->addRow(tr("Tag"), tagControl_);
    identityLayout->addRow(tr("Segments"), segmentsControl_);

    auto* geometryGroup = new QGroupBox(tr("Endpoints"), this);
    auto* geometryLayout = new QFormLayout(geometryGroup);
    const auto suffix = unitSuffix(lengthUnit_);
    const std::array<double, 6> coordinates{wire.start.x, wire.start.y, wire.start.z,
        wire.end.x, wire.end.y, wire.end.z};
    const std::array<QString, 6> coordinateNames{tr("Start X"), tr("Start Y"), tr("Start Z"),
        tr("End X"), tr("End Y"), tr("End Z")};
    for (auto index = std::size_t{0}; index < coordinateControls_.size(); ++index) {
        coordinateControls_[index] = createLengthControl(geometryGroup);
        coordinateControls_[index]->setSuffix(suffix);
        coordinateControls_[index]->setValue(model::fromMeters(coordinates[index], lengthUnit_));
        geometryLayout->addRow(coordinateNames[index], coordinateControls_[index]);
    }

    auto* sizeGroup = new QGroupBox(tr("Wire Size"), this);
    auto* sizeLayout = new QFormLayout(sizeGroup);
    gaugeControl_ = new QComboBox(sizeGroup);
    gaugeControl_->addItem(tr("Custom radius"));
    for (auto gauge = -3; gauge <= 40; ++gauge) {
        gaugeControl_->addItem(QString::fromStdString(model::awgLabel(gauge)), gauge);
    }
    radiusControl_ = createLengthControl(sizeGroup);
    radiusControl_->setRange(0.000000001, 1.0e12);
    radiusControl_->setSuffix(suffix);
    radiusControl_->setValue(model::fromMeters(wire.radius, lengthUnit_));
    diameterLabel_ = new QLabel(sizeGroup);
    auto* gaugeNote = new QLabel(tr("AWG choices use nominal bare-conductor diameter."), sizeGroup);
    gaugeNote->setWordWrap(true);
    sizeLayout->addRow(tr("Standard gauge"), gaugeControl_);
    sizeLayout->addRow(tr("Radius"), radiusControl_);
    sizeLayout->addRow(tr("Calculated diameter"), diameterLabel_);
    sizeLayout->addRow({}, gaugeNote);

    validationLabel_ = new QLabel(this);
    validationLabel_->setWordWrap(true);
    validationLabel_->setStyleSheet(QStringLiteral("color: #b03030;"));
    validationLabel_->hide();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &WirePropertiesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(gaugeControl_, &QComboBox::currentIndexChanged, this, [this](int index) { selectGauge(index); });
    connect(radiusControl_, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!updatingRadius_ && gaugeControl_->currentData().isValid()) {
            gaugeControl_->setCurrentIndex(0);
        }
        updateDiameterLabel();
    });

    layout->addWidget(identityGroup);
    layout->addWidget(geometryGroup);
    layout->addWidget(sizeGroup);
    layout->addWidget(validationLabel_);
    layout->addWidget(buttons);
    updateDiameterLabel();
}

auto WirePropertiesDialog::wire() const -> model::Wire
{
    const model::Point3D start{
        model::toMeters(coordinateControls_[0]->value(), lengthUnit_),
        model::toMeters(coordinateControls_[1]->value(), lengthUnit_),
        model::toMeters(coordinateControls_[2]->value(), lengthUnit_)};
    const model::Point3D end{
        model::toMeters(coordinateControls_[3]->value(), lengthUnit_),
        model::toMeters(coordinateControls_[4]->value(), lengthUnit_),
        model::toMeters(coordinateControls_[5]->value(), lengthUnit_)};
    return {tagControl_->value(), start, end, segmentsControl_->value(),
        model::toMeters(radiusControl_->value(), lengthUnit_), original_.sourceLine};
}

void WirePropertiesDialog::accept()
{
    const auto updated = wire();
    if (updated.tag != original_.tag && model_.wireByTag(updated.tag) != nullptr) {
        validationLabel_->setText(tr("Wire tag %1 is already in use.").arg(updated.tag));
        validationLabel_->show();
        return;
    }
    if (updated.start == updated.end) {
        validationLabel_->setText(tr("Start and end points must be different."));
        validationLabel_->show();
        return;
    }
    if (!std::isfinite(updated.radius) || updated.radius <= 0.0) {
        validationLabel_->setText(tr("Wire radius must be a positive finite value."));
        validationLabel_->show();
        return;
    }
    QDialog::accept();
}

void WirePropertiesDialog::selectGauge(int index)
{
    const auto data = gaugeControl_->itemData(index);
    if (!data.isValid()) {
        return;
    }
    updatingRadius_ = true;
    radiusControl_->setValue(model::fromMeters(model::awgRadiusMeters(data.toInt()), lengthUnit_));
    updatingRadius_ = false;
    updateDiameterLabel();
}

void WirePropertiesDialog::updateDiameterLabel()
{
    diameterLabel_->setText(QStringLiteral("%1%2")
        .arg(QString::number(radiusControl_->value() * 2.0, 'g', 9), unitSuffix(lengthUnit_)));
}

}
