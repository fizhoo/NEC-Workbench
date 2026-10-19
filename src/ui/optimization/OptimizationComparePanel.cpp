#include "ui/optimization/OptimizationComparePanel.h"

#include "ui/analysis/FieldResultsViews.h"
#include "ui/analysis/SweepPlotsView.h"

#include "analysis/OptimizationBaseline.h"

#include <QFrame>
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
    layout->setContentsMargins(0, 0, 0, 0);
    status_ = new QLabel(tr(
        "Run an optimization baseline before comparing a completed candidate."), this);
    statusText_ = status_->text();
    status_->setObjectName(QStringLiteral("optimizationBaselineStatus"));
    status_->setWordWrap(true);
    comparison_ = new QLabel(this);
    comparison_->setObjectName(QStringLiteral("optimizationComparisonIdentity"));
    comparison_->setWordWrap(true);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    sweepPlots_ = new SweepPlotsView(tabs_);
    sweepPlots_->setReferenceControlVisible(false);
    pattern_ = new RadiationPatternView(tabs_);
    breakdown_ = new QLabel(tabs_);
    breakdown_->setObjectName(QStringLiteral("optimizationCompareBreakdown"));
    breakdown_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    breakdown_->setWordWrap(true);
    breakdown_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* breakdownScroll = new QScrollArea(tabs_);
    breakdownScroll->setWidgetResizable(true);
    breakdownScroll->setFrameShape(QFrame::NoFrame);
    breakdownScroll->setWidget(breakdown_);
    tabs_->addTab(sweepPlots_, tr("SWR & Impedance"));
    tabs_->addTab(pattern_, tr("2D Pattern"));
    tabs_->addTab(breakdownScroll, tr("Candidate Details"));
    layout->addWidget(status_);
    layout->addWidget(comparison_);
    layout->addWidget(tabs_, 1);
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
    QString label, QString breakdown)
{
    candidate_ = result;
    candidateLabel_ = std::move(label);
    breakdown_->setText(breakdown.isEmpty()
        ? tr("Objective details are unavailable for this candidate.")
        : std::move(breakdown));
    refresh();
}

void OptimizationComparePanel::clearCandidate()
{
    candidate_.reset();
    candidateLabel_.clear();
    breakdown_->setText(tr("Select a completed candidate to inspect its results."));
    refresh();
}

void OptimizationComparePanel::refresh()
{
    constexpr auto patternTab = 1;
    status_->setText(statusText_);
    if (!candidate_) {
        comparison_->setText(tr(
            "No candidate selected. Double-click a completed evaluation to compare it with the optimization baseline."));
        sweepPlots_->setResults({}, tr("a selected candidate"));
        sweepPlots_->clearReferenceResults();
        pattern_->setResults({}, tr("a selected candidate"));
        pattern_->clearReferenceResults();
        tabs_->setTabEnabled(patternTab, false);
        return;
    }

    sweepPlots_->setResults(*candidate_, candidateLabel_);
    pattern_->setResults(*candidate_, candidateLabel_);
    const auto compare = baselineCompatible_ && baseline_.has_value();
    comparison_->setText(compare
        ? tr("Comparing %1 with %2. Candidate traces are solid; optimization-baseline traces are dashed.")
              .arg(candidateLabel_, baselineLabel_)
        : tr("Showing %1 without an optimization-baseline comparison.")
              .arg(candidateLabel_));
    if (compare) {
        sweepPlots_->setReferenceResults(*baseline_, baselineLabel_);
        pattern_->setReferenceResults(*baseline_, baselineLabel_);
    } else {
        sweepPlots_->clearReferenceResults();
        pattern_->clearReferenceResults();
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
            status_->setText(statusText_ + tr(
                " · 2D overlay unavailable: candidate and baseline use different RP cut grids"));
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
}

}
