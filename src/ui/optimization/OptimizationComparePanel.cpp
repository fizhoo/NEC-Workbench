#include "ui/optimization/OptimizationComparePanel.h"

#include "ui/analysis/FieldResultsViews.h"
#include "ui/analysis/SweepPlotsView.h"

#include "analysis/OptimizationBaseline.h"

#include <QFrame>
#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {

OptimizationComparePanel::OptimizationComparePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("optimizationComparePanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    status_ = new QLabel(tr(
        "Run an optimization baseline before comparing a completed candidate."), this);
    statusText_ = status_->text();
    status_->setObjectName(QStringLiteral("optimizationBaselineStatus"));
    status_->setWordWrap(true);
    compareWithStartingModel_ = new QCheckBox(tr("Compare with starting model"), this);
    compareWithStartingModel_->setObjectName(
        QStringLiteral("optimizationCompareWithStartingModel"));
    compareWithStartingModel_->setToolTip(tr(
        "Overlay the solver results from the NEC model as it existed before optimization began."));
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("optimizationCompareTabs"));
    tabs_->setDocumentMode(true);
    tabs_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    sweepPlots_ = new SweepPlotsView(tabs_);
    sweepPlots_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    sweepPlots_->setReferenceControlVisible(false);
    sweepPlots_->configureForCompactReview();
    pattern_ = new RadiationPatternView(tabs_);
    pattern_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    directional_ = new DirectionalMetricsView(tabs_);
    directional_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    breakdown_ = new QLabel(tabs_);
    breakdown_->setObjectName(QStringLiteral("optimizationCompareBreakdown"));
    breakdown_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    breakdown_->setWordWrap(true);
    breakdown_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* breakdownScroll = new QScrollArea(tabs_);
    breakdownScroll->setWidgetResizable(true);
    breakdownScroll->setFrameShape(QFrame::NoFrame);
    breakdownScroll->setWidget(breakdown_);
    tabs_->addTab(sweepPlots_, tr("SWR && Impedance"));
    tabs_->addTab(pattern_, tr("2D Pattern"));
    const auto directionalTab = tabs_->addTab(
        directional_, tr("Directional vs Frequency"));
    tabs_->setTabVisible(directionalTab, false);
    tabs_->addTab(breakdownScroll, tr("Objective Details"));
    layout->addWidget(compareWithStartingModel_);
    layout->addWidget(status_);
    layout->addWidget(tabs_, 1);
    connect(compareWithStartingModel_, &QCheckBox::toggled,
        this, [this] { refresh(); });
    clearCandidate();
}

void OptimizationComparePanel::setBaseline(
    const analysis::AnalysisResult& result, QString label)
{
    baseline_ = result;
    baselineLabel_ = std::move(label);
    refresh();
}

void OptimizationComparePanel::clearBaseline()
{
    baseline_.reset();
    baselineLabel_.clear();
    baselineCompatible_ = false;
    refresh();
}

void OptimizationComparePanel::setBaselineStatus(QString status, bool compatible)
{
    statusText_ = std::move(status);
    baselineCompatible_ = compatible;
    refresh();
}

void OptimizationComparePanel::setCandidate(const analysis::AnalysisResult& result,
    QString label, QString breakdown, QString comparisonBreakdown,
    std::optional<analysis::OptimizationObjectiveResult> directionalMetrics,
    std::optional<analysis::OptimizationObjectiveResult> baselineDirectionalMetrics)
{
    candidate_ = result;
    candidateLabel_ = std::move(label);
    candidateBreakdown_ = std::move(breakdown);
    comparisonBreakdown_ = std::move(comparisonBreakdown);
    directionalMetrics_ = std::move(directionalMetrics);
    baselineDirectionalMetrics_ = std::move(baselineDirectionalMetrics);
    refresh();
}

void OptimizationComparePanel::clearCandidate()
{
    candidate_.reset();
    candidateLabel_.clear();
    candidateBreakdown_.clear();
    comparisonBreakdown_.clear();
    directionalMetrics_.reset();
    baselineDirectionalMetrics_.reset();
    refresh();
}

