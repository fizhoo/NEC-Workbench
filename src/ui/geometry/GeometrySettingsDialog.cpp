#include "ui/geometry/GeometrySettingsDialog.h"

#include "ui/DisplayFormat.h"
#include "ui/geometry/EngineeringSpinBox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

auto createLengthControl(QWidget* parent) -> QComboBox*
{
    auto* control = new QComboBox(parent);
    control->addItem(QObject::tr("Meters"), static_cast<int>(model::LengthUnit::Meter));
    control->addItem(QObject::tr("Centimeters"), static_cast<int>(model::LengthUnit::Centimeter));
    control->addItem(QObject::tr("Millimeters"), static_cast<int>(model::LengthUnit::Millimeter));
    control->addItem(QObject::tr("Inches"), static_cast<int>(model::LengthUnit::Inch));
    control->addItem(QObject::tr("Feet"), static_cast<int>(model::LengthUnit::Foot));
    return control;
}

auto createLengthSpinBox(QWidget* parent) -> QDoubleSpinBox*
{
    auto* control = new EngineeringSpinBox(parent);
    control->setDecimals(DisplayDecimalPlaces);
    control->setRange(0.000001, 1.0e9);
    return control;
}

}

GeometrySettingsDialog::GeometrySettingsDialog(const GeometrySettings& settings, QWidget* parent)
    : QDialog(parent)
    , lengthUnit_(settings.lengthUnit)
    , snapUnitBehavior_(settings.snapUnitBehavior)
{
    setWindowTitle(tr("Geometry Settings"));
    setMinimumWidth(430);
    auto* layout = new QVBoxLayout(this);

    auto* displayGroup = new QGroupBox(tr("Display"), this);
    auto* displayLayout = new QFormLayout(displayGroup);
    lengthUnitControl_ = createLengthControl(displayGroup);
    lengthUnitControl_->setCurrentIndex(lengthUnitControl_->findData(static_cast<int>(lengthUnit_)));
    showGridControl_ = new QCheckBox(tr("Show grid"), displayGroup);
    showGridControl_->setChecked(settings.showGrid);
    showAxesControl_ = new QCheckBox(tr("Show axes"), displayGroup);
    showAxesControl_->setChecked(settings.showAxes);
    showLabelsControl_ = new QCheckBox(tr("Show coordinate labels"), displayGroup);
    showLabelsControl_->setChecked(settings.showLabels);
    displayLayout->addRow(tr("Display units"), lengthUnitControl_);
    displayLayout->addRow({}, showGridControl_);
    displayLayout->addRow({}, showAxesControl_);
    displayLayout->addRow({}, showLabelsControl_);

    auto* gridGroup = new QGroupBox(tr("Grid Layout"), this);
    auto* gridLayout = new QFormLayout(gridGroup);
    automaticGridControl_ = new QCheckBox(tr("Choose clean major spacing automatically"), gridGroup);
    automaticGridControl_->setChecked(settings.automaticGridSpacing);
    majorGridControl_ = createLengthSpinBox(gridGroup);
    majorGridControl_->setValue(model::fromMeters(settings.manualGridSpacingMeters, lengthUnit_));
    majorGridControl_->setEnabled(!settings.automaticGridSpacing);
    minorDivisionsControl_ = new QSpinBox(gridGroup);
    minorDivisionsControl_->setRange(1, 10);
    minorDivisionsControl_->setValue(settings.minorGridDivisions);
    gridLayout->addRow({}, automaticGridControl_);
    gridLayout->addRow(tr("Major spacing"), majorGridControl_);
    gridLayout->addRow(tr("Minor divisions"), minorDivisionsControl_);

    auto* snappingGroup = new QGroupBox(tr("Snapping"), this);
    auto* snappingLayout = new QFormLayout(snappingGroup);
    gridSnappingControl_ = new QCheckBox(tr("Snap to grid"), snappingGroup);
    gridSnappingControl_->setChecked(settings.gridSnapping);
    snapUnitBehaviorControl_ = new QComboBox(snappingGroup);
    snapUnitBehaviorControl_->addItem(tr("Unit-friendly intervals"),
        static_cast<int>(SnapUnitBehavior::UnitFriendly));
    snapUnitBehaviorControl_->addItem(tr("Preserve physical interval"),
        static_cast<int>(SnapUnitBehavior::PreservePhysical));
    snapUnitBehaviorControl_->setCurrentIndex(
        snapUnitBehaviorControl_->findData(static_cast<int>(snapUnitBehavior_)));
    snapSpacingControl_ = createLengthSpinBox(snappingGroup);
    snapSpacingControl_->setKeyboardTracking(false);
    snapSpacingControl_->setValue(model::fromMeters(settings.snapSpacingMeters, lengthUnit_));
    endpointSnappingControl_ = new QCheckBox(tr("Snap to nearby endpoints"), snappingGroup);
    endpointSnappingControl_->setChecked(settings.endpointSnapping);
    endpointToleranceControl_ = new QDoubleSpinBox(snappingGroup);
    endpointToleranceControl_->setRange(1.0, 50.0);
    endpointToleranceControl_->setValue(settings.endpointTolerancePixels);
    endpointToleranceControl_->setSuffix(tr(" px"));
    snappingLayout->addRow({}, gridSnappingControl_);
    snappingLayout->addRow(tr("When units change"), snapUnitBehaviorControl_);
    snappingLayout->addRow(tr("Snap interval"), snapSpacingControl_);
    snappingLayout->addRow({}, endpointSnappingControl_);
    snappingLayout->addRow(tr("Endpoint tolerance"), endpointToleranceControl_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(automaticGridControl_, &QCheckBox::toggled, majorGridControl_, &QWidget::setDisabled);
    connect(lengthUnitControl_, &QComboBox::currentIndexChanged, this, [this] {
        changeLengthUnit(static_cast<model::LengthUnit>(lengthUnitControl_->currentData().toInt()));
    });
    connect(snapUnitBehaviorControl_, &QComboBox::currentIndexChanged, this, [this] {
        snapUnitBehavior_ = static_cast<SnapUnitBehavior>(snapUnitBehaviorControl_->currentData().toInt());
        if (snapUnitBehavior_ == SnapUnitBehavior::UnitFriendly) {
            snapSpacingControl_->setValue(model::niceEngineeringStep(snapSpacingControl_->value()));
        }
    });
    connect(snapSpacingControl_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (snapUnitBehavior_ == SnapUnitBehavior::UnitFriendly) {
            snapSpacingControl_->setValue(model::niceEngineeringStep(snapSpacingControl_->value()));
        }
    });

    layout->addWidget(displayGroup);
    layout->addWidget(gridGroup);
    layout->addWidget(snappingGroup);
    layout->addWidget(buttons);
    updateUnitControls();
}

