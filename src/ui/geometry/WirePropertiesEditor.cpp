#include "ui/geometry/WirePropertiesEditor.h"

#include "model/WireGauge.h"
#include "ui/DisplayFormat.h"
#include "ui/PendingEditIndicator.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace necwb::ui {
namespace {

auto createLengthControl(QWidget* parent) -> QDoubleSpinBox*
{
    auto* control = new QDoubleSpinBox(parent);
    control->setDecimals(DisplayDecimalPlaces);
    control->setRange(-1.0e12, 1.0e12);
    control->setKeyboardTracking(false);
    // The huge range would size the box for twelve digits; the pane sets the width.
    control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    control->setMinimumWidth(56);
    return control;
}

auto unitSuffix(model::LengthUnit unit) -> QString
{
    const auto symbol = model::lengthUnitSymbol(unit);
    return QStringLiteral(" %1").arg(
        QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())));
}

}

WirePropertiesEditor::WirePropertiesEditor(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("wirePropertiesEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    title_ = new QLabel(this);
    title_->setWordWrap(true);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
    tagControl_ = new QSpinBox(this);
    tagControl_->setObjectName(QStringLiteral("wireTagEditor"));
    tagControl_->setRange(1, std::numeric_limits<int>::max());
    segmentsControl_ = new QSpinBox(this);
    segmentsControl_->setObjectName(QStringLiteral("wireSegmentsEditor"));
    segmentsControl_->setRange(1, std::numeric_limits<int>::max());
    grid->addWidget(new QLabel(tr("Tag"), this), 0, 0);
    grid->addWidget(tagControl_, 0, 1);
    grid->addWidget(new QLabel(tr("Segments"), this), 0, 2, Qt::AlignRight);
    grid->addWidget(segmentsControl_, 0, 3);
    // One row per endpoint, X Y Z across.
    const std::array axes{tr("X"), tr("Y"), tr("Z")};
    for (auto axis = 0; axis < 3; ++axis) {
        auto* label = new QLabel(axes[static_cast<std::size_t>(axis)], this);
        label->setAlignment(Qt::AlignCenter);
        grid->addWidget(label, 1, axis + 1);
    }
    const std::array endpoints{tr("Start"), tr("End")};
    for (auto row = 0; row < 2; ++row) {
        grid->addWidget(new QLabel(endpoints[static_cast<std::size_t>(row)], this), row + 2, 0);
        for (auto axis = 0; axis < 3; ++axis) {
            auto* control = createLengthControl(this);
            control->setObjectName(QStringLiteral("wireCoordinateEditor%1").arg(row * 3 + axis));
            coordinateControls_[static_cast<std::size_t>(row * 3 + axis)] = control;
            grid->addWidget(control, row + 2, axis + 1);
        }
    }
    gaugeControl_ = new QComboBox(this);
    gaugeControl_->setObjectName(QStringLiteral("wireGaugeEditor"));
    gaugeControl_->setToolTip(tr("AWG choices use nominal bare-conductor diameter."));
    gaugeControl_->addItem(tr("Custom radius"));
    for (auto gauge = -3; gauge <= 40; ++gauge)
        gaugeControl_->addItem(QString::fromStdString(model::awgLabel(gauge)), gauge);
    radiusControl_ = createLengthControl(this);
    radiusControl_->setObjectName(QStringLiteral("wireRadiusEditor"));
    radiusControl_->setRange(0.000000001, 1.0e12);
    diameterLabel_ = new QLabel(this);
    diameterLabel_->setObjectName(QStringLiteral("wireDiameterLabel"));
    grid->addWidget(new QLabel(tr("Gauge"), this), 4, 0);
    grid->addWidget(gaugeControl_, 4, 1);
    grid->addWidget(new QLabel(tr("Radius"), this), 4, 2, Qt::AlignRight);
    grid->addWidget(radiusControl_, 4, 3);
    grid->addWidget(diameterLabel_, 5, 1, 1, 3);
    for (const auto column : {1, 2, 3}) grid->setColumnStretch(column, 1);

    validationLabel_ = new QLabel(this);
    validationLabel_->setObjectName(QStringLiteral("wireValidationLabel"));
    validationLabel_->setWordWrap(true);
    validationLabel_->setStyleSheet(QStringLiteral("color: #b03030;"));
    validationLabel_->hide();
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    applyButton_ = new QPushButton(tr("Apply"), this);
    applyButton_->setObjectName(QStringLiteral("applyWireButton"));
    applyButton_->setToolTip(tr("Apply the wire changes (Enter)"));
    revertButton_ = new QPushButton(tr("Revert"), this);
    revertButton_->setObjectName(QStringLiteral("revertWireButton"));
    revertButton_->setToolTip(tr("Restore the wire as it is in the model (Esc)"));
    buttons->addStretch();
    buttons->addWidget(revertButton_);
    buttons->addWidget(applyButton_);

    layout->addWidget(title_);
    layout->addLayout(grid);
    layout->addWidget(validationLabel_);
    layout->addLayout(buttons);
    layout->addStretch();

    const auto markPending = [this] { if (!updating_) setPending(true); };
    connect(tagControl_, &QSpinBox::valueChanged, this, markPending);
    connect(segmentsControl_, &QSpinBox::valueChanged, this, markPending);
    for (auto* control : coordinateControls_)
        connect(control, &QDoubleSpinBox::valueChanged, this, markPending);
    connect(gaugeControl_, &QComboBox::currentIndexChanged, this, [this](int index) {
        selectGauge(index);
    });
    connect(radiusControl_, &QDoubleSpinBox::valueChanged, this, [this, markPending] {
        // A radius typed by hand no longer matches the chosen gauge.
        if (!updating_ && !selectingGauge_ && gaugeControl_->currentData().isValid()) {
            const QSignalBlocker blocker(gaugeControl_);
            gaugeControl_->setCurrentIndex(0);
        }
        updateDiameterLabel();
        markPending();
    });
    connect(applyButton_, &QPushButton::clicked, this, [this] { apply(); });
    connect(revertButton_, &QPushButton::clicked, this, [this] { discardPendingEdits(); });
    setPending(false);
}

void WirePropertiesEditor::setWire(const model::Wire& wire, const model::AntennaModel& model,
    model::LengthUnit lengthUnit)
{
    original_ = wire;
    model_ = model;
    lengthUnit_ = lengthUnit;
    updating_ = true;
    // The pane names the wire; this line only explains a wire it cannot edit.
    title_->setText(tr("Arcs and helices are edited in the NEC Deck."));
    title_->setVisible(!wire.editable);
    tagControl_->setValue(wire.tag);
    segmentsControl_->setValue(wire.segments);
    const std::array<double, 6> coordinates{wire.start.x, wire.start.y, wire.start.z,
        wire.end.x, wire.end.y, wire.end.z};
    const auto suffix = unitSuffix(lengthUnit_);
    for (auto index = std::size_t{0}; index < coordinateControls_.size(); ++index) {
        coordinateControls_[index]->setSuffix(suffix);
        coordinateControls_[index]->setValue(model::fromMeters(coordinates[index], lengthUnit_));
    }
    radiusControl_->setSuffix(suffix);
    radiusControl_->setValue(model::fromMeters(wire.radius, lengthUnit_));
    const auto gauge = model::matchingAwg(wire.radius);
    gaugeControl_->setCurrentIndex(gauge ? gaugeControl_->findData(*gauge) : 0);
    updating_ = false;
    for (auto* control : findChildren<QWidget*>()) {
        if (control != title_) control->setEnabled(wire.editable);
    }
    validationLabel_->hide();
    updateDiameterLabel();
    setPending(false);
}

auto WirePropertiesEditor::wire() const -> model::Wire
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

void WirePropertiesEditor::discardPendingEdits()
{
    setWire(original_, model_, lengthUnit_);
}

void WirePropertiesEditor::setAppliedCallback(
    std::function<void(const model::Wire&, const model::Wire&)> callback)
{
    applied_ = std::move(callback);
}

auto WirePropertiesEditor::validationError(const model::Wire& updated) const -> QString
{
    if (updated.tag != original_.tag && model_.wireByTag(updated.tag) != nullptr)
        return tr("Wire tag %1 is already in use.").arg(updated.tag);
    if (updated.start == updated.end) return tr("Start and end points must be different.");
    if (!std::isfinite(updated.radius) || updated.radius <= 0.0)
        return tr("Wire radius must be a positive finite value.");
    return {};
}

void WirePropertiesEditor::apply()
{
    if (!pending_) return;
    const auto updated = wire();
    // An invalid wire stays pending, so leaving it asks whether to discard it.
    const auto error = validationError(updated);
    validationLabel_->setText(error);
    validationLabel_->setVisible(!error.isEmpty());
    if (!error.isEmpty()) return;
    const auto original = original_;
    setPending(false);
    if (applied_) applied_(original, updated);
}

void WirePropertiesEditor::selectGauge(int index)
{
    const auto data = gaugeControl_->itemData(index);
    if (!data.isValid()) return;
    selectingGauge_ = true;
    radiusControl_->setValue(model::fromMeters(model::awgRadiusMeters(data.toInt()), lengthUnit_));
    selectingGauge_ = false;
    updateDiameterLabel();
}

void WirePropertiesEditor::updateDiameterLabel()
{
    diameterLabel_->setText(tr("Diameter %1%2")
        .arg(formatDecimal(radiusControl_->value() * 2.0), unitSuffix(lengthUnit_)));
}

void WirePropertiesEditor::setPending(bool pending)
{
    pending_ = pending;
    applyButton_->setEnabled(pending && original_.editable);
    revertButton_->setEnabled(pending);
    setPendingEditIndicator(applyButton_, pending);
}

}
