#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/DisplayFormat.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/optimization/OptimizationComparePanel.h"
#include "ui/optimization/OptimizationWorkspaceSupport.h"

#include "analysis/AnalysisResult.h"
#include "analysis/FrequencyComparison.h"
#include "analysis/OptimizationObjective.h"

#include <QDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace necwb::ui {
using namespace detail;

namespace {

auto optionalNumericItem(const std::optional<double>& value) -> QTableWidgetItem*
{
    if (value) return numericItem(*value);
    auto* item = new QTableWidgetItem(QString::fromUtf8("—"));
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

auto goalName(analysis::OptimizationGoal goal,
    analysis::GoodEnoughDirection direction) -> QString
{
    switch (goal) {
    case analysis::OptimizationGoal::Minimize: return QObject::tr("Minimize");
    case analysis::OptimizationGoal::Maximize: return QObject::tr("Maximize");
    case analysis::OptimizationGoal::Target: return QObject::tr("Target");
    case analysis::OptimizationGoal::GoodEnough:
        return direction == analysis::GoodEnoughDirection::AtLeast
            ? QObject::tr("Good Enough ≥") : QObject::tr("Good Enough ≤");
    }
    return {};
}

auto aggregationName(analysis::OptimizationAggregation aggregation,
    analysis::OptimizationGoal goal) -> QString
{
    const auto suffix = goal == analysis::OptimizationGoal::Target
            || goal == analysis::OptimizationGoal::GoodEnough
        ? QObject::tr(" error") : QString{};
    switch (aggregation) {
    case analysis::OptimizationAggregation::Minimum:
        return QObject::tr("Minimum") + suffix;
    case analysis::OptimizationAggregation::Average:
        return QObject::tr("Average") + suffix;
    case analysis::OptimizationAggregation::Maximum:
        return QObject::tr("Maximum") + suffix;
    }
    return {};
}

}

void OptimizationWorkspace::showCandidateDetails(int row)
{
    detailCandidateRow_ = row;
    updateCandidateDetails(row);
    candidateDetailViews_->setCurrentIndex(0);
    restoreCandidateDetailsWindow();
}

void OptimizationWorkspace::restoreCandidateDetailsWindow()
{
    if (candidateDetailsWindow_ == nullptr || detailCandidateRow_ < 0) return;
    if (candidateDetailsWindow_->isMinimized()) candidateDetailsWindow_->showNormal();
    else candidateDetailsWindow_->show();
    candidateDetailsWindow_->raise();
    candidateDetailsWindow_->activateWindow();
}

void OptimizationWorkspace::updateCandidateDetails(int row)
{
    candidateDetailsTable_->setRowCount(0);
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()) {
        candidateDetailLabel_->setText(
            tr("Candidate details are unavailable."));
        updateCandidateActionState();
        return;
    }