void OptimizationComparePanel::refresh()
{
    constexpr auto patternTab = 1;
    constexpr auto directionalTab = 2;
    if (!candidate_) {
        compareWithStartingModel_->setEnabled(false);
        compareWithStartingModel_->setChecked(false);
        status_->setText(statusText_);
        status_->hide();
        breakdown_->setText(tr("Select a completed candidate to inspect its results."));
        sweepPlots_->setResults({}, tr("a selected candidate"));
        sweepPlots_->clearReferenceResults();
        pattern_->setResults({}, tr("a selected candidate"));
        pattern_->clearReferenceResults();
        directional_->setResults({});
        directional_->clearReferenceMetrics();
        tabs_->setTabEnabled(patternTab, false);
        tabs_->setTabVisible(directionalTab, false);
        return;
    }

    sweepPlots_->setResults(*candidate_, tr("Candidate"));
    pattern_->setResults(*candidate_, tr("Candidate"));
    directional_->setResults(directionalMetrics_.value_or(
        analysis::OptimizationObjectiveResult{}));
    const auto comparisonAvailable = baselineCompatible_ && baseline_.has_value();
    compareWithStartingModel_->setEnabled(comparisonAvailable);
    compareWithStartingModel_->setToolTip(comparisonAvailable
        ? tr("Overlay %1 as dashed traces. Candidate traces remain solid.")
              .arg(baselineLabel_)
        : statusText_);
    const auto compare = comparisonAvailable && compareWithStartingModel_->isChecked();
    status_->setVisible(compare);
    status_->setText(compare
        ? tr("Starting-model traces are dashed; candidate traces are solid.")
        : statusText_);
    status_->setToolTip(compare ? statusText_ : QString{});
    auto details = candidateBreakdown_.isEmpty()
        ? tr("Objective details are unavailable for this candidate.")
        : candidateBreakdown_;
    if (compare && !comparisonBreakdown_.isEmpty())
        details += tr("\n\nComparison with starting model:\n%1")
            .arg(comparisonBreakdown_);
    breakdown_->setText(details);
    if (compare) {
        sweepPlots_->setReferenceResults(*baseline_, tr("Starting model"));
        pattern_->setReferenceResults(*baseline_, tr("Starting model"));
        if (baselineDirectionalMetrics_)
            directional_->setReferenceResults(
                *baselineDirectionalMetrics_, tr("Starting model"));
        else
            directional_->clearReferenceMetrics();
    } else {
        sweepPlots_->clearReferenceResults();
        pattern_->clearReferenceResults();
        directional_->clearReferenceMetrics();
    }
    const auto hasPattern = !candidate_->radiation.empty();
    auto hasMatchingPatternGrid = false;
    if (compare && hasPattern && !baseline_->radiation.empty()) {
        const auto candidateCoverage = analysis::optimizationResultCoverage(*candidate_);
        const auto baselineCoverage = analysis::optimizationResultCoverage(*baseline_);
        hasMatchingPatternGrid = std::ranges::any_of(
            candidateCoverage.radiation, [&baselineCoverage](const auto& candidateGrid) {
                return std::ranges::find(baselineCoverage.radiation, candidateGrid)
                    != baselineCoverage.radiation.end();
            });
        if (!hasMatchingPatternGrid) {
            status_->setText(tr(
                "Starting-model 2D overlay unavailable: the pattern cut grids differ."));
        }
    }
    tabs_->setTabEnabled(patternTab, hasPattern);
    tabs_->setTabToolTip(patternTab, hasPattern
        ? compare && hasMatchingPatternGrid
            ? tr("Candidate pattern with a compatible baseline drawn dashed.")
            : compare
                ? tr("Candidate pattern only; the baseline uses a different frequency or cut grid.")
                : tr("Candidate pattern; the baseline is missing or incompatible.")
        : tr("This candidate contains no radiation-pattern samples."));
    if (!hasPattern && tabs_->currentIndex() == patternTab) tabs_->setCurrentIndex(0);
    const auto hasDirectionalMetrics = directionalMetrics_
        && std::ranges::any_of(directionalMetrics_->frequencyMetrics,
            [](const auto& metrics) {
                return metrics.forwardGainDb || metrics.frontToBackDb
                    || metrics.frontToRearDb;
            });
    tabs_->setTabVisible(directionalTab, hasDirectionalMetrics);
    tabs_->setTabToolTip(directionalTab, hasDirectionalMetrics
        ? compare && baselineDirectionalMetrics_
            ? tr("Candidate gain, F/B, and F/R with the starting model drawn dashed.")
            : tr("Candidate gain, F/B, and F/R across the evaluated frequencies.")
        : tr("Unavailable because this candidate has no directional objective data."));
    if (!hasDirectionalMetrics && tabs_->currentIndex() == directionalTab)
        tabs_->setCurrentIndex(0);
}

}
