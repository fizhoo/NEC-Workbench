#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/DisplayFormat.h"
#include "ui/optimization/DirectionStrategyDialog.h"
#include "ui/optimization/OptimizationWorkspaceSupport.h"

#include "analysis/AnalysisResult.h"
#include "analysis/FrequencyComparison.h"
#include "analysis/OptimizationPreset.h"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <vector>

namespace necwb::ui {
using namespace detail;

namespace {

auto selectedGoodEnoughDirection(const QComboBox* control)
    -> analysis::GoodEnoughDirection
{
    return control->currentIndex() == 4 ? analysis::GoodEnoughDirection::AtLeast
                                        : analysis::GoodEnoughDirection::AtMost;
}

auto goalIndex(analysis::OptimizationGoal goal,
    analysis::GoodEnoughDirection direction) -> int
{
    if (goal == analysis::OptimizationGoal::GoodEnough)
        return direction == analysis::GoodEnoughDirection::AtLeast ? 4 : 3;
    return static_cast<int>(goal);
}

auto selectedAggregation(const QComboBox* control) -> analysis::OptimizationAggregation
{
    return static_cast<analysis::OptimizationAggregation>(control->currentData().toInt());
}

}

auto OptimizationWorkspace::selectedObjectivePreset() const
    -> analysis::OptimizationPresetId
{
    if (objectivePresetControl_ == nullptr || objectivePresetControl_->currentIndex() < 0)
        return analysis::OptimizationPresetId::Custom;
    return static_cast<analysis::OptimizationPresetId>(
        objectivePresetControl_->currentData().toInt());
}

auto OptimizationWorkspace::directionalPresetRequiresSetup() const -> bool
{
    const auto preset = selectedObjectivePreset();
    if (preset == analysis::OptimizationPresetId::Custom) return false;
    const auto definitions = analysis::optimizationPresetDefinitions();
    const auto definition = std::ranges::find(definitions, preset,
        &analysis::OptimizationPresetDefinition::id);
    return definition != definitions.end() && definition->requiresDirectionalSetup;
}

void OptimizationWorkspace::markObjectiveCustom()
{
    if (applyingObjectivePreset_
        || selectedObjectivePreset() == analysis::OptimizationPresetId::Custom)
        return;
    const QSignalBlocker blocker(objectivePresetControl_);
    objectivePresetControl_->setCurrentIndex(objectivePresetControl_->findData(
        static_cast<int>(analysis::OptimizationPresetId::Custom)));
}

void OptimizationWorkspace::applySelectedObjectivePreset()
{
    if (applyingObjectivePreset_) return;
    const auto preset = selectedObjectivePreset();
    if (preset == analysis::OptimizationPresetId::Custom) {
        updateObjectiveControls();
        updateWorkload();
        updateReadiness();
        return;
    }

    const auto current = selectedObjective();
    const auto currentDirectional = current.forwardGainWeight > 0.0
        || current.frontToBackWeight > 0.0 || current.frontToRearWeight > 0.0;
    const auto presetObjective = analysis::optimizationPresetObjective(preset,
        {referenceImpedanceControl_->value(), presetSwrLimitControl_->value()});
    if (!presetObjective) return;
    auto objective = *presetObjective;
    objective.forwardThetaDegrees = current.forwardThetaDegrees;
    objective.forwardPhiDegrees = current.forwardPhiDegrees;
    objective.directionalEvaluationMode = current.directionalEvaluationMode;
    objective.radiationComponent = current.radiationComponent;
    if (directionalPresetRequiresSetup()
        && current.directionalEvaluationMode == analysis::DirectionalEvaluationMode::SpecifiedDirection
        && !directionSetupConfigured_) {
        objective.directionalEvaluationMode =
            analysis::DirectionalEvaluationMode::StrongestInAzimuthCut;
    }

    applyingObjectivePreset_ = true;
    restoreObjectiveSetup(objective);
    applyingObjectivePreset_ = false;
    if (directionalPresetRequiresSetup() && !currentDirectional)
        directionSetupReady_ = false;
    updateObjectiveControls();
    if (directionalPresetRequiresSetup() && !directionSetupReady_)
        (void)seedDirectionFromLatestPattern();
    updateDirectionalSeedState();
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::updateObjectivePresetControls()
{
    const auto preset = selectedObjectivePreset();
    const auto custom = preset == analysis::OptimizationPresetId::Custom;
    const auto swrLimit = preset == analysis::OptimizationPresetId::SwrLimit
        || preset == analysis::OptimizationPresetId::SwrAndGain;
    const auto objective = selectedObjective();
    const auto directional = objective.forwardGainWeight > 0.0
        || objective.frontToBackWeight > 0.0 || objective.frontToRearWeight > 0.0;

    objectiveCriteriaTable_->setVisible(custom);
    presetSwrLimitLabel_->setVisible(swrLimit);
    presetSwrLimitControl_->setVisible(swrLimit);
    directionSetupLabel_->setVisible(directional);
    directionSetupPanel_->setVisible(directional);
    radiationComponentLabel_->setVisible(directional);
    radiationComponentControl_->setVisible(directional);
    latestPatternPeakFieldLabel_->setVisible(directional);
    latestPatternPeakPanel_->setVisible(directional);
    directionalFrequencyNote_->setVisible(custom);
    resetObjectiveDefaultsButton_->setText(custom
        ? tr("Restore Objective Defaults") : tr("Reset Goal"));
}

void OptimizationWorkspace::updateObjectiveControls()
{
    const auto objective = selectedObjective();
    const auto directional = objective.forwardGainWeight > 0.0
        || objective.frontToBackWeight > 0.0
        || objective.frontToRearWeight > 0.0;
    directionalFrequencyNote_->setText(tr(
        "Each criterion independently applies its Goal to the selected Minimum, Average, "
        "or Maximum across the frequency plan. Weight zero disables that row."));
    frequencyModeControl_->setToolTip(tr("Choose which frequencies are calculated for every candidate."));
    const auto editable = !isRunning() && !historicalSession_;
    for (auto row = 0; row < 6; ++row) {
        const auto goal = selectedGoal(objectiveGoalControls_[row]);
        objectiveGoalControls_[row]->setToolTip(
            objectiveGoalControls_[row]->currentData(Qt::ToolTipRole).toString());
        objectiveAggregationControls_[row]->setToolTip(
            objectiveAggregationControls_[row]
                ->currentData(Qt::ToolTipRole).toString());
        objectiveValueControls_[row]->setEnabled(editable
            && (goal == analysis::OptimizationGoal::Target
                || goal == analysis::OptimizationGoal::GoodEnough));
    }
    resultsTable_->horizontalHeaderItem(ScoreColumn)->setText(tr("Objective Score"));
    const auto fixedDirection = objective.directionalEvaluationMode
        == analysis::DirectionalEvaluationMode::SpecifiedDirection;
    const auto azimuthCut = objective.directionalEvaluationMode
        == analysis::DirectionalEvaluationMode::StrongestInAzimuthCut;
    const auto trackedCut = !fixedDirection;
    directionSetupButton_->setEnabled(editable);
    QString directionDescription;
    if (azimuthCut) {
        directionDescription = tr("Azimuth pattern · strongest gain · elevation %1°")
            .arg(formatDecimal(90.0 - objective.forwardThetaDegrees));
    } else if (!fixedDirection) {
        directionDescription = tr("Elevation pattern · strongest gain · azimuth %1°")
            .arg(formatDecimal(objective.forwardPhiDegrees));
    } else {
        directionDescription = tr("Specific direction · elevation %1°, azimuth %2°")
            .arg(formatDecimal(90.0 - objective.forwardThetaDegrees),
                formatDecimal(objective.forwardPhiDegrees));
    }
    directionSetupSummaryLabel_->setText(directionalPresetRequiresSetup()
        ? (directionSetupReady_ ? tr("Direction ready · ") : tr("Direction setup required · "))
            + directionDescription
        : directionDescription);
    directionSetupSummaryLabel_->setStyleSheet(
        directionalPresetRequiresSetup() && !directionSetupReady_
            ? QStringLiteral("color: #9a3030;") : QString{});
    radiationComponentControl_->setEnabled(directional && !isRunning() && !historicalSession_);
    auto* gainCriterion = objectiveCriteriaTable_->item(3, 0);
    gainCriterion->setText(trackedCut
        ? tr("Peak Gain in Cut (dBi)") : tr("Gain at Direction (dBi)"));
    gainCriterion->setToolTip(trackedCut
        ? tr("Highest gain found in the selected azimuth or elevation cut.")
        : tr("Gain at the specified physical elevation and azimuth."));
    resultsTable_->horizontalHeaderItem(GainColumn)->setText(trackedCut
        ? tr("Peak Gain (dBi)") : tr("Gain (dBi)"));
    resultsTable_->horizontalHeaderItem(GainColumn)->setToolTip(trackedCut
        ? tr("Highest gain found in the selected cut for this candidate.")
        : tr("Gain at the specified elevation, azimuth, frequency, and radiation component."));
    // F/R depends on the evaluation mode; say which rule is active.
    const auto frontToRear = fromCore(analysis::frontToRearDefinition(
        objective.directionalEvaluationMode));
    objectiveCriteriaTable_->item(5, 0)->setToolTip(frontToRear);
    resultsTable_->horizontalHeaderItem(FrontToRearColumn)->setToolTip(frontToRear);
    updateObjectivePresetControls();
    updateLatestPatternPeakControls();
    updateFrequencyControls();
}

void OptimizationWorkspace::updateLatestPatternPeakControls()
{
    const auto setup = currentStudySetup();
    const auto targetFrequencyMHz = setup.frequenciesMHz.empty()
        ? 0.0 : setup.frequenciesMHz.front();
    const auto component = static_cast<analysis::RadiationComponent>(
        radiationComponentControl_->currentData().toInt());
    const auto peak = latestPatternPeak(component, targetFrequencyMHz);
    const auto editable = !isRunning() && !historicalSession_;
    const auto usable = peak && peak->sampleCount > 1;
    findMaximumGainButton_->setText(maximumGainSearchRequested_
            ? tr("Finding Maximum…")
            : usable || maximumGainDirectionStale_
                ? tr("Update Maximum…") : tr("Find Maximum Gain…"));
    findMaximumGainButton_->setEnabled(editable && !externalRunActive_
        && modelValid_ && static_cast<bool>(findMaximumGainCallback_)
        && !maximumGainSearchRequested_);
    useLatestPatternPeakButton_->setEnabled(editable && usable);
    const auto mode = directionalEvaluationMode_;
    useLatestPatternPeakButton_->setText(mode
            == analysis::DirectionalEvaluationMode::SpecifiedDirection
        ? tr("Use Latest Pattern Peak") : tr("Use Latest Peak to Set Cut"));
    if (usable) {
        auto status = tr("%1 · %2 MHz")
            .arg(fromCore(analysis::formatGain({.gainDb = peak->metrics.peakGainDb,
                .thetaDegrees = peak->metrics.peakThetaDegrees,
                .phiDegrees = peak->metrics.peakPhiDegrees, .component = component,
                .frequencyMHz = peak->frequencyMHz, .sampleStepDegrees = std::nullopt}).text))
            .arg(formatDecimal(peak->frequencyMHz));
        if (mode != analysis::DirectionalEvaluationMode::SpecifiedDirection) {
            const auto cutAngle = mode
                    == analysis::DirectionalEvaluationMode::StrongestInAzimuthCut
                ? forwardThetaDegrees_ : forwardPhiDegrees_;
            const auto& cutMetrics = latestTrackedCutMetrics(component, mode, cutAngle);
            const auto nearest = std::ranges::min_element(cutMetrics, {},
                [targetFrequencyMHz](const auto& metrics) {
                    return std::abs(metrics.frequencyMHz - targetFrequencyMHz);
                });
            if (nearest != cutMetrics.end()
                && nearest->forwardAvailability
                    == analysis::RadiationMetricAvailability::PatternNull) {
                status += mode
                        == analysis::DirectionalEvaluationMode::StrongestInAzimuthCut
                    ? tr(" · Warning: %1 contains only pattern nulls; latest peak suggests El %2")
                          .arg(fromCore(analysis::azimuthCutName(cutAngle)),
                              fromCore(analysis::formatAngle(analysis::patternDirection(
                                  peak->metrics.peakThetaDegrees, 0.0).elevationDegrees)))
                    : tr(" · Warning: %1 contains only pattern nulls; latest peak suggests Az %2")
                          .arg(fromCore(analysis::elevationCutName(cutAngle)),
                              fromCore(analysis::formatAngle(analysis::patternDirection(
                                  90.0, peak->metrics.peakPhiDegrees).azimuthDegrees)));
            }
        }
        latestPatternPeakLabel_->setText(status);
        return;
    }
    if (directionalPresetRequiresSetup()) {
        latestPatternPeakLabel_->setText(directionSetupReady_
            ? tr("Direction is configured. Find Maximum Gain can replace it with a calculated peak.")
            : maximumGainDirectionStale_
                ? tr("Direction setup required. The previous maximum is stale; use Find Maximum Gain or Choose.")
                : tr("Direction setup required. Use Find Maximum Gain or Choose."));
        return;
    }
    latestPatternPeakLabel_->setText(maximumGainDirectionStale_
        ? tr("Maximum-gain direction is stale because the model changed. Run again to update it.")
        : latestRadiation_.empty()
            ? tr("Run the active model with a radiation pattern first.")
            : tr("The latest run has no usable multi-angle pattern near the optimization frequencies."));
}

void OptimizationWorkspace::updateDirectionalSeedState()
{
    const auto objective = selectedObjective();
    const auto directional = objective.forwardGainWeight > 0.0
        || objective.frontToBackWeight > 0.0
        || objective.frontToRearWeight > 0.0;
    if (!directional) {
        directionalObjectiveWasEnabled_ = false;
        return;
    }
    if (directionalObjectiveWasEnabled_) return;
    directionalObjectiveWasEnabled_ = true;
    if (objective.directionalEvaluationMode
        != analysis::DirectionalEvaluationMode::SpecifiedDirection)
        (void)seedDirectionFromLatestPattern();
}

void OptimizationWorkspace::setLatestRadiation(std::vector<analysis::RadiationSample> radiation)
{
    latestRadiation_ = std::move(radiation);
    latestPatternPeakCache_.reset();
    latestTrackedCutCache_.reset();
}

auto OptimizationWorkspace::latestPatternPeak(analysis::RadiationComponent component,
    double frequencyMHz) -> const std::optional<analysis::RadiationPeak>&
{
    if (!latestPatternPeakCache_ || latestPatternPeakCache_->component != component
        || latestPatternPeakCache_->frequencyMHz != frequencyMHz) {
        latestPatternPeakCache_ = LatestPatternPeak{component, frequencyMHz,
            analysis::radiationPeakAtNearestFrequency(latestRadiation_, component, frequencyMHz)};
    }
    return latestPatternPeakCache_->peak;
}

auto OptimizationWorkspace::latestTrackedCutMetrics(analysis::RadiationComponent component,
    analysis::DirectionalEvaluationMode mode, double cutAngleDegrees)
    -> const std::vector<analysis::RadiationFrequencyMetric>&
{
    if (!latestTrackedCutCache_ || latestTrackedCutCache_->component != component
        || latestTrackedCutCache_->mode != mode
        || latestTrackedCutCache_->cutAngleDegrees != cutAngleDegrees) {
        latestTrackedCutCache_ = LatestTrackedCut{component, mode, cutAngleDegrees,
            analysis::radiationTrackedCutFrequencyMetrics(
                latestRadiation_, component, mode, cutAngleDegrees)};
    }
    return latestTrackedCutCache_->metrics;
}

auto OptimizationWorkspace::seedDirectionFromLatestPattern() -> bool
{
    const auto setup = currentStudySetup();
    if (setup.frequenciesMHz.empty()) return false;
    const auto component = static_cast<analysis::RadiationComponent>(
        radiationComponentControl_->currentData().toInt());
    const auto peak = latestPatternPeak(component, setup.frequenciesMHz.front());
    if (!peak || peak->sampleCount <= 1) return false;
    const auto mode = directionalEvaluationMode_;
    setDirection(mode,
        mode != analysis::DirectionalEvaluationMode::StrongestInElevationCut
            ? peak->metrics.peakThetaDegrees : forwardThetaDegrees_,
        mode != analysis::DirectionalEvaluationMode::StrongestInAzimuthCut
            ? peak->metrics.peakPhiDegrees : forwardPhiDegrees_);
    directionSetupReady_ = true;
    updateObjectiveControls();
    return true;
}

void OptimizationWorkspace::chooseDirectionStrategy()
{
    const auto wasConfigured = directionSetupConfigured_;
    if (!directionSetupConfigured_) (void)seedDirectionFromLatestPattern();
    const auto setup = currentStudySetup();
    const auto targetFrequencyMHz = !setup.frequenciesMHz.empty()
        ? setup.frequenciesMHz.front()
        : modelFrequenciesMHz_.empty() ? 0.0 : modelFrequenciesMHz_.front();
    DirectionStrategyDialog dialog(latestRadiation_, targetFrequencyMHz,
        static_cast<analysis::RadiationComponent>(
            radiationComponentControl_->currentData().toInt()),
        directionalEvaluationMode_, forwardThetaDegrees_, forwardPhiDegrees_,
        objectiveDialog_);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto changed = dialog.mode() != directionalEvaluationMode_
            || !nearlyEqual(dialog.thetaDegrees(), forwardThetaDegrees_)
            || !nearlyEqual(dialog.phiDegrees(), forwardPhiDegrees_);
    directionSetupConfigured_ = dialog.usesLatestPatternPeak()
        ? false : changed || wasConfigured;
    directionSetupReady_ = true;
    setDirection(dialog.mode(), dialog.thetaDegrees(), dialog.phiDegrees());
}

void OptimizationWorkspace::setDirection(analysis::DirectionalEvaluationMode mode,
    double thetaDegrees, double phiDegrees)
{
    directionalEvaluationMode_ = mode;
    forwardThetaDegrees_ = thetaDegrees;
    forwardPhiDegrees_ = phiDegrees;
    updateObjectiveControls();
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::useLatestPatternPeak()
{
    directionSetupReady_ = seedDirectionFromLatestPattern();
    updateObjectiveControls();
    updateReadiness();
}

void OptimizationWorkspace::editObjectiveSetup()
{
    const auto original = selectedObjective();
    const auto originalPreset = selectedObjectivePreset();
    const auto originalSwrLimit = presetSwrLimitControl_->value();
    if (objectiveDialog_->exec() != QDialog::Accepted) {
        applyingObjectivePreset_ = true;
        restoreObjectiveSetup(original);
        presetSwrLimitControl_->setValue(originalSwrLimit);
        objectivePresetControl_->setCurrentIndex(objectivePresetControl_->findData(
            static_cast<int>(originalPreset)));
        applyingObjectivePreset_ = false;
    }
    updateObjectiveControls();
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::restoreObjectiveSetup(
    const analysis::OptimizationObjectiveSpec& objective)
{
    // Every caller refreshes once afterwards, so silence the ~24 per-control refreshes.
    // One blocker per object: the resistance and reactance targets are value controls 1 and 2.
    std::vector<QSignalBlocker> blockers;
    for (QObject* control : {static_cast<QObject*>(referenceImpedanceControl_),
             static_cast<QObject*>(swrWeightControl_),
             static_cast<QObject*>(resistanceWeightControl_),
             static_cast<QObject*>(reactanceWeightControl_),
             static_cast<QObject*>(forwardGainWeightControl_),
             static_cast<QObject*>(frontToBackWeightControl_),
             static_cast<QObject*>(frontToRearWeightControl_),
             static_cast<QObject*>(radiationComponentControl_)})
        blockers.emplace_back(control);
    for (auto row = 0; row < 6; ++row) {
        blockers.emplace_back(objectiveGoalControls_[row]);
        blockers.emplace_back(objectiveValueControls_[row]);
        blockers.emplace_back(objectiveAggregationControls_[row]);
    }
    referenceImpedanceControl_->setValue(objective.referenceImpedance);
    swrWeightControl_->setValue(objective.swrWeight);
    resistanceWeightControl_->setValue(objective.resistanceWeight);
    resistanceTargetControl_->setValue(objective.resistanceTargetOhms);
    reactanceWeightControl_->setValue(objective.reactanceWeight);
    reactanceTargetControl_->setValue(objective.reactanceTargetOhms);
    forwardGainWeightControl_->setValue(objective.forwardGainWeight);
    frontToBackWeightControl_->setValue(objective.frontToBackWeight);
    frontToRearWeightControl_->setValue(objective.frontToRearWeight);
    // The directional weight signals used to run this here, before the direction is restored.
    updateDirectionalSeedState();
    const std::array goals{objective.swrGoal, objective.resistanceGoal,
        objective.reactanceGoal, objective.forwardGainGoal,
        objective.frontToBackGoal, objective.frontToRearGoal};
    const std::array directions{objective.swrGoodEnoughDirection,
        objective.resistanceGoodEnoughDirection, objective.reactanceGoodEnoughDirection,
        objective.forwardGainGoodEnoughDirection, objective.frontToBackGoodEnoughDirection,
        objective.frontToRearGoodEnoughDirection};
    const std::array values{objective.swrTarget, objective.resistanceTargetOhms,
        objective.reactanceTargetOhms, objective.forwardGainTarget,
        objective.frontToBackTarget, objective.frontToRearTarget};
    const std::array aggregations{objective.swrAggregation, objective.resistanceAggregation,
        objective.reactanceAggregation, objective.forwardGainAggregation,
        objective.frontToBackAggregation, objective.frontToRearAggregation};
    for (auto row = 0; row < 6; ++row) {
        objectiveGoalControls_[row]->setCurrentIndex(goalIndex(goals[row], directions[row]));
        objectiveValueControls_[row]->setValue(values[row]);
        configureAggregationControl(objectiveAggregationControls_[row], goals[row],
            aggregations[row]);
    }
    directionalEvaluationMode_ = objective.directionalEvaluationMode;
    forwardThetaDegrees_ = objective.forwardThetaDegrees;
    forwardPhiDegrees_ = objective.forwardPhiDegrees;
    const auto componentIndex = radiationComponentControl_->findData(
        static_cast<int>(objective.radiationComponent));
    if (componentIndex >= 0) radiationComponentControl_->setCurrentIndex(componentIndex);
}

auto OptimizationWorkspace::selectedObjective() const -> analysis::OptimizationObjectiveSpec
{
    return {
        .kind = analysis::OptimizationObjectiveKind::PerCriterion,
        .referenceImpedance = referenceImpedanceControl_->value(),
        .swrWeight = swrWeightControl_->value(),
        .resistanceWeight = resistanceWeightControl_->value(),
        .resistanceTargetOhms = resistanceTargetControl_->value(),
        .reactanceWeight = reactanceWeightControl_->value(),
        .reactanceTargetOhms = reactanceTargetControl_->value(),
        .forwardGainWeight = forwardGainWeightControl_->value(),
        .frontToBackWeight = frontToBackWeightControl_->value(),
        .frontToRearWeight = frontToRearWeightControl_->value(),
        .swrGoal = selectedGoal(objectiveGoalControls_[0]),
        .resistanceGoal = selectedGoal(objectiveGoalControls_[1]),
        .reactanceGoal = selectedGoal(objectiveGoalControls_[2]),
        .forwardGainGoal = selectedGoal(objectiveGoalControls_[3]),
        .frontToBackGoal = selectedGoal(objectiveGoalControls_[4]),
        .frontToRearGoal = selectedGoal(objectiveGoalControls_[5]),
        .swrTarget = objectiveValueControls_[0]->value(),
        .forwardGainTarget = objectiveValueControls_[3]->value(),
        .frontToBackTarget = objectiveValueControls_[4]->value(),
        .frontToRearTarget = objectiveValueControls_[5]->value(),
        .swrGoodEnoughDirection = selectedGoodEnoughDirection(objectiveGoalControls_[0]),
        .resistanceGoodEnoughDirection = selectedGoodEnoughDirection(objectiveGoalControls_[1]),
        .reactanceGoodEnoughDirection = selectedGoodEnoughDirection(objectiveGoalControls_[2]),
        .forwardGainGoodEnoughDirection = selectedGoodEnoughDirection(objectiveGoalControls_[3]),
        .frontToBackGoodEnoughDirection = selectedGoodEnoughDirection(objectiveGoalControls_[4]),
        .frontToRearGoodEnoughDirection = selectedGoodEnoughDirection(objectiveGoalControls_[5]),
        .swrAggregation = selectedAggregation(objectiveAggregationControls_[0]),
        .resistanceAggregation = selectedAggregation(objectiveAggregationControls_[1]),
        .reactanceAggregation = selectedAggregation(objectiveAggregationControls_[2]),
        .forwardGainAggregation = selectedAggregation(objectiveAggregationControls_[3]),
        .frontToBackAggregation = selectedAggregation(objectiveAggregationControls_[4]),
        .frontToRearAggregation = selectedAggregation(objectiveAggregationControls_[5]),
        .forwardThetaDegrees = forwardThetaDegrees_,
        .forwardPhiDegrees = forwardPhiDegrees_,
        .directionalEvaluationMode = directionalEvaluationMode_,
        .radiationComponent = static_cast<analysis::RadiationComponent>(
            radiationComponentControl_->currentData().toInt()),
    };
}

}