    auto& candidate = candidates_[static_cast<std::size_t>(row)];
    ensureCandidateAnalysisLoaded(candidate);
    const auto candidateIdentity = tr("Candidate %1 — %2")
        .arg(row + 1)
        .arg(candidateDescription(candidate));
    auto feedpoints = candidate.feedpoints;
    std::ranges::sort(feedpoints, {}, &analysis::FeedpointResult::frequencyMHz);
    const auto evaluation = candidate.evaluation
        ? candidate.evaluation : candidate.availableMeasurements;
    const auto detailRows = evaluation ? evaluation->frequencyMetrics.size() : feedpoints.size();
    candidateDetailsTable_->setRowCount(static_cast<int>(detailRows));
    for (std::size_t index = 0; index < detailRows; ++index) {
        const auto detailRow = static_cast<int>(index);
        analysis::OptimizationFrequencyMetrics metrics;
        if (evaluation) {
            metrics = evaluation->frequencyMetrics[index];
        } else {
            metrics.frequencyMHz = feedpoints[index].frequencyMHz;
            metrics.swr = analysis::standingWaveRatio(feedpoints[index].impedance,
                activeObjective_.referenceImpedance);
            metrics.resistanceOhms = feedpoints[index].impedance.real();
            metrics.reactanceOhms = feedpoints[index].impedance.imag();
        }
        candidateDetailsTable_->setItem(detailRow, 0, numericItem(metrics.frequencyMHz));
        candidateDetailsTable_->setItem(detailRow, 1, optionalNumericItem(metrics.swr));
        candidateDetailsTable_->setItem(detailRow, 2,
            optionalNumericItem(metrics.resistanceOhms));
        candidateDetailsTable_->setItem(detailRow, 3,
            optionalNumericItem(metrics.reactanceOhms));
        candidateDetailsTable_->setItem(detailRow, 4,
            optionalNumericItem(metrics.forwardGainDb));
        candidateDetailsTable_->setItem(detailRow, 5,
            optionalNumericItem(metrics.frontToBackDb));
        candidateDetailsTable_->setItem(detailRow, 6,
            optionalNumericItem(metrics.frontToRearDb));
        const auto direction = metrics.forwardThetaDegrees && metrics.forwardPhiDegrees
            ? std::optional{analysis::patternDirection(
                  *metrics.forwardThetaDegrees, *metrics.forwardPhiDegrees)}
            : std::nullopt;
        candidateDetailsTable_->setItem(detailRow, 7, optionalNumericItem(direction
            ? std::optional{direction->elevationDegrees} : std::nullopt));
        candidateDetailsTable_->setItem(detailRow, 8, optionalNumericItem(direction
            ? std::optional{direction->azimuthDegrees} : std::nullopt));
        if (direction) {
            const auto necDirection = tr("NEC %1").arg(fromCore(analysis::formatNecDirection(
                *metrics.forwardThetaDegrees, *metrics.forwardPhiDegrees)));
            candidateDetailsTable_->item(detailRow, 7)->setToolTip(necDirection);
            candidateDetailsTable_->item(detailRow, 8)->setToolTip(necDirection);
        }
        if (candidate.evaluation && evaluation->evaluatedFrequencyCount == 1
            && std::abs(evaluation->evaluationFrequencyMHz - metrics.frequencyMHz) < 1.0e-9) {
            for (auto column = 0; column < candidateDetailsTable_->columnCount(); ++column) {
                auto* item = candidateDetailsTable_->item(detailRow, column);
                auto font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
        }
    }
    QStringList breakdown;
    if (candidate.evaluation) {
        breakdown.append(evaluation->evaluatedFrequencyCount == 1
            ? tr("Score %1 at %2 MHz")
                .arg(formatDecimal(evaluation->score))
                .arg(formatDecimal(evaluation->evaluationFrequencyMHz))
            : tr("Score %1 across %2 frequencies")
                .arg(formatDecimal(evaluation->score))
                .arg(evaluation->evaluatedFrequencyCount));
        const auto addCriterion = [this, &breakdown](const QString& name,
                                      double weight, analysis::OptimizationGoal goal,
                                      analysis::GoodEnoughDirection direction,
                                      analysis::OptimizationAggregation aggregation,
                                      double target, double metric, double component,
                                      const QString& unit) {
            if (weight <= 0.0) return;
            auto description = tr("%1 · %2 · %3 · value %4%5 · component %6")
                .arg(name, goalName(goal, direction), aggregationName(aggregation, goal),
                    formatDecimal(metric), unit, formatDecimal(component));
            if (goal == analysis::OptimizationGoal::Target
                || goal == analysis::OptimizationGoal::GoodEnough) {
                description += tr(" · setting %1%2").arg(formatDecimal(target), unit);
            }
            breakdown.append(description);
        };
        addCriterion(tr("SWR"), activeObjective_.swrWeight,
            activeObjective_.swrGoal, activeObjective_.swrGoodEnoughDirection,
            activeObjective_.swrAggregation, activeObjective_.swrTarget,
            evaluation->swr, evaluation->swrComponent, {});
        if (evaluation->feedpoint) {
            addCriterion(tr("Resistance"), activeObjective_.resistanceWeight,
                activeObjective_.resistanceGoal,
                activeObjective_.resistanceGoodEnoughDirection,
                activeObjective_.resistanceAggregation,
                activeObjective_.resistanceTargetOhms,
                evaluation->feedpoint->impedance.real(),
                evaluation->resistanceComponent, tr(" Ω"));
            addCriterion(tr("Reactance"), activeObjective_.reactanceWeight,
                activeObjective_.reactanceGoal,
                activeObjective_.reactanceGoodEnoughDirection,
                activeObjective_.reactanceAggregation,
                activeObjective_.reactanceTargetOhms,
                evaluation->feedpoint->impedance.imag(),
                evaluation->reactanceComponent, tr(" Ω"));
        }
        if (evaluation->forwardGainDb) {
            addCriterion(activeObjective_.directionalEvaluationMode
                    == analysis::DirectionalEvaluationMode::SpecifiedDirection
                    ? tr("Gain at direction") : tr("Peak gain in cut"),
                activeObjective_.forwardGainWeight,
                activeObjective_.forwardGainGoal,
                activeObjective_.forwardGainGoodEnoughDirection,
                activeObjective_.forwardGainAggregation,
                activeObjective_.forwardGainTarget, *evaluation->forwardGainDb,
                evaluation->forwardGainComponent, tr(" dBi"));
        }
        if (evaluation->frontToBackDb) {
            addCriterion(tr("F/B"), activeObjective_.frontToBackWeight,
                activeObjective_.frontToBackGoal,
                activeObjective_.frontToBackGoodEnoughDirection,
                activeObjective_.frontToBackAggregation,
                activeObjective_.frontToBackTarget, *evaluation->frontToBackDb,
                evaluation->frontToBackComponent, tr(" dB"));
        }
        if (evaluation->frontToRearDb) {
            addCriterion(tr("F/R"), activeObjective_.frontToRearWeight,
                activeObjective_.frontToRearGoal,
                activeObjective_.frontToRearGoodEnoughDirection,
                activeObjective_.frontToRearAggregation,
                activeObjective_.frontToRearTarget, *evaluation->frontToRearDb,
                evaluation->frontToRearComponent, tr(" dB"));
        }
        const auto addSummary = [this, &breakdown](const QString& name, const QString& unit,
                                    const std::optional<analysis::OptimizationMetricSummary>& summary) {
            if (!summary) return;
            breakdown.append(tr(
                "%1 min %2 %3 at %4 MHz / avg %5 %3 / max %6 %3 at %7 MHz")
                .arg(name, formatDecimal(summary->minimum), unit,
                    formatDecimal(summary->minimumFrequencyMHz),
                    formatDecimal(summary->average), formatDecimal(summary->maximum),
                    formatDecimal(summary->maximumFrequencyMHz)));
        };
        addSummary(tr("SWR"), {}, evaluation->swrSummary);
        addSummary(tr("Gain"), tr("dBi"), evaluation->forwardGainSummary);
        addSummary(tr("F/B"), tr("dB"), evaluation->frontToBackSummary);
        addSummary(tr("F/R"), tr("dB"), evaluation->frontToRearSummary);
        if (activeObjective_.directionalEvaluationMode
            != analysis::DirectionalEvaluationMode::SpecifiedDirection) {
            const auto firstDirection = std::ranges::find_if(
                evaluation->frequencyMetrics, [](const auto& metrics) {
                    return metrics.forwardThetaDegrees && metrics.forwardPhiDegrees;
                });
            if (firstDirection != evaluation->frequencyMetrics.end()) {
                const auto directionVaries = std::ranges::any_of(
                    evaluation->frequencyMetrics, [&firstDirection](const auto& metrics) {
                        return metrics.forwardThetaDegrees && metrics.forwardPhiDegrees
                            && (std::abs(*metrics.forwardThetaDegrees
                                    - *firstDirection->forwardThetaDegrees) > 1.0e-6
                                || std::abs(*metrics.forwardPhiDegrees
                                    - *firstDirection->forwardPhiDegrees) > 1.0e-6);
                    });
                breakdown.append(directionVaries
                    ? tr("Tracked lobe direction varies by frequency; see El/Az columns")
                    : tr("Tracked lobe direction %1 (NEC %2)")
                          .arg(fromCore(analysis::formatDirection(
                                   *firstDirection->forwardThetaDegrees,
                                   *firstDirection->forwardPhiDegrees)),
                              fromCore(analysis::formatNecDirection(
                                  *firstDirection->forwardThetaDegrees,
                                  *firstDirection->forwardPhiDegrees))));
            }
        }
    }
    if (!candidate.evaluation && candidate.availableMeasurements) {
        breakdown.append(tr("Objective unavailable: %1").arg(candidate.failureDetail));
        breakdown.append(tr("Available measurements retained across %1 frequencies")
            .arg(candidate.availableMeasurements->evaluatedFrequencyCount));
    }
    QStringList header{tr("Candidate %1").arg(row + 1), candidateDescription(candidate)};
    if (candidate.evaluation) {
        auto score = tr("score %1").arg(formatDecimal(candidate.evaluation->score));
        header.append(score);
        if (candidate.evaluation->swrSummary) {
            header.append(tr("min SWR %1 @ %2 MHz")
                .arg(formatDecimal(candidate.evaluation->swrSummary->minimum),
                    formatDecimal(candidate.evaluation->swrSummary->minimumFrequencyMHz)));
        }
        if (candidate.evaluation->frontToBackDb)
            header.append(tr("F/B %1 dB")
                .arg(formatDecimal(*candidate.evaluation->frontToBackDb)));
        else if (candidate.evaluation->forwardGainDb)
            header.append(tr("gain %1").arg(fromCore(analysis::formatGain({
                .gainDb = *candidate.evaluation->forwardGainDb,
                .thetaDegrees = candidate.evaluation->forwardThetaDegrees,
                .phiDegrees = candidate.evaluation->forwardPhiDegrees,
                .component = activeObjective_.radiationComponent,
                .frequencyMHz = candidate.evaluation->forwardGainFrequencyMHz,
                .sampleStep = std::nullopt}).text)));
        else if (candidate.evaluation->frontToRearDb)
            header.append(tr("F/R %1 dB")
                .arg(formatDecimal(*candidate.evaluation->frontToRearDb)));
    }
    auto headerText = header.join(QStringLiteral(" | "));
    if (headerText.size() > 110) headerText = headerText.left(109) + QChar(0x2026);
    candidateDetailLabel_->setText(headerText);
    candidateDetailLabel_->setToolTip(tr(
        "%1\n%2\nTemporary optimization evaluation; live result windows remain separate.")
        .arg(candidateIdentity,
            breakdown.isEmpty() ? tr("Objective breakdown unavailable")
                                : breakdown.join(tr(" · "))));
    updateCandidateActionState();
}

void OptimizationWorkspace::resetCandidateDetails()
{
    detailCandidateRow_ = -1;
    candidateDetailsTable_->setRowCount(0);
    candidateDetailLabel_->setText(
        tr("Select a completed candidate to inspect its results."));
    candidateDetailLabel_->setToolTip({});
    updateCandidateActionState();
}

void OptimizationWorkspace::updateCandidateActionState()
{
    const auto candidateIndexValid = detailCandidateRow_ >= 0
        && static_cast<std::size_t>(detailCandidateRow_) < candidates_.size();
    const auto* candidate = candidateIndexValid
        ? &candidates_[static_cast<std::size_t>(detailCandidateRow_)] : nullptr;
    const auto validCandidate = candidate != nullptr && candidate->evaluation.has_value();
    const auto editable = validCandidate && !historicalSession_ && modelValid_ && !isRunning();
    applyCandidateButton_->setEnabled(editable && static_cast<bool>(applyParameterCallback_));
    applyCandidateAndRunButton_->setEnabled(editable && static_cast<bool>(applyAndRunCallback_));
}

void OptimizationWorkspace::showCandidateContextMenu(const QPoint& position)
{
    const auto index = resultsTable_->indexAt(position);
    if (!index.isValid()) return;
    const auto row = index.row();
    const auto candidateIndex = candidateIndexAtTableRow(row);
    if (candidateIndex < 0
        || static_cast<std::size_t>(candidateIndex) >= candidates_.size()) return;
    resultsTable_->selectRow(row);

    const auto& candidate = candidates_[static_cast<std::size_t>(candidateIndex)];
    const auto hasDetails = candidate.evaluation.has_value()
        || candidate.availableMeasurements.has_value() || !candidate.feedpoints.empty();
    const auto editable = candidate.evaluation.has_value() && !historicalSession_
        && modelValid_ && !isRunning();

    QMenu menu(resultsTable_);
    menu.setToolTipsVisible(true);
    auto* inspectAction = menu.addAction(tr("Open Candidate Results"));
    inspectAction->setEnabled(hasDetails);
    menu.addSeparator();
    auto* applyAction = menu.addAction(tr("Apply Candidate to Model"));
    applyAction->setEnabled(editable && static_cast<bool>(applyParameterCallback_));
    auto* applyAndRunAction = menu.addAction(tr("Apply Candidate and Run Analysis"));
    applyAndRunAction->setEnabled(editable && static_cast<bool>(applyAndRunCallback_));

    const auto* selected = menu.exec(resultsTable_->viewport()->mapToGlobal(position));
    if (selected == inspectAction) showCandidateDetails(candidateIndex);
    else if (selected == applyAction) applyCandidate(candidateIndex, false);
    else if (selected == applyAndRunAction) applyCandidate(candidateIndex, true);
}

void OptimizationWorkspace::sortCandidateTable(int column)
{
    if (isRunning() || column < ValueColumn || column > FrontToRearColumn) return;
    const auto naturalOrder = column >= GainColumn && column <= FrontToRearColumn
        ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (candidateSortColumn_ == column) {
        candidateSortOrder_ = candidateSortOrder_ == Qt::AscendingOrder
            ? Qt::DescendingOrder : Qt::AscendingOrder;
    } else {
        candidateSortColumn_ = column;
        candidateSortOrder_ = naturalOrder;
    }
    applyCandidateSort();
}

void OptimizationWorkspace::applyCandidateSort()
{
    if (candidateSortColumn_ < ValueColumn || candidateSortColumn_ > FrontToRearColumn)
        return;
    resultsTable_->horizontalHeader()->setSortIndicator(
        candidateSortColumn_, candidateSortOrder_);
    resultsTable_->horizontalHeader()->setSortIndicatorShown(true);
    resultsTable_->sortItems(candidateSortColumn_, candidateSortOrder_);
}

auto OptimizationWorkspace::candidateIndexAtTableRow(int row) const -> int
{
    if (row < 0 || row >= resultsTable_->rowCount()) return -1;
    const auto* item = resultsTable_->item(row, ValueColumn);
    if (item == nullptr) return -1;
    auto valid = false;
    const auto candidateIndex = item->data(CandidateIndexRole).toInt(&valid);
    return valid ? candidateIndex : -1;
}

auto OptimizationWorkspace::tableRowForCandidateIndex(int candidateIndex) const -> int
{
    for (auto row = 0; row < resultsTable_->rowCount(); ++row) {
        if (candidateIndexAtTableRow(row) == candidateIndex) return row;
    }
    return -1;
}

auto OptimizationWorkspace::candidateParameterValues(int row) const
    -> std::vector<std::pair<QString, double>>
{
    std::vector<std::pair<QString, double>> values;
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()) return values;
    const auto& candidate = candidates_[static_cast<std::size_t>(row)];
    values.reserve(std::min(activeVariables_.size(), candidate.values.size()));
    for (auto index = std::size_t{};
         index < activeVariables_.size() && index < candidate.values.size(); ++index) {
        values.emplace_back(activeVariables_[index].name, candidate.values[index]);
    }
    return values;
}

