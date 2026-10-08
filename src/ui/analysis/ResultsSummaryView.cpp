#include "ui/analysis/ResultsSummaryView.h"

#include "analysis/FrequencyComparison.h"
#include "ui/DisplayFormat.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <vector>

namespace necwb::ui {
namespace {

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
    layout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto* summaryPanel = new QWidget(splitter);
    auto* summaryLayout = new QVBoxLayout(summaryPanel);
    summaryLayout->setContentsMargins(20, 20, 20, 20);
    auto* heading = new QLabel(tr("Result Summary"), summaryPanel);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 4);
    heading->setFont(font);
    contextLabel_ = valueLabel(summaryPanel);
    auto* form = new QFormLayout;
    frequencyLabel_ = valueLabel(summaryPanel);
    impedanceLabel_ = valueLabel(summaryPanel);
    swrLabel_ = valueLabel(summaryPanel);
    radiationLabel_ = valueLabel(summaryPanel);
    availabilityLabel_ = valueLabel(summaryPanel);
    form->addRow(tr("Frequencies:"), frequencyLabel_);
    form->addRow(tr("Selected impedance:"), impedanceLabel_);
    form->addRow(tr("SWR range:"), swrLabel_);
    form->addRow(tr("Selected radiation:"), radiationLabel_);
    form->addRow(tr("Available data:"), availabilityLabel_);
    summaryLayout->addWidget(heading);
    summaryLayout->addWidget(contextLabel_);
    summaryLayout->addSpacing(12);
    summaryLayout->addLayout(form);
    summaryLayout->addStretch();

    inputSnapshotPanel_ = new QWidget(splitter);
    inputSnapshotPanel_->setObjectName(QStringLiteral("historicalInputSnapshotPanel"));
    auto* snapshotLayout = new QVBoxLayout(inputSnapshotPanel_);
    snapshotLayout->setContentsMargins(12, 12, 12, 12);
    auto* snapshotHeading = new QLabel(tr("Input Snapshot"), inputSnapshotPanel_);
    auto snapshotFont = snapshotHeading->font();
    snapshotFont.setBold(true);
    snapshotHeading->setFont(snapshotFont);
    inputSnapshotSource_ = new QComboBox(inputSnapshotPanel_);
    inputSnapshotSource_->setObjectName(QStringLiteral("historicalInputSnapshotSource"));
    inputSnapshotText_ = new QPlainTextEdit(inputSnapshotPanel_);
    inputSnapshotText_->setObjectName(QStringLiteral("historicalInputSnapshotText"));
    inputSnapshotText_->setReadOnly(true);
    inputSnapshotText_->setLineWrapMode(QPlainTextEdit::NoWrap);
    inputSnapshotText_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    snapshotLayout->addWidget(snapshotHeading);
    snapshotLayout->addWidget(inputSnapshotSource_);
    snapshotLayout->addWidget(inputSnapshotText_, 1);
    connect(inputSnapshotSource_, &QComboBox::currentIndexChanged,
        this, [this] { refreshInputSnapshot(); });

    splitter->addWidget(summaryPanel);
    splitter->addWidget(inputSnapshotPanel_);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter);
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

void ResultsSummaryView::setHistoricalInputSnapshot(const QString& authoredSource,
    const QString& generatedDeck)
{
    authoredSource_ = authoredSource;
    generatedDeck_ = generatedDeck;
    inputSnapshotSource_->clear();
    if (!authoredSource_.isEmpty())
        inputSnapshotSource_->addItem(tr("Authored Source"), QStringLiteral("authored"));
    if (!generatedDeck_.isEmpty())
        inputSnapshotSource_->addItem(tr("Generated Solver Deck"), QStringLiteral("generated"));
    if (inputSnapshotSource_->count() == 0)
        inputSnapshotSource_->addItem(tr("Input Snapshot Unavailable"), QStringLiteral("missing"));
    inputSnapshotPanel_->show();
    refreshInputSnapshot();
}

void ResultsSummaryView::clearInputSnapshot()
{
    authoredSource_.clear();
    generatedDeck_.clear();
    inputSnapshotSource_->clear();
    inputSnapshotText_->clear();
    inputSnapshotPanel_->hide();
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
    clearInputSnapshot();
}

void ResultsSummaryView::refreshInputSnapshot()
{
    const auto source = inputSnapshotSource_->currentData().toString();
    if (source == QStringLiteral("authored")) {
        inputSnapshotText_->setPlainText(authoredSource_);
    } else if (source == QStringLiteral("generated")) {
        inputSnapshotText_->setPlainText(generatedDeck_);
    } else {
        inputSnapshotText_->setPlainText(tr("No archived input deck is available for this run."));
    }
    inputSnapshotText_->document()->setModified(false);
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
