#include "ui/analysis/ResultsSummaryView.h"

#include "ui/DisplayFormat.h"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace necwb::ui {
namespace {

auto sameFrequency(double first, double second) -> bool
{
    return std::abs(first - second) <= 1.0e-9 * std::max({1.0, std::abs(first), std::abs(second)});
}

auto valueLabel(QWidget* parent) -> QLabel*
{
    auto* label = new QLabel(parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

}

ResultsSummaryView::ResultsSummaryView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    auto* heading = new QLabel(tr("Result Summary"), this);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 4);
    heading->setFont(font);
    contextLabel_ = valueLabel(this);
    auto* form = new QFormLayout;
    frequencyLabel_ = valueLabel(this);
    impedanceLabel_ = valueLabel(this);
    swrLabel_ = valueLabel(this);
    radiationLabel_ = valueLabel(this);
    availabilityLabel_ = valueLabel(this);
    form->addRow(tr("Frequencies:"), frequencyLabel_);
    form->addRow(tr("Selected impedance:"), impedanceLabel_);
    form->addRow(tr("SWR range:"), swrLabel_);
    form->addRow(tr("Selected radiation:"), radiationLabel_);
    form->addRow(tr("Available data:"), availabilityLabel_);
    layout->addWidget(heading);
    layout->addWidget(contextLabel_);
    layout->addSpacing(12);
    layout->addLayout(form);
    layout->addStretch();
    clear();
}

void ResultsSummaryView::setResults(const analysis::AnalysisResult& result, const QString& context)
{
    result_ = result;
    context_ = context;
    refresh();
}

void ResultsSummaryView::setSelectedFrequency(double frequencyMHz)
{
    selectedFrequency_ = frequencyMHz;
    refresh();
}

void ResultsSummaryView::clear()
{
    result_ = {};
    context_.clear();
    selectedFrequency_ = 0.0;
    contextLabel_->setText(tr("No analysis result is loaded."));
    frequencyLabel_->setText(tr("—"));
    impedanceLabel_->setText(tr("—"));
    swrLabel_->setText(tr("—"));
    radiationLabel_->setText(tr("—"));
    availabilityLabel_->setText(tr("—"));
}

void ResultsSummaryView::refresh()
{
    contextLabel_->setText(context_.isEmpty() ? tr("Unknown run") : context_);
    std::vector<double> frequencies;
    const auto addFrequency = [&frequencies](double value) {
        if (std::ranges::none_of(frequencies, [value](double existing) {
                return sameFrequency(existing, value);
            })) frequencies.push_back(value);
    };
    for (const auto& value : result_.feedpoints) addFrequency(value.frequencyMHz);
    for (const auto& value : result_.currents) addFrequency(value.frequencyMHz);
    for (const auto& value : result_.radiation) addFrequency(value.frequencyMHz);
    std::ranges::sort(frequencies);
    frequencyLabel_->setText(frequencies.empty() ? tr("None")
        : frequencies.size() == 1 ? tr("%1 MHz").arg(formatDecimal(frequencies.front()))
        : tr("%1 points · %2 to %3 MHz").arg(frequencies.size())
            .arg(formatDecimal(frequencies.front()), formatDecimal(frequencies.back())));

    const analysis::FeedpointResult* selectedFeedpoint{};
    auto minimumSwr = std::numeric_limits<double>::infinity();
    auto maximumSwr = 0.0;
    for (const auto& feedpoint : result_.feedpoints) {
        const auto swr = analysis::standingWaveRatio(
            feedpoint.impedance, result_.referenceImpedanceOhms);
        if (std::isfinite(swr)) {
            minimumSwr = std::min(minimumSwr, swr);
            maximumSwr = std::max(maximumSwr, swr);
        }
        if (sameFrequency(feedpoint.frequencyMHz, selectedFrequency_)) selectedFeedpoint = &feedpoint;
    }
    impedanceLabel_->setText(selectedFeedpoint == nullptr ? tr("No feedpoint row at %1 MHz")
            .arg(formatDecimal(selectedFrequency_))
        : tr("%1 %2 j%3 Ω · wire %4, segment %5")
            .arg(formatDecimal(selectedFeedpoint->impedance.real()))
            .arg(selectedFeedpoint->impedance.imag() < 0.0 ? QStringLiteral("−") : QStringLiteral("+"))
            .arg(formatDecimal(std::abs(selectedFeedpoint->impedance.imag())))
            .arg(selectedFeedpoint->wireTag).arg(selectedFeedpoint->segment));
    swrLabel_->setText(std::isfinite(minimumSwr)
        ? tr("%1 minimum · %2 maximum · %3 Ω reference")
            .arg(formatDecimal(minimumSwr), formatDecimal(maximumSwr),
                formatDecimal(result_.referenceImpedanceOhms))
        : tr("No finite SWR values"));

    std::vector<analysis::RadiationSample> selectedRadiation;
    for (const auto& sample : result_.radiation) {
        if (sameFrequency(sample.frequencyMHz, selectedFrequency_)) selectedRadiation.push_back(sample);
    }
    const auto metrics = analysis::radiationMetrics(selectedRadiation, analysis::RadiationComponent::Total);
    if (metrics.valid) {
        auto text = tr("Peak %1 dBi at θ %2°, φ %3°")
            .arg(formatDecimal(metrics.peakGainDb))
            .arg(formatDecimal(metrics.peakThetaDegrees))
            .arg(formatDecimal(metrics.peakPhiDegrees));
        if (metrics.tiedPeaks.size() > 1)
            text += tr(" · %1 tied peak directions").arg(metrics.tiedPeaks.size());
        radiationLabel_->setText(text);
    } else {
        radiationLabel_->setText(tr("No radiation pattern at %1 MHz")
            .arg(formatDecimal(selectedFrequency_)));
    }

    QStringList available;
    if (!result_.feedpoints.empty()) available.append(tr("Impedance"));
    if (!result_.currents.empty()) available.append(tr("Currents"));
    if (!result_.radiation.empty()) available.append(tr("Radiation"));
    availabilityLabel_->setText(available.isEmpty() ? tr("No supported parsed data")
                                                     : available.join(QStringLiteral(" · ")));
}

}