void OptimizationWorkspace::applyCandidate(int row, bool runAfterApply)
{
    const auto values = candidateParameterValues(row);
    if (values.empty()) return;
    if (runAfterApply) {
        if (!applyAndRunCallback_) return;
        preserveStudyOnNextContextUpdate_ = true;
        if (applyAndRunCallback_(values))
            statusLabel_->setText(
                tr("Applied the selected candidate and started a normal analysis."));
        preserveStudyOnNextContextUpdate_ = false;
        return;
    }
    preserveStudyOnNextContextUpdate_ = true;
    const auto applied = applyParameterCallback_ && applyParameterCallback_(values);
    preserveStudyOnNextContextUpdate_ = false;
    if (applied) {
        const auto appliedText = row == bestRow_
            ? tr("Applied the best parameter values to the active model.")
            : tr("Applied the selected candidate values to the active model.");
        statusLabel_->setText(directionSetupReady_
            ? tr("%1 The model changed; the optimization baseline will be rebuilt before the next optimization.")
                .arg(appliedText)
            : maximumGainDirectionStale_
                ? tr("%1 The fixed direction came from the previous pattern; update the sampled peak or choose a direction before optimizing again.")
                    .arg(appliedText)
                : appliedText);
    }
}

void OptimizationWorkspace::updateCandidateRowToolTip(int row)
{
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()) return;
    const auto& candidate = candidates_[static_cast<std::size_t>(row)];
    const auto tableRow = tableRowForCandidateIndex(row);
    if (tableRow < 0) return;
    const auto hasDetails = candidate.evaluation.has_value()
        || candidate.availableMeasurements.has_value()
        || candidate.record.hasImpedance || candidate.record.hasRadiation;
    const auto* statusItem = resultsTable_->item(tableRow, StatusColumn);
    const auto inProgress = statusItem != nullptr
        && (statusItem->text() == tr("Pending") || statusItem->text() == tr("Running"));
    auto toolTip = hasDetails
        ? tr("Double-click to inspect this candidate. Right-click to apply it or apply and run.")
        : inProgress
            ? tr("Frequency details will be available after this candidate completes.")
            : tr("Frequency details are unavailable because this candidate did not complete with impedance results.");
    if (candidate.evaluation) {
        toolTip += tr("\nFull objective score: %1.")
            .arg(candidate.evaluation->score, 0, 'f', 6);
        toolTip += candidate.evaluation->evaluatedFrequencyCount == 1
            ? tr("\nObjective evaluated at %1 MHz.")
                .arg(formatDecimal(candidate.evaluation->evaluationFrequencyMHz))
            : tr("\nEach enabled criterion uses its configured frequency reduction across %1 frequencies.")
                .arg(candidate.evaluation->evaluatedFrequencyCount);
        if (candidate.evaluation->forwardGainDb)
            toolTip += tr(" Reduced forward gain: %1 dBi.")
                .arg(formatDecimal(*candidate.evaluation->forwardGainDb));
        if (candidate.evaluation->frontToBackDb)
            toolTip += tr(" Reduced F/B: %1 dB.")
                .arg(formatDecimal(*candidate.evaluation->frontToBackDb));
        if (candidate.evaluation->frontToRearDb)
            toolTip += tr(" Reduced F/R: %1 dB.")
                .arg(formatDecimal(*candidate.evaluation->frontToRearDb));
    }
    if (!candidate.evaluation && candidate.availableMeasurements)
        toolTip += tr("\nAvailable measurements are shown, but this candidate has no objective score.");
    if (!candidate.failureDetail.isEmpty())
        toolTip += tr("\nFailure: %1").arg(candidate.failureDetail);
    for (auto column = 0; column < resultsTable_->columnCount(); ++column) {
        if (auto* item = resultsTable_->item(tableRow, column)) item->setToolTip(toolTip);
    }
}

