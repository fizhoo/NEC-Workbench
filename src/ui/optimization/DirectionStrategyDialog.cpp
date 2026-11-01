#include "ui/optimization/DirectionStrategyDialog.h"

#include "ui/DisplayFormat.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace necwb::ui {
namespace {

constexpr auto planeToleranceDegrees = 1.0e-6;

auto circularDistance(double left, double right) -> double
{
    auto difference = std::fmod(std::abs(left - right), 360.0);
    return std::min(difference, 360.0 - difference);
}

struct PreviewPoint {
    double angleDegrees{};
    double gainDb{};
};

}

class DirectionCutPreviewWidget final : public QWidget {
public:
    explicit DirectionCutPreviewWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(300, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setData(std::vector<PreviewPoint> points, std::optional<double> markerAngle,
        QString emptyText, bool elevation)
    {
        points_ = std::move(points);
        markerAngle_ = markerAngle;
        emptyText_ = std::move(emptyText);
        elevation_ = elevation;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto foreground = palette().color(QPalette::Text);
        const auto grid = palette().color(QPalette::Mid);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        const auto center = QPointF(width() / 2.0, height() / 2.0);
        const auto radius = std::max(20.0, std::min(width(), height()) / 2.0 - 25.0);
        painter.setPen(QPen(grid, 1.0));
        for (const auto fraction : {0.33, 0.66, 1.0})
            painter.drawEllipse(center, radius * fraction, radius * fraction);
        painter.drawLine(QPointF(center.x() - radius, center.y()),
            QPointF(center.x() + radius, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - radius),
            QPointF(center.x(), center.y() + radius));
        painter.setPen(foreground);
        painter.drawText(QRectF(center.x() - 30.0, center.y() - radius - 20.0, 60.0, 18.0),
            Qt::AlignCenter, elevation_ ? QStringLiteral("90°") : QStringLiteral("0°"));
        painter.drawText(QRectF(center.x() + radius + 3.0, center.y() - 9.0, 45.0, 18.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            elevation_ ? QStringLiteral("0°") : QStringLiteral("90°"));
        painter.drawText(QRectF(center.x() - 30.0, center.y() + radius + 2.0, 60.0, 18.0),
            Qt::AlignCenter, elevation_ ? QStringLiteral("−90°") : QStringLiteral("180°"));
        painter.drawText(QRectF(center.x() - radius - 48.0, center.y() - 9.0, 45.0, 18.0),
            Qt::AlignRight | Qt::AlignVCenter,
            elevation_ ? QStringLiteral("180°") : QStringLiteral("270°"));
        if (points_.size() < 2) {
            painter.drawText(rect().adjusted(20, 35, -20, -35),
                Qt::AlignCenter | Qt::TextWordWrap, emptyText_);
            return;
        }

        const auto peak = std::ranges::max_element(points_, {}, &PreviewPoint::gainDb);
        const auto floor = peak->gainDb - 30.0;
        QPainterPath path;
        auto first = true;
        for (const auto& point : points_) {
            const auto radial = radius * std::clamp((point.gainDb - floor) / 30.0, 0.0, 1.0);
            const auto radians = (elevation_ ? -point.angleDegrees
                                             : point.angleDegrees - 90.0)
                * std::numbers::pi / 180.0;
            const QPointF plotted{center.x() + radial * std::cos(radians),
                center.y() + radial * std::sin(radians)};
            if (first) {
                path.moveTo(plotted);
                first = false;
            } else {
                path.lineTo(plotted);
            }
        }
        painter.setPen(QPen(QColor(35, 120, 220), 2.0));
        painter.drawPath(path);

        const auto marker = markerAngle_.value_or(peak->angleDegrees);
        const auto nearest = std::ranges::min_element(points_, {}, [marker](const auto& point) {
            return circularDistance(point.angleDegrees, marker);
        });
        const auto markerRadius = radius
            * std::clamp((nearest->gainDb - floor) / 30.0, 0.0, 1.0);
        const auto markerRadians = (elevation_ ? -nearest->angleDegrees
                                               : nearest->angleDegrees - 90.0)
            * std::numbers::pi / 180.0;
        const QPointF markerPoint{center.x() + markerRadius * std::cos(markerRadians),
            center.y() + markerRadius * std::sin(markerRadians)};
        painter.setBrush(QColor(220, 45, 45));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(markerPoint, 4.0, 4.0);
        painter.setPen(foreground);
        painter.drawText(QRectF(8, 4, width() - 16, 20), Qt::AlignLeft,
            tr("%1 dBi at %2 %3")
                .arg(formatDecimal(nearest->gainDb), elevation_ ? tr("El") : tr("Az"),
                    fromCore(analysis::formatAngle(nearest->angleDegrees))));
    }

private:
    std::vector<PreviewPoint> points_;
    std::optional<double> markerAngle_;
    QString emptyText_;
    bool elevation_{};
};

DirectionStrategyDialog::DirectionStrategyDialog(
    std::vector<analysis::RadiationSample> radiation, double targetFrequencyMHz,
    analysis::RadiationComponent component, analysis::DirectionalEvaluationMode mode,
    double thetaDegrees, double phiDegrees, QWidget* parent)
    : QDialog(parent), radiation_(std::move(radiation)),
      targetFrequencyMHz_(targetFrequencyMHz), component_(component)
{
    setObjectName(QStringLiteral("optimizationDirectionStrategyDialog"));
    setWindowTitle(tr("Direction Setup"));
    setModal(true);
    resize(570, 650);
    auto* layout = new QVBoxLayout(this);
    auto* introduction = new QLabel(tr(
        "Choose one 2D pattern slice, then measure either its strongest gain or gain at one specific angle."), this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* form = new QFormLayout;
    patternControl_ = new QComboBox(this);
    patternControl_->setObjectName(QStringLiteral("directionSetupPattern"));
    patternControl_->addItem(tr("Azimuth — around the antenna"),
        static_cast<int>(PatternSlice::Azimuth));
    patternControl_->addItem(tr("Elevation — above the ground"),
        static_cast<int>(PatternSlice::Elevation));
    sliceAngleLabel_ = new QLabel(this);
    sliceAngle_ = new QDoubleSpinBox(this);
    sliceAngle_->setObjectName(QStringLiteral("directionSetupSliceAngle"));
    sliceAngle_->setRange(-360.0, 360.0);
    sliceAngle_->setDecimals(DisplayDecimalPlaces);
    sliceAngle_->setSuffix(QStringLiteral("°"));
    measurementControl_ = new QComboBox(this);
    measurementControl_->setObjectName(QStringLiteral("directionSetupMeasurement"));
    measurementControl_->addItem(tr("Strongest gain in this slice"),
        static_cast<int>(GainMeasurement::StrongestInSlice));
    measurementControl_->addItem(tr("Gain at a specific angle"),
        static_cast<int>(GainMeasurement::SpecificAngle));
    gainAngleLabel_ = new QLabel(this);
    gainAngle_ = new QDoubleSpinBox(this);
    gainAngle_->setObjectName(QStringLiteral("directionSetupGainAngle"));
    gainAngle_->setRange(-360.0, 360.0);
    gainAngle_->setDecimals(DisplayDecimalPlaces);
    gainAngle_->setSuffix(QStringLiteral("°"));
    form->addRow(tr("Pattern"), patternControl_);
    form->addRow(sliceAngleLabel_, sliceAngle_);
    form->addRow(tr("Measure gain"), measurementControl_);
    form->addRow(gainAngleLabel_, gainAngle_);
    layout->addLayout(form);

    latestResult_ = new QLabel(this);
    latestResult_->setObjectName(QStringLiteral("directionSetupLatestResult"));
    latestResult_->setWordWrap(true);
    layout->addWidget(latestResult_);
    previewLabel_ = new QLabel(this);
    previewLabel_->setWordWrap(true);
    layout->addWidget(previewLabel_);
    preview_ = new DirectionCutPreviewWidget(this);
    preview_->setObjectName(QStringLiteral("directionSetupPreview"));
    layout->addWidget(preview_, 1);

    const auto elevationSlice = mode
        == analysis::DirectionalEvaluationMode::StrongestInElevationCut;
    patternControl_->setCurrentIndex(elevationSlice ? 1 : 0);
    measurementControl_->setCurrentIndex(mode
            == analysis::DirectionalEvaluationMode::SpecifiedDirection
        ? 1 : 0);
    if (elevationSlice) {
        sliceAngle_->setValue(phiDegrees);
        gainAngle_->setValue(90.0 - thetaDegrees);
    } else {
        sliceAngle_->setValue(90.0 - thetaDegrees);
        gainAngle_->setValue(phiDegrees);
    }
    previousPatternSlice_ = patternSlice();

    auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* latestPeak = actions->addButton(tr("Use Latest Pattern Peak"),
        QDialogButtonBox::ActionRole);
    latestPeak->setEnabled(!radiation_.empty());
    connect(latestPeak, &QPushButton::clicked, this, [this] { useLatestPeak(); });
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(patternControl_, &QComboBox::currentIndexChanged, this, [this] {
        latestPatternPeakSelected_ = false;
        const auto theta = previousPatternSlice_ == PatternSlice::Azimuth
            ? 90.0 - sliceAngle_->value() : 90.0 - gainAngle_->value();
        const auto phi = previousPatternSlice_ == PatternSlice::Azimuth
            ? gainAngle_->value() : sliceAngle_->value();
        if (patternSlice() == PatternSlice::Azimuth) {
            sliceAngle_->setValue(90.0 - theta);
            gainAngle_->setValue(phi);
        } else {
            sliceAngle_->setValue(phi);
            gainAngle_->setValue(90.0 - theta);
        }
        previousPatternSlice_ = patternSlice();
        updateControls();
        updatePreview();
    });
    connect(measurementControl_, &QComboBox::currentIndexChanged,
        this, [this] {
            latestPatternPeakSelected_ = false;
            updateControls();
            updatePreview();
        });
    connect(sliceAngle_, &QDoubleSpinBox::valueChanged, this, [this] {
        latestPatternPeakSelected_ = false;
        updatePreview();
    });
    connect(gainAngle_, &QDoubleSpinBox::valueChanged, this, [this] {
        latestPatternPeakSelected_ = false;
        updatePreview();
    });
    layout->addWidget(actions);
    updateControls();
    updatePreview();
}

auto DirectionStrategyDialog::patternSlice() const -> PatternSlice
{
    return static_cast<PatternSlice>(patternControl_->currentData().toInt());
}

auto DirectionStrategyDialog::gainMeasurement() const -> GainMeasurement
{
    return static_cast<GainMeasurement>(measurementControl_->currentData().toInt());
}

auto DirectionStrategyDialog::mode() const -> analysis::DirectionalEvaluationMode
{
    if (gainMeasurement() == GainMeasurement::SpecificAngle)
        return analysis::DirectionalEvaluationMode::SpecifiedDirection;
    return patternSlice() == PatternSlice::Azimuth
        ? analysis::DirectionalEvaluationMode::StrongestInAzimuthCut
        : analysis::DirectionalEvaluationMode::StrongestInElevationCut;
}

auto DirectionStrategyDialog::thetaDegrees() const -> double
{
    return patternSlice() == PatternSlice::Azimuth
        ? 90.0 - sliceAngle_->value() : 90.0 - gainAngle_->value();
}

auto DirectionStrategyDialog::phiDegrees() const -> double
{
    return patternSlice() == PatternSlice::Azimuth
        ? gainAngle_->value() : sliceAngle_->value();
}

auto DirectionStrategyDialog::usesLatestPatternPeak() const noexcept -> bool
{
    return latestPatternPeakSelected_;
}

void DirectionStrategyDialog::updateControls()
{
    const auto azimuth = patternSlice() == PatternSlice::Azimuth;
    sliceAngleLabel_->setText(azimuth ? tr("At elevation") : tr("At azimuth"));
    gainAngleLabel_->setText(azimuth ? tr("Specific azimuth") : tr("Specific elevation"));
    gainAngleLabel_->setVisible(gainMeasurement() == GainMeasurement::SpecificAngle);
    gainAngle_->setVisible(gainMeasurement() == GainMeasurement::SpecificAngle);
    previewLabel_->setText(azimuth
        ? tr("Azimuth pattern · 0° is the +X direction")
        : tr("Elevation pattern · 0° is the horizon, 90° is straight up"));
}

void DirectionStrategyDialog::updatePreview()
{
    if (radiation_.empty()) {
        latestResult_->setText(tr("Latest result: no multi-angle radiation pattern available."));
        preview_->setData({}, {}, tr("Run the active model with a radiation pattern first."),
            patternSlice() == PatternSlice::Elevation);
        return;
    }
    const auto nearestFrequency = std::ranges::min_element(radiation_, {},
        [this](const auto& sample) {
            return std::abs(sample.frequencyMHz - targetFrequencyMHz_);
        })->frequencyMHz;
    const auto requestedTheta = 90.0 - sliceAngle_->value();
    const auto requestedPhi = sliceAngle_->value();
    const auto nearestPlane = std::ranges::min_element(radiation_, {},
        [this, nearestFrequency, requestedTheta, requestedPhi](const auto& sample) {
            if (std::abs(sample.frequencyMHz - nearestFrequency) > 1.0e-6)
                return std::numeric_limits<double>::infinity();
            return patternSlice() == PatternSlice::Azimuth
                ? std::abs(sample.thetaDegrees - requestedTheta)
                : circularDistance(sample.phiDegrees, requestedPhi);
        });
    std::vector<PreviewPoint> points;
    for (const auto& sample : radiation_) {
        if (std::abs(sample.frequencyMHz - nearestFrequency) > 1.0e-6) continue;
        const auto samePlane = patternSlice() == PatternSlice::Azimuth
            ? std::abs(sample.thetaDegrees - nearestPlane->thetaDegrees)
                <= planeToleranceDegrees
            : circularDistance(sample.phiDegrees, nearestPlane->phiDegrees)
                <= planeToleranceDegrees;
        const auto gain = analysis::radiationGainDb(sample, component_);
        if (!samePlane || !std::isfinite(gain) || gain <= -900.0) continue;
        points.push_back({patternSlice() == PatternSlice::Azimuth
                ? sample.phiDegrees : 90.0 - sample.thetaDegrees,
            gain});
    }
    std::ranges::sort(points, {}, &PreviewPoint::angleDegrees);
    if (points.empty()) {
        latestResult_->setText(tr("Latest result: selected slice contains no usable gain samples."));
    } else {
        const auto peak = std::ranges::max_element(points, {}, &PreviewPoint::gainDb);
        latestResult_->setText(tr("Latest result: maximum in this slice %1 dBi at %2 %3 · %4 MHz")
            .arg(formatDecimal(peak->gainDb),
                patternSlice() == PatternSlice::Azimuth ? tr("Az") : tr("El"),
                fromCore(analysis::formatAngle(peak->angleDegrees)),
                formatDecimal(nearestFrequency)));
    }
    preview_->setData(std::move(points),
        gainMeasurement() == GainMeasurement::SpecificAngle
            ? std::optional{gainAngle_->value()} : std::nullopt,
        tr("The latest pattern does not contain this 2D slice."),
        patternSlice() == PatternSlice::Elevation);
}

void DirectionStrategyDialog::useLatestPeak()
{
    const auto peak = analysis::radiationPeakAtNearestFrequency(
        radiation_, component_, targetFrequencyMHz_);
    if (!peak || peak->sampleCount <= 1) return;
    if (patternSlice() == PatternSlice::Azimuth) {
        sliceAngle_->setValue(90.0 - peak->metrics.peakThetaDegrees);
        gainAngle_->setValue(peak->metrics.peakPhiDegrees);
    } else {
        sliceAngle_->setValue(peak->metrics.peakPhiDegrees);
        gainAngle_->setValue(90.0 - peak->metrics.peakThetaDegrees);
    }
    latestPatternPeakSelected_ = true;
    updatePreview();
}

}
