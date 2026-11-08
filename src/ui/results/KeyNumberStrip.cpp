#include "ui/results/KeyNumberStrip.h"

#include "analysis/FrequencyComparison.h"
#include "analysis/GainDirection.h"
#include "ui/DisplayFormat.h"
#include "ui/ElidedLabel.h"

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

constexpr auto ViewProperty = "resultView";

auto gainText(const analysis::PeakGainSummary& gain) -> analysis::GainText
{
    return analysis::formatGain({.gainDb = gain.gainDb, .thetaDegrees = gain.thetaDegrees,
        .phiDegrees = gain.phiDegrees, .frequencyMHz = gain.frequencyMHz,
        .sampleStep = gain.sampleStep});
}

auto valueText(const std::optional<double>& value, const QString& suffix = {}) -> QString
{
    return value ? formatDecimal(*value) + suffix : QString::fromUtf8("—");
}

}

KeyNumberStrip::KeyNumberStrip(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("resultsStrip"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 2);
    layout->setSpacing(2);
    auto* frequencyRow = new QHBoxLayout;
    frequencyRow->setSpacing(4);
    frequencyRow->addWidget(new QLabel(tr("At:"), this));
    frequency_ = new QComboBox(this);
    frequency_->setObjectName(QStringLiteral("resultsFrequency"));
    frequency_->setToolTip(tr("Selected frequency: every result view and the status bar "
        "follow it. Double-click a point on a sweep plot to pick one there."));
    frequency_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    frequency_->setMinimumContentsLength(10);
    frequencyRow->addWidget(frequency_, 1);
    stale_ = new QLabel(tr("STALE"), this);
    stale_->setObjectName(QStringLiteral("resultsStale"));
    stale_->setStyleSheet(QStringLiteral("color: palette(mid); font-weight: bold;"));
    stale_->setToolTip(tr("The model changed since this run. Run it again to update."));
    stale_->hide();
    frequencyRow->addWidget(stale_);
    layout->addLayout(frequencyRow);

    values_ = new QWidget(this);
    values_->setObjectName(QStringLiteral("resultsStripValues"));
    auto* grid = new QGridLayout(values_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(0);
    const auto place = [grid](int row, int column, const QString& name, ElidedLabel* value,
                           int span = 1) {
        grid->addWidget(new QLabel(name, value->parentWidget()), row, column);
        grid->addWidget(value, row, column + 1, 1, span);
    };
    resistance_ = addValue(QStringLiteral("resultsResistance"), ResultView::Swr);
    reactance_ = addValue(QStringLiteral("resultsReactance"), ResultView::Swr);
    swr_ = addValue(QStringLiteral("resultsSwr"), ResultView::Swr);
    gain_ = addValue(QStringLiteral("resultsGain"), ResultView::Azimuth);
    frontToBack_ = addValue(QStringLiteral("resultsFrontToBack"), ResultView::Azimuth);
    frontToRear_ = addValue(QStringLiteral("resultsFrontToRear"), ResultView::Azimuth);
    place(0, 0, tr("R"), resistance_);
    place(0, 2, tr("X"), reactance_);
    place(1, 0, tr("SWR"), swr_, 3);
    place(2, 0, tr("Gain"), gain_, 3);
    place(3, 0, tr("F/B"), frontToBack_);
    place(3, 2, tr("F/R"), frontToRear_);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    layout->addWidget(values_);
    extrema_ = new ElidedLabel(this);
    extrema_->setObjectName(QStringLiteral("resultsExtrema"));
    auto small = extrema_->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    extrema_->setFont(small);
    extrema_->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(extrema_);
    setSummary({}, 50.0);

    connect(frequency_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && frequencyChanged_)
            frequencyChanged_(frequency_->itemData(index).toDouble());
    });
}

auto KeyNumberStrip::addValue(const QString& name, ResultView view) -> ElidedLabel*
{
    auto* value = new ElidedLabel(values_);
    value->setObjectName(name);
    value->setProperty(ViewProperty, static_cast<int>(view));
    value->setCursor(Qt::PointingHandCursor);
    // A long gain direction would otherwise widen the column through its width hint.
    value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    value->installEventFilter(this);
    return value;
}

void KeyNumberStrip::setFrequencies(const std::vector<double>& frequenciesMHz,
    std::optional<double> selectedMHz)
{
    const QSignalBlocker blocker(frequency_);
    frequency_->clear();
    for (const auto frequency : frequenciesMHz)
        frequency_->addItem(tr("%1 MHz").arg(formatDecimal(frequency)), frequency);
    if (selectedMHz) {
        for (auto index = 0; index < frequency_->count(); ++index) {
            if (nearlyEqual(frequency_->itemData(index).toDouble(), *selectedMHz))
                frequency_->setCurrentIndex(index);
        }
    }
    frequency_->setEnabled(frequency_->count() > 1);
}