void OptimizationWorkspace::updateCandidatePlots()
{
    const auto multivariable = activeVariables_.size() > 1;
    std::vector<CandidatePlotPoint> points;
    points.reserve(candidates_.size());
    for (const auto& candidate : candidates_) {
        if (!candidate.evaluation) continue;
        points.push_back({candidate.row,
            multivariable ? static_cast<double>(candidate.row + 1) : candidate.value,
            *candidate.evaluation});
    }
    candidatePlots_->setCandidates(multivariable ? tr("Evaluation Number") : selectedSymbol_,
        multivariable ? QString{} : selectedValueSuffix_, points, bestRow_);
}

void OptimizationWorkspace::updateComparisonCandidate()
{
    const auto candidateIndex = candidateIndexAtTableRow(resultsTable_->currentRow());
    if (candidateIndex < 0
        || static_cast<std::size_t>(candidateIndex) >= candidates_.size()) {
        comparePanel_->clearCandidate();
        return;
    }
    auto& candidate = candidates_[static_cast<std::size_t>(candidateIndex)];
    ensureCandidateAnalysisLoaded(candidate);
    if (candidate.feedpoints.empty() && candidate.radiation.empty()) {
        comparePanel_->clearCandidate();
        return;
    }
    analysis::AnalysisResult result;
    result.feedpoints = candidate.feedpoints;
    result.radiation = candidate.radiation;
    result.referenceImpedanceOhms = activeObjective_.referenceImpedance;
    const auto candidateIdentity = tr("Candidate %1 — %2")
        .arg(candidateIndex + 1)
        .arg(candidateDescription(candidate));
    auto breakdown = candidateIdentity;
    QString comparisonBreakdown;
    std::optional<analysis::OptimizationObjectiveResult> baselineEvaluation;
    if (candidate.evaluation) {
        breakdown += tr("\nObjective score: %1\nEvaluated frequencies: %2")
            .arg(formatDecimal(candidate.evaluation->score))
            .arg(candidate.evaluation->evaluatedFrequencyCount);
        if (candidate.evaluation->feedpoint) {
            breakdown += tr("\nReduced SWR: %1\nReduced impedance: %2 %3 j%4 Ω")
                .arg(formatDecimal(candidate.evaluation->swr))
                .arg(formatDecimal(candidate.evaluation->feedpoint->impedance.real()))
                .arg(candidate.evaluation->feedpoint->impedance.imag() < 0.0
                    ? QStringLiteral("−") : QStringLiteral("+"))
                .arg(formatDecimal(std::abs(
                    candidate.evaluation->feedpoint->impedance.imag())));
        }
        if (candidate.evaluation->forwardGainDb)
            breakdown += tr("\nReduced gain: %1 dBi")
                .arg(formatDecimal(*candidate.evaluation->forwardGainDb));
        if (candidate.evaluation->frontToBackDb)
            breakdown += tr("\nReduced F/B: %1 dB")
                .arg(formatDecimal(*candidate.evaluation->frontToBackDb));
        if (candidate.evaluation->frontToBackDb
            && std::abs(*candidate.evaluation->frontToBackDb) < 0.005) {
            breakdown += tr(
                "\nF/B is 0 dB because the tracked forward lobe and its physical opposite have equal gain.");
        }
        if (candidate.evaluation->frontToRearDb)
            breakdown += tr("\nReduced F/R: %1 dB")
                .arg(formatDecimal(*candidate.evaluation->frontToRearDb));

        if (baselineCompatibility_.compatible() && baselineRecord_ && baselineResult_) {
            baselineEvaluation = analysis::evaluateOptimizationObjective(
                *baselineResult_, activeObjective_);
            if (baselineEvaluation) {
                const auto signedDelta = [](double value) {
                    return QStringLiteral("%1%2")
                        .arg(value >= 0.0 ? QStringLiteral("+") : QString{})
                        .arg(formatDecimal(value));
                };
                const auto reducedFrequency = [](const auto& evaluation,
                                                  const auto& summary,
                                                  analysis::OptimizationAggregation aggregation)
                    -> std::optional<double> {
                    if (evaluation.evaluatedFrequencyCount == 1)
                        return evaluation.evaluationFrequencyMHz;
                    if (!summary || aggregation == analysis::OptimizationAggregation::Average)
                        return std::nullopt;
                    return aggregation == analysis::OptimizationAggregation::Minimum
                        ? summary->minimumFrequencyMHz : summary->maximumFrequencyMHz;
                };
                const auto frequencyContext = [](std::optional<double> candidateFrequency,
                                                  std::optional<double> baselineFrequency,
                                                  std::size_t frequencyCount) {
                    if (candidateFrequency && baselineFrequency) {
                        if (nearlyEqual(*candidateFrequency, *baselineFrequency))
                            return tr(" at %1 MHz").arg(formatDecimal(*candidateFrequency));
                        return tr(" · candidate %1 MHz, baseline %2 MHz")
                            .arg(formatDecimal(*candidateFrequency),
                                formatDecimal(*baselineFrequency));
                    }
                    return tr(" across %1 frequencies").arg(frequencyCount);
                };
                comparisonBreakdown += tr("Objective score Δ %1")
                    .arg(signedDelta(candidate.evaluation->score
                        - baselineEvaluation->score));
                if (activeObjective_.swrWeight > 0.0) {
                    const auto candidateFrequency = reducedFrequency(*candidate.evaluation,
                        candidate.evaluation->swrSummary, activeObjective_.swrAggregation);
                    const auto baselineFrequency = reducedFrequency(*baselineEvaluation,
                        baselineEvaluation->swrSummary, activeObjective_.swrAggregation);
                    comparisonBreakdown += tr("\nSWR Δ %1%2")
                        .arg(signedDelta(candidate.evaluation->swr
                            - baselineEvaluation->swr),
                            frequencyContext(candidateFrequency, baselineFrequency,
                                candidate.evaluation->evaluatedFrequencyCount));
                }
                if (candidate.evaluation->feedpoint && baselineEvaluation->feedpoint) {
                    const auto candidateFrequency = candidate.evaluation->feedpoint->frequencyMHz;
                    const auto baselineFrequency = baselineEvaluation->feedpoint->frequencyMHz;
                    const auto context = frequencyContext(candidateFrequency,
                        baselineFrequency, candidate.evaluation->evaluatedFrequencyCount);
                    if (activeObjective_.resistanceWeight > 0.0) {
                        comparisonBreakdown += tr("\nR Δ %1 Ω%2")
                            .arg(signedDelta(candidate.evaluation->feedpoint->impedance.real()
                                - baselineEvaluation->feedpoint->impedance.real()), context);
                    }
                    if (activeObjective_.reactanceWeight > 0.0) {
                        comparisonBreakdown += tr("\nX Δ %1 Ω%2")
                            .arg(signedDelta(candidate.evaluation->feedpoint->impedance.imag()
                                - baselineEvaluation->feedpoint->impedance.imag()), context);
                    }
                }
                const auto addDirectionalDelta = [&](const QString& name, double weight,
                                                     const auto& candidateValue,
                                                     const auto& baselineValue,
                                                     const auto& candidateFrequency,
                                                     const auto& baselineFrequency) {
                    if (weight <= 0.0 || !candidateValue || !baselineValue) return;
                    comparisonBreakdown += tr("\n%1 Δ %2 dB%3")
                        .arg(name,
                            signedDelta(*candidateValue - *baselineValue),
                            frequencyContext(candidateFrequency, baselineFrequency,
                                candidate.evaluation->evaluatedFrequencyCount));
                };
                addDirectionalDelta(tr("Gain"), activeObjective_.forwardGainWeight,
                    candidate.evaluation->forwardGainDb, baselineEvaluation->forwardGainDb,
                    candidate.evaluation->forwardGainFrequencyMHz,
                    baselineEvaluation->forwardGainFrequencyMHz);
                addDirectionalDelta(tr("F/B"), activeObjective_.frontToBackWeight,
                    candidate.evaluation->frontToBackDb, baselineEvaluation->frontToBackDb,
                    candidate.evaluation->frontToBackFrequencyMHz,
                    baselineEvaluation->frontToBackFrequencyMHz);
                addDirectionalDelta(tr("F/R"), activeObjective_.frontToRearWeight,
                    candidate.evaluation->frontToRearDb, baselineEvaluation->frontToRearDb,
                    candidate.evaluation->frontToRearFrequencyMHz,
                    baselineEvaluation->frontToRearFrequencyMHz);
            } else {
                comparisonBreakdown = tr(
                    "Deltas unavailable: the saved starting model cannot be evaluated with the current objective.");
            }
        }
    } else if (!candidate.failureDetail.isEmpty()) {
        breakdown += tr("\nObjective unavailable: %1").arg(candidate.failureDetail);
    }
    comparePanel_->setCandidate(result,
        candidateIdentity, breakdown, comparisonBreakdown,
        candidate.evaluation ? candidate.evaluation : candidate.availableMeasurements,
        baselineEvaluation);
}

void OptimizationWorkspace::setCandidateStatus(int row, const QString& status)
{
    auto displayStatus = status;
    if (row >= 0 && static_cast<std::size_t>(row) < candidates_.size()) {
        const auto& candidate = candidates_[static_cast<std::size_t>(row)];
        if (candidate.trialRole.startsWith(QStringLiteral("initial")))
            displayStatus = tr("Initial sample · %1").arg(status);
        else if (!candidate.trialRole.isEmpty())
            displayStatus = tr("Iteration %1 · %2 · %3")
                .arg(candidate.refinementRound)
                .arg(candidate.trialRole)
                .arg(status);
    }
    resultsTable_->setItem(row, StatusColumn, new QTableWidgetItem(displayStatus));
    if (row >= 0 && static_cast<std::size_t>(row) < candidates_.size()
        && !candidates_[static_cast<std::size_t>(row)].record.id.isEmpty()) {
        resultsTable_->setItem(row, RunColumn,
            new QTableWidgetItem(candidates_[static_cast<std::size_t>(row)].record.id));
    }
    updateCandidateRowToolTip(row);
}

}