auto GeometrySettingsDialog::settings() const -> GeometrySettings
{
    auto result = GeometrySettings{};
    result.lengthUnit = lengthUnit_;
    result.automaticGridSpacing = automaticGridControl_->isChecked();
    result.manualGridSpacingMeters = model::toMeters(majorGridControl_->value(), lengthUnit_);
    result.minorGridDivisions = minorDivisionsControl_->value();
    result.showGrid = showGridControl_->isChecked();
    result.showAxes = showAxesControl_->isChecked();
    result.showLabels = showLabelsControl_->isChecked();
    result.gridSnapping = gridSnappingControl_->isChecked();
    const auto snapSpacing = snapUnitBehavior_ == SnapUnitBehavior::UnitFriendly
        ? model::niceEngineeringStep(snapSpacingControl_->value())
        : snapSpacingControl_->value();
    result.snapSpacingMeters = model::toMeters(snapSpacing, lengthUnit_);
    result.snapUnitBehavior = snapUnitBehavior_;
    result.endpointSnapping = endpointSnappingControl_->isChecked();
    result.endpointTolerancePixels = endpointToleranceControl_->value();
    return result;
}

void GeometrySettingsDialog::changeLengthUnit(model::LengthUnit unit)
{
    const auto majorMeters = model::toMeters(majorGridControl_->value(), lengthUnit_);
    const auto snapMeters = model::toMeters(snapSpacingControl_->value(), lengthUnit_);
    lengthUnit_ = unit;
    {
        const QSignalBlocker majorBlocker(majorGridControl_);
        const QSignalBlocker snapBlocker(snapSpacingControl_);
        majorGridControl_->setValue(model::fromMeters(majorMeters, lengthUnit_));
        const auto convertedSnap = model::fromMeters(snapMeters, lengthUnit_);
        snapSpacingControl_->setValue(snapUnitBehavior_ == SnapUnitBehavior::UnitFriendly
                ? model::niceEngineeringStep(convertedSnap)
                : convertedSnap);
    }
    updateUnitControls();
}

void GeometrySettingsDialog::updateUnitControls()
{
    const auto symbol = model::lengthUnitSymbol(lengthUnit_);
    const auto suffix = QStringLiteral(" %1")
        .arg(QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
    majorGridControl_->setSuffix(suffix);
    snapSpacingControl_->setSuffix(suffix);
}

}