void KeyNumberStrip::setSummary(const analysis::ResultSummary& summary,
    double referenceImpedanceOhms)
{
    const auto& feedpoint = summary.selectedFeedpoint;
    resistance_->setText(valueText(feedpoint
        ? std::optional{feedpoint->impedance.real()} : std::nullopt, tr(" Ω")));
    reactance_->setText(valueText(feedpoint
        ? std::optional{feedpoint->impedance.imag()} : std::nullopt, tr(" Ω")));
    const auto referenceImpedance = formatReferenceImpedance(referenceImpedanceOhms);
    swr_->setText(feedpoint ? tr("%1 (Z0 %2)").arg(formatDecimal(feedpoint->swr),
                                 referenceImpedance)
                            : QString::fromUtf8("—"));
    swr_->setToolTip(tr("SWR against the Z0 %1 reference impedance set in Setup.")
        .arg(referenceImpedance));

    const auto selectedGain = summary.selectedGain
        ? std::optional{gainText(*summary.selectedGain)} : std::nullopt;
    gain_->setText(selectedGain ? fromCore(selectedGain->text) : QString::fromUtf8("—"));
    gain_->setToolTip(selectedGain
        ? tr("Peak at this frequency (all RP data): %1\n%2")
              .arg(fromCore(selectedGain->text), fromCore(selectedGain->toolTip))
        : tr("No radiation pattern at this frequency."));
    const auto forward = summary.selectedGain
        ? tr("Forward direction: the peak at this frequency, %1.").arg(fromCore(
              analysis::formatDirection(summary.selectedGain->thetaDegrees,
                  summary.selectedGain->phiDegrees)))
        : QString{};
    frontToBack_->setText(valueText(summary.selectedFrontToBackDb, tr(" dB")));
    frontToBack_->setToolTip(summary.selectedGain
        ? tr("%1\n%2").arg(fromCore(analysis::frontToBackDefinition()), forward)
        : tr("No radiation pattern at this frequency."));
    frontToRear_->setText(valueText(summary.selectedFrontToRearDb, tr(" dB")));
    const auto frontToRearDefinition = fromCore(analysis::frontToRearDefinition(
        analysis::DirectionalEvaluationMode::SpecifiedDirection));
    frontToRear_->setToolTip(!summary.selectedGain
        ? tr("No radiation pattern at this frequency.")
        : summary.selectedFrontToRearDb ? tr("%1\n%2").arg(frontToRearDefinition, forward)
        : summary.selectedFrontToRearAvailability
                == analysis::RadiationMetricAvailability::PatternNull
            ? tr("Unavailable: every sample behind the peak is a pattern null.\n%1")
                  .arg(frontToRearDefinition)
            : tr("Unavailable: this run has no samples behind the peak at its elevation, so "
                 "there is no azimuth data to read F/R from. Add an azimuth cut or a full "
                 "pattern in Analysis › Requests.\n%1").arg(frontToRearDefinition));

    QStringList extrema;
    QStringList extremaToolTips;
    if (summary.minimumSwr) {
        extrema.append(tr("Min SWR %1 @ %2 MHz").arg(formatDecimal(summary.minimumSwr->swr),
            formatDecimal(summary.minimumSwr->frequencyMHz)));
        extremaToolTips.append(tr("Lowest SWR in the band against Z0 %1.").arg(referenceImpedance));
    }
    if (summary.peakGain) {
        const auto peak = gainText(*summary.peakGain);
        extrema.append(tr("Band peak %1, %2 MHz")
            .arg(fromCore(peak.text), formatDecimal(summary.peakGain->frequencyMHz)));
        extremaToolTips.append(tr("Peak across the band: strongest Total sample at any "
            "frequency · %1").arg(fromCore(peak.toolTip)));
    }
    extrema_->setText(extrema.join(QStringLiteral(" · ")));
    extrema_->setToolTip(extremaToolTips.join(QStringLiteral("\n")));
    extrema_->setVisible(!extrema.isEmpty());
}

void KeyNumberStrip::setStale(bool stale)
{
    stale_->setVisible(stale);
    values_->setEnabled(!stale);
    extrema_->setEnabled(!stale);
}

void KeyNumberStrip::setFrequencyChangedCallback(std::function<void(double)> callback)
{
    frequencyChanged_ = std::move(callback);
}

void KeyNumberStrip::setOpenViewCallback(std::function<void(ResultView)> callback)
{
    openView_ = std::move(callback);
}

auto KeyNumberStrip::eventFilter(QObject* watched, QEvent* event) -> bool
{
    if (event->type() == QEvent::MouseButtonRelease && openView_
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        const auto view = watched->property(ViewProperty);
        if (view.isValid()) openView_(static_cast<ResultView>(view.toInt()));
    }
    return QWidget::eventFilter(watched, event);
}

}
