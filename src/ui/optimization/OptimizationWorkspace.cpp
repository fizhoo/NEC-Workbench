#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/DisplayFormat.h"
#include "ui/FileIo.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/optimization/OptimizationComparePanel.h"
#include "ui/optimization/OptimizationWorkspaceSupport.h"

#include "analysis/AnalysisResult.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace necwb::ui {
using namespace detail;

namespace {

auto objectiveSentence(const QString& name, const QString& unit,
    analysis::OptimizationGoal goal, analysis::GoodEnoughDirection direction,
    analysis::OptimizationAggregation aggregation, double target, double weight) -> QString
{
    const auto measurementWord = aggregation == analysis::OptimizationAggregation::Minimum
        ? QObject::tr("lowest")
        : aggregation == analysis::OptimizationAggregation::Average
            ? QObject::tr("average") : QObject::tr("highest");
    const auto errorWord = aggregation == analysis::OptimizationAggregation::Minimum
        ? QObject::tr("smallest")
        : aggregation == analysis::OptimizationAggregation::Average
            ? QObject::tr("average") : QObject::tr("largest");
    QString sentence;
    switch (goal) {
    case analysis::OptimizationGoal::Minimize:
        sentence = QObject::tr("Minimize the %1 %2 across the selected frequencies.")
            .arg(measurementWord, name);
        break;
    case analysis::OptimizationGoal::Maximize:
        sentence = QObject::tr("Maximize the %1 %2 across the selected frequencies.")
            .arg(measurementWord, name);
        break;
    case analysis::OptimizationGoal::Target:
        sentence = QObject::tr(
            "Keep %1 near %2%3 by minimizing the %4 error across the selected frequencies.")
            .arg(name, formatDecimal(target), unit, errorWord);
        break;
    case analysis::OptimizationGoal::GoodEnough:
        if (aggregation == analysis::OptimizationAggregation::Maximum) {
            sentence = direction == analysis::GoodEnoughDirection::AtMost
                ? QObject::tr("Keep %1 at or below %2%3 across all selected frequencies.")
                    .arg(name, formatDecimal(target), unit)
                : QObject::tr("Keep %1 at or above %2%3 across all selected frequencies.")
                    .arg(name, formatDecimal(target), unit);
        } else {
            const auto relation = direction == analysis::GoodEnoughDirection::AtMost
                ? QObject::tr("exceeds") : QObject::tr("falls below");
            sentence = QObject::tr(
                "Minimize the %1 amount by which %2 %3 %4%5 across the selected frequencies.")
                .arg(errorWord, name, relation, formatDecimal(target), unit);
        }
        break;
    }
    return QObject::tr("• %1 Relative weight: %2.")
        .arg(sentence, formatDecimal(weight));
}

auto optimizationPresetSummary(analysis::OptimizationPresetId id,
    const analysis::OptimizationObjectiveSpec& objective) -> QString
{
    switch (id) {
    case analysis::OptimizationPresetId::MinimizeSwr:
        return QObject::tr("Minimize the highest SWR across all selected frequencies.");
    case analysis::OptimizationPresetId::SwrLimit:
        return QObject::tr("Accept any candidate whose maximum SWR is at or below %1.")
            .arg(formatDecimal(objective.swrTarget));
    case analysis::OptimizationPresetId::FeedpointMatch:
        return QObject::tr(
            "Keep feedpoint resistance near %1 Ω and reactance near 0 Ω across the band.")
            .arg(formatDecimal(objective.referenceImpedance));
    case analysis::OptimizationPresetId::PeakGain:
        return QObject::tr("Maximize the lowest peak gain in the selected pattern cut across the band.");
    case analysis::OptimizationPresetId::FrontToBack:
        return QObject::tr("Maximize the lowest front-to-back ratio across the band; forward gain is not constrained.");
    case analysis::OptimizationPresetId::FrontToRear:
        return QObject::tr("Maximize the lowest front-to-rear ratio across the band; forward gain is not constrained.");
    case analysis::OptimizationPresetId::SwrAndGain:
        return QObject::tr(
            "Keep SWR at or below %1 while maximizing the lowest peak gain in the selected cut.")
            .arg(formatDecimal(objective.swrTarget));
    case analysis::OptimizationPresetId::Custom:
        return {};
    }
    return {};
}

}

void OptimizationWorkspace::setContext(QString source, QString sourceFile, QString backend,
    QString executable, int timeoutSeconds, bool modelValid)
{
    if (isRunning() || historicalSession_) return;
    const auto preserveStudy = std::exchange(preserveStudyOnNextContextUpdate_, false);
    const auto sourceUnchanged = source_ == source && sourceFile_ == sourceFile;
    const auto preserveFrequencySelection = contextInitialized_ && sourceFile_ == sourceFile;
    const auto directionMode = directionalEvaluationMode_;
    const auto preserveDirectionalSetup = preserveStudy && directionSetupReady_
        && (directionSetupConfigured_
            || directionMode != analysis::DirectionalEvaluationMode::SpecifiedDirection);
    const auto patternDerivedFixedDirection = preserveStudy && directionSetupReady_
        && !directionSetupConfigured_
        && directionMode == analysis::DirectionalEvaluationMode::SpecifiedDirection;
    const auto frequencySelection = preserveFrequencySelection
        ? std::optional{captureFrequencySelection()} : std::nullopt;
    source_ = std::move(source);
    sourceFile_ = std::move(sourceFile);
    backend_ = std::move(backend);
    executable_ = std::move(executable);
    timeoutSeconds_ = timeoutSeconds;
    modelValid_ = modelValid;
    contextInitialized_ = true;
    if (sourceUnchanged) {
        updateBaselineState();
        updateWorkload();
        updateReadiness();
        return;
    }
    maximumGainDirectionStale_ = patternDerivedFixedDirection
        || (!preserveStudy && preserveFrequencySelection
            && (maximumGainDirectionStale_ || !latestRadiation_.empty()));
    setLatestRadiation({});
    if (!preserveDirectionalSetup) {
        directionSetupConfigured_ = false;
        directionSetupReady_ = false;
    }
    const auto resolution = nec::NecSymbolResolver{}.resolve(source_.toStdString());
    // Set before populateVariables, whose readiness refresh compares against the baseline.
    resolvedDeck_ = resolution.ok() ? std::optional{resolution.generatedDeck} : std::nullopt;
    modelFingerprint_ = resolvedDeck_
        ? analysis::optimizationModelFingerprint(*resolvedDeck_) : std::string{};
    populateVariables(resolution);
    if (resolution.ok()) {
        const auto setup = nec::NecSetupConverter{}.convert(
            nec::NecParser{}.parse(resolution.generatedDeck));
        referenceImpedanceControl_->setValue(model::referenceImpedanceOhms(setup));
        resistanceTargetControl_->setValue(model::referenceImpedanceOhms(setup));
        if (setup.frequency) {
            frequencyEntryControl_->setValue(setup.frequency->startMHz);
            populateModelFrequencies(*setup.frequency);
        } else {
            modelFrequenciesMHz_.clear();
            frequencyTable_->clear();
        }
    } else {
        modelFrequenciesMHz_.clear();
        frequencyTable_->clear();
    }
    if (frequencySelection) restoreFrequencySelection(*frequencySelection);
    if (!preserveStudy) {
        candidates_.clear();
        candidateIndex_ = 0;
        bestScore_ = std::numeric_limits<double>::infinity();
        bestRow_ = -1;
        resultsTable_->setRowCount(0);
        candidatePlots_->clear();
        comparePanel_->clearCandidate();
        bestLabel_->setText(tr("No optimization results yet."));
        resetCandidateDetails();
    }
    updateBaselineState();
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::setLatestAnalysisResult(
    const analysis::AnalysisResult& result)
{
    if (historicalSession_) return;
    setLatestRadiation(result.radiation);
    maximumGainSearchRequested_ = false;
    maximumGainDirectionStale_ = false;
    const auto objective = selectedObjective();
    if (!directionSetupReady_ && (directionalPresetRequiresSetup()
            || (!directionSetupConfigured_ && objective.directionalEvaluationMode
                != analysis::DirectionalEvaluationMode::SpecifiedDirection)))
        (void)seedDirectionFromLatestPattern();
    updateObjectiveControls();
}

void OptimizationWorkspace::setExternalRunActive(bool active)
{
    externalRunActive_ = active;
    if (!active) maximumGainSearchRequested_ = false;
    const auto startOptimization = !active && startAfterBaseline_;
    if (startOptimization) startAfterBaseline_ = false;
    updateBaselineState();
    updateReadiness();
    updateLatestPatternPeakControls();
    if (startOptimization) QTimer::singleShot(0, this, [this] { startSweep(); });
}

void OptimizationWorkspace::setModelValid(bool valid)
{
    modelValid_ = valid;
    updateReadiness();
}

void OptimizationWorkspace::setRunsChangedCallback(std::function<void()> callback)
{
    runsChangedCallback_ = std::move(callback);
}

void OptimizationWorkspace::setRunningChangedCallback(std::function<void()> callback)
{
    runningChangedCallback_ = std::move(callback);
}

void OptimizationWorkspace::setReturnToCurrentWorkCallback(std::function<void()> callback)
{
    returnToCurrentWorkCallback_ = std::move(callback);
}

void OptimizationWorkspace::setParameterizationHelpCallback(
    std::function<void()> callback)
{
    parameterizationHelpCallback_ = std::move(callback);
}

void OptimizationWorkspace::setApplyParameterCallback(
    std::function<bool(std::vector<std::pair<QString, double>>)> callback)
{
    applyParameterCallback_ = std::move(callback);
    updateReadiness();
}

void OptimizationWorkspace::setApplyAndRunCallback(
    std::function<bool(std::vector<std::pair<QString, double>>)> callback)
{
    applyAndRunCallback_ = std::move(callback);
    updateReadiness();
}

void OptimizationWorkspace::setFindMaximumGainCallback(
    std::function<bool(double)> callback)
{
    findMaximumGainCallback_ = std::move(callback);
    updateLatestPatternPeakControls();
}

auto OptimizationWorkspace::isRunning() const noexcept -> bool
{
    return baselineRunInProgress_ || evaluator_->isRunning()
        || candidateIndex_ < candidates_.size();
}

void OptimizationWorkspace::cancelAndWait()
{
    cancelRequested_ = true;
    if (baselineRunInProgress_ && cancelBaselineRunCallback_) cancelBaselineRunCallback_();
    evaluator_->cancelAndWait();
}

void OptimizationWorkspace::cancel()
{
    cancelSweep();
}

void OptimizationWorkspace::populateVariables(const nec::SymbolResolution& resolution)
{
    struct SavedRange {
        Qt::CheckState checked{Qt::Unchecked};
        double minimum{};
        double maximum{};
        double tolerance{0.01};
    };
    std::unordered_map<std::string, SavedRange> savedRanges;
    for (auto row = 0; row < variablesTable_->rowCount(); ++row) {
        const auto* useItem = variablesTable_->item(row, 0);
        const auto* nameItem = variablesTable_->item(row, 1);
        if (useItem == nullptr || nameItem == nullptr) continue;
        bool minimumValid{};
        bool maximumValid{};
        bool toleranceValid{};
        const auto minimum = variablesTable_->item(row, 3)->text().toDouble(&minimumValid);
        const auto maximum = variablesTable_->item(row, 4)->text().toDouble(&maximumValid);
        const auto tolerance = variablesTable_->item(row, 5)->text().toDouble(&toleranceValid);
        if (minimumValid && maximumValid && toleranceValid) {
            savedRanges.emplace(nameItem->text().toStdString(),
                SavedRange{useItem->checkState(), minimum, maximum, tolerance});
        }
    }
    const QSignalBlocker blocker(variablesTable_);
    definitions_ = resolution.definitions;
    symbolsResolved_ = resolution.ok();
    variablesTable_->setRowCount(static_cast<int>(definitions_.size()));
    selectedValueSuffix_.clear();
    for (std::size_t index = 0; index < definitions_.size(); ++index) {
        const auto& definition = definitions_[index];
        const auto row = static_cast<int>(index);
        const auto name = QString::fromStdString(definition.name);
        auto* useItem = new QTableWidgetItem;
        useItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        useItem->setCheckState(index == 0 ? Qt::Checked : Qt::Unchecked);
        variablesTable_->setItem(row, 0, useItem);
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        nameItem->setToolTip(tr("Expression: %1\nSource line: %2")
            .arg(QString::fromStdString(definition.expression))
            .arg(definition.lineNumber));
        variablesTable_->setItem(row, 1, nameItem);
        auto* valueItem = numericItem(definition.value);
        valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        valueItem->setToolTip(tr(
            "Resolved numeric SY value. No physical unit is inferred from where the symbol is used."));
        variablesTable_->setItem(row, 2, valueItem);
        auto minimum = definition.value == 0.0 ? -1.0 : definition.value * 0.8;
        auto maximum = definition.value == 0.0 ? 1.0 : definition.value * 1.2;
        if (minimum > maximum) std::swap(minimum, maximum);
        auto tolerance = 0.01;
        if (const auto saved = savedRanges.find(definition.name); saved != savedRanges.end()) {
            useItem->setCheckState(saved->second.checked);
            minimum = saved->second.minimum;
            maximum = saved->second.maximum;
            tolerance = saved->second.tolerance;
        }
        for (const auto& [column, value] : std::vector<std::pair<int, double>>{
                 {3, minimum}, {4, maximum}, {5, tolerance}}) {
            auto* item = numericItem(value);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            variablesTable_->setItem(row, column, item);
        }
    }
    fitVariablesTableHeight();
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::fitVariablesTableHeight()
{
    // Show up to eight parameters before scrolling, so short lists leave no blank band.
    const auto visibleRows = std::clamp(variablesTable_->rowCount(), 3, 8);
    variablesTable_->setFixedHeight(variablesTable_->horizontalHeader()->sizeHint().height()
        + visibleRows * variablesTable_->verticalHeader()->defaultSectionSize()
        + 2 * variablesTable_->frameWidth());
}

auto OptimizationWorkspace::selectedVariables() const
    -> std::vector<StudySetup::VariableRange>
{
    std::vector<StudySetup::VariableRange> variables;
    for (auto row = 0; row < variablesTable_->rowCount(); ++row) {
        const auto* useItem = variablesTable_->item(row, 0);
        const auto* nameItem = variablesTable_->item(row, 1);
        if (useItem == nullptr || nameItem == nullptr || useItem->checkState() != Qt::Checked)
            continue;
        bool resolvedValid{};
        bool minimumValid{};
        bool maximumValid{};
        bool toleranceValid{};
        const auto resolved = variablesTable_->item(row, 2)->text().toDouble(&resolvedValid);
        const auto minimum = variablesTable_->item(row, 3)->text().toDouble(&minimumValid);
        const auto maximum = variablesTable_->item(row, 4)->text().toDouble(&maximumValid);
        const auto tolerance = variablesTable_->item(row, 5)->text().toDouble(&toleranceValid);
        const auto invalid = std::numeric_limits<double>::quiet_NaN();
        variables.push_back({nameItem->text(), resolvedValid ? resolved : invalid,
            minimumValid ? minimum : invalid, maximumValid ? maximum : invalid,
            toleranceValid ? tolerance : invalid});
    }
    return variables;
}

void OptimizationWorkspace::updateSearchMethodControls()
{
    const auto method = selectedSearchMethod();
    const auto multivariable = isMultivariable(method);
    const auto differentialEvolution = method == SearchMethod::DifferentialEvolution;
    resultsTable_->horizontalHeaderItem(ValueColumn)->setText(
        multivariable ? tr("Parameters") : tr("Value"));
    searchMethodControl_->setToolTip(
        searchMethodControl_->currentData(Qt::ToolTipRole).toString());
    searchBudgetLabel_->setText(multivariable ? tr("Maximum evaluations") : tr("Candidate count"));
    searchBudgetLabel_->setVisible(!differentialEvolution);
    pointsControl_->setVisible(!multivariable);
    adaptiveMaximumEvaluationsControl_->setVisible(multivariable && !differentialEvolution);
    adaptiveScoreToleranceLabel_->setVisible(multivariable && !differentialEvolution);
    adaptiveScoreToleranceControl_->setVisible(multivariable && !differentialEvolution);
    differentialEvolutionSettings_->setVisible(differentialEvolution);
    pointsControl_->setEnabled(!multivariable && !isRunning() && !historicalSession_);
    adaptiveMaximumEvaluationsControl_->setEnabled(multivariable && !differentialEvolution && !isRunning()
        && !historicalSession_);
    adaptiveScoreToleranceControl_->setEnabled(multivariable && !differentialEvolution && !isRunning()
        && !historicalSession_);
    differentialEvolutionSettings_->setEnabled(
        differentialEvolution && !isRunning() && !historicalSession_);
    if (!historicalSession_)
        runButton_->setText(method == SearchMethod::ParameterSweep
            ? tr("Run Parameter Sweep") : tr("Run %1").arg(searchMethodName(method)));
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::updateSetupSummaries()
{
    const auto setup = currentStudySetup();
    const auto& frequencies = setup.frequenciesMHz;
    QString frequencySource;
    switch (setup.frequencySelection.mode) {
    case FrequencyMode::ModelSweep: frequencySource = tr("Model FR sweep"); break;
    case FrequencyMode::Explicit: frequencySource = tr("Selected frequencies"); break;
    case FrequencyMode::Continuous: frequencySource = tr("Custom continuous sweep"); break;
    }
    const auto frequencyDetail = frequencies.empty()
        ? tr("No valid frequencies")
        : frequencies.size() == 1
            ? tr("1 point · %1 MHz").arg(formatDecimal(frequencies.front()))
            : tr("%1 points · %2–%3 MHz")
                .arg(frequencies.size())
                .arg(formatDecimal(frequencies.front()))
                .arg(formatDecimal(frequencies.back()));
    frequencySummaryLabel_->setText(frequencySource + QLatin1Char('\n') + frequencyDetail);

    QStringList criteria;
    const std::array weights{setup.objective.swrWeight, setup.objective.resistanceWeight,
        setup.objective.reactanceWeight, setup.objective.forwardGainWeight,
        setup.objective.frontToBackWeight, setup.objective.frontToRearWeight};
    const auto gainName = setup.objective.directionalEvaluationMode
            == analysis::DirectionalEvaluationMode::SpecifiedDirection
        ? tr("gain at the specified direction") : tr("peak gain in the selected cut");
    const std::array names{tr("SWR"), tr("resistance"), tr("reactance"),
        gainName, tr("F/B"), fromCore(analysis::frontToRearName(
            setup.objective.frontToRearRegion))};
    const std::array units{QString{}, tr(" Ω"), tr(" Ω"), tr(" dBi"), tr(" dB"), tr(" dB")};
    const std::array goals{setup.objective.swrGoal, setup.objective.resistanceGoal,
        setup.objective.reactanceGoal, setup.objective.forwardGainGoal,
        setup.objective.frontToBackGoal, setup.objective.frontToRearGoal};
    const std::array directions{setup.objective.swrGoodEnoughDirection,
        setup.objective.resistanceGoodEnoughDirection,
        setup.objective.reactanceGoodEnoughDirection,
        setup.objective.forwardGainGoodEnoughDirection,
        setup.objective.frontToBackGoodEnoughDirection,
        setup.objective.frontToRearGoodEnoughDirection};
    const std::array aggregations{setup.objective.swrAggregation,
        setup.objective.resistanceAggregation, setup.objective.reactanceAggregation,
        setup.objective.forwardGainAggregation, setup.objective.frontToBackAggregation,
        setup.objective.frontToRearAggregation};
    const std::array targets{setup.objective.swrTarget,
        setup.objective.resistanceTargetOhms, setup.objective.reactanceTargetOhms,
        setup.objective.forwardGainTarget, setup.objective.frontToBackTarget,
        setup.objective.frontToRearTarget};
    for (auto row = 0; row < 6; ++row) {
        if (weights[row] <= 0.0) continue;
        criteria.append(objectiveSentence(names[row], units[row], goals[row],
            directions[row], aggregations[row], targets[row], weights[row]));
    }
    QString directionSummary;
    if (setup.objective.directionalEvaluationMode
        == analysis::DirectionalEvaluationMode::StrongestInAzimuthCut) {
        directionSummary = tr("\nTrack peak · %1 (NEC θ %2)")
            .arg(fromCore(analysis::azimuthCutName(setup.objective.forwardThetaDegrees)),
                fromCore(analysis::formatAngle(setup.objective.forwardThetaDegrees)));
    } else if (setup.objective.directionalEvaluationMode
        == analysis::DirectionalEvaluationMode::StrongestInElevationCut) {
        directionSummary = tr("\nTrack peak · %1")
            .arg(fromCore(analysis::elevationCutName(setup.objective.forwardPhiDegrees)));
    } else {
        directionSummary = tr("\nEvaluate fixed direction %1 (NEC %2)")
            .arg(fromCore(analysis::formatDirection(setup.objective.forwardThetaDegrees,
                     setup.objective.forwardPhiDegrees)),
                fromCore(analysis::formatNecDirection(setup.objective.forwardThetaDegrees,
                    setup.objective.forwardPhiDegrees)));
    }
    const auto preset = selectedObjectivePreset();
    const auto directional = setup.objective.forwardGainWeight > 0.0
        || setup.objective.frontToBackWeight > 0.0
        || setup.objective.frontToRearWeight > 0.0;
    if (preset != analysis::OptimizationPresetId::Custom) {
        const auto summary = optimizationPresetSummary(preset, setup.objective);
        objectiveSummaryLabel_->setText(tr("%1\nReference Z %2 Ω\n%3")
            .arg(optimizationPresetName(preset))
            .arg(formatDecimal(setup.objective.referenceImpedance))
            .arg(summary) + (directional ? directionSummary : QString{}));
        objectiveExplanationLabel_->setText(summary);
    } else {
        objectiveSummaryLabel_->setText(tr("Custom\nReference Z %1 Ω\n%2")
            .arg(formatDecimal(setup.objective.referenceImpedance))
            .arg(criteria.isEmpty() ? tr("No enabled criteria") : criteria.join(QLatin1Char('\n')))
            + (directional ? directionSummary : QString{}));
        objectiveExplanationLabel_->setText(criteria.isEmpty()
            ? tr("No criteria are enabled. Set at least one weight above zero.")
            : criteria.join(QLatin1Char('\n')));
    }
    const auto objectiveWarning = criteria.isEmpty()
        ? tr("No criteria are enabled. Click Edit… and set a weight above zero.")
        : directionalPresetRequiresSetup() && !directionSetupReady_
            ? tr("Direction setup required. Click Edit… and use Find Sampled Peak or Choose.")
            : QString{};
    objectiveWarningLabel_->setText(objectiveWarning);
    objectiveWarningLabel_->setVisible(!objectiveWarning.isEmpty() && !historicalSession_);
}

void OptimizationWorkspace::updateWorkload()
{
    const auto setup = currentStudySetup();
    const auto frequencyCount = setup.frequenciesMHz.size();
    const auto multivariable = isMultivariable(setup.searchMethod);
    const auto candidateCount = setup.candidateLimit;
    auto workload = multivariable
        ? tr("Up to %1 candidates × %2 %3 = up to %4 calculated points")
            .arg(candidateCount).arg(frequencyCount)
            .arg(frequencyCount == 1 ? tr("frequency") : tr("frequencies"))
            .arg(static_cast<qulonglong>(candidateCount * frequencyCount))
        : tr("%1 candidates × %2 %3 = %4 calculated points")
            .arg(candidateCount).arg(frequencyCount)
            .arg(frequencyCount == 1 ? tr("frequency") : tr("frequencies"))
            .arg(static_cast<qulonglong>(candidateCount * frequencyCount));
    const auto directionalEnabled = setup.objective.forwardGainWeight > 0.0
        || setup.objective.frontToBackWeight > 0.0
        || setup.objective.frontToRearWeight > 0.0;
    auto directionalSamplesPerFrequency = 0U;
    if (directionalEnabled) {
        if (setup.objective.directionalEvaluationMode
            == analysis::DirectionalEvaluationMode::StrongestInAzimuthCut) {
            directionalSamplesPerFrequency = 72U;
            if ((setup.objective.frontToBackWeight > 0.0
                    || setup.objective.frontToRearWeight > 0.0)
                && std::abs(setup.objective.forwardThetaDegrees - 90.0) > 1.0e-6)
                directionalSamplesPerFrequency = 144U;
        } else if (setup.objective.directionalEvaluationMode
            == analysis::DirectionalEvaluationMode::StrongestInElevationCut) {
            directionalSamplesPerFrequency = 37U;
        } else {
            directionalSamplesPerFrequency = setup.objective.frontToRearWeight > 0.0
                ? 38U : 1U + (setup.objective.frontToBackWeight > 0.0 ? 1U : 0U);
        }
    }
    if (directionalSamplesPerFrequency > 0) {
        workload += tr(" · %1 directional RP samples")
            .arg(static_cast<qulonglong>(candidateCount * frequencyCount
                * static_cast<std::size_t>(directionalSamplesPerFrequency)));
    }
    workloadLabel_->setText(workload);
    QStringList variableNames;
    for (const auto& variable : setup.variables) variableNames.append(variable.name);
    const auto variableSummary = isMultivariable(setup.searchMethod) || variableNames.size() > 1
        ? tr("%1 parameter(s): %2").arg(variableNames.size()).arg(variableNames.join(QStringLiteral(", ")))
        : setup.variable.isEmpty() ? tr("None") : setup.variable;
    const auto rangeSummary = isMultivariable(setup.searchMethod)
        ? tr("per-parameter bounds")
        : tr("%1 to %2").arg(formatDecimal(setup.minimum), formatDecimal(setup.maximum));
    studySummaryLabel_->setText(tr("Variables: %1  |  Range: %2  |  Frequencies: %3  |  Goal: %4")
        .arg(variableSummary)
        .arg(rangeSummary)
        .arg(frequencyCount)
        .arg(optimizationPresetName(selectedObjectivePreset())));
    updateSetupSummaries();
}

void OptimizationWorkspace::setBaselineRunCallback(std::function<bool(
    const analysis::FrequencyPlan&, const analysis::OptimizationObjectiveSpec&)> callback)
{
    baselineRunCallback_ = std::move(callback);
    updateReadiness();
}

void OptimizationWorkspace::setCancelBaselineRunCallback(std::function<void()> callback)
{
    cancelBaselineRunCallback_ = std::move(callback);
}

void OptimizationWorkspace::setOptimizationBaseline(const AnalysisRunRecord& record,
    const analysis::AnalysisResult& result)
{
    if (historicalSession_) return;
    baselineRecord_ = record;
    baselineResult_ = result;
    updateBaselineState();
    updateComparisonCandidate();
    updateReadiness();
}

void OptimizationWorkspace::finishOptimizationBaseline(bool succeeded, QString detail)
{
    baselineRunInProgress_ = false;
    startAfterBaseline_ = succeeded;
    if (!succeeded) {
        statusLabel_->setText(detail.isEmpty()
            ? tr("The optimization baseline was not created; optimization was not started.")
            : tr("Optimization baseline failed: %1").arg(detail));
    } else {
        updateBaselineState();
        if (!baselineCompatibility_.compatible()) {
            startAfterBaseline_ = false;
            statusLabel_->setText(tr(
                "The optimization baseline does not cover the current study."));
        } else {
            statusLabel_->setText(tr(
                "Optimization baseline created; live result windows remain unchanged. Starting optimization."));
        }
    }
    updateReadiness();
}

auto OptimizationWorkspace::baselineRequirements(const StudySetup& setup) const
    -> analysis::OptimizationBaselineRequirements
{
    analysis::OptimizationBaselineRequirements requirements;
    requirements.modelFingerprint = modelFingerprint_;
    requirements.backend = backend_.toStdString();
    requirements.referenceImpedanceOhms = setup.objective.referenceImpedance;
    auto frequencies = setup.frequenciesMHz;
    if (setup.objective.kind == analysis::OptimizationObjectiveKind::SwrAtFrequency)
        frequencies = {setup.objective.targetFrequencyMHz};
    const auto impedanceRequired = setup.objective.swrWeight > 0.0
        || setup.objective.resistanceWeight > 0.0
        || setup.objective.reactanceWeight > 0.0;
    if (impedanceRequired) requirements.impedanceFrequenciesMHz = frequencies;
    const auto radiationRequired = setup.objective.forwardGainWeight > 0.0
        || setup.objective.frontToBackWeight > 0.0
        || setup.objective.frontToRearWeight > 0.0;
    if (radiationRequired) {
        requirements.radiationFrequenciesMHz = frequencies;
        if (resolvedDeck_) {
            const analysis::FrequencyPlan frequencyPlan{
                analysis::FrequencyPlanMode::Explicit, frequencies, {}};
            requirements.radiation = analysis::optimizationRadiationRequirements(
                analysis::prepareOptimizationInput(
                    *resolvedDeck_, frequencyPlan, setup.objective));
        }
    }
    return requirements;
}

void OptimizationWorkspace::updateBaselineState()
{
    std::optional<analysis::OptimizationBaselineDescriptor> descriptor;
    if (baselineRecord_ && baselineResult_) {
        analysis::OptimizationResultCoverage coverage;
        coverage.referenceImpedanceOhms = baselineRecord_->referenceImpedanceOhms;
        coverage.impedanceFrequenciesMHz = baselineRecord_->impedanceFrequenciesMHz;
        coverage.radiation = baselineRecord_->radiationCoverage;
        descriptor = analysis::OptimizationBaselineDescriptor{
            baselineRecord_->modelFingerprint.toStdString(),
            baselineRecord_->backend.toStdString(), std::move(coverage)};
    }
    baselineCompatibility_ = analysis::evaluateOptimizationBaseline(
        descriptor, baselineRequirements(currentStudySetup()));

    QString status;
    switch (baselineCompatibility_.status) {
    case analysis::OptimizationBaselineStatus::Compatible:
        status = tr("Optimization baseline — Run %1 · compatible with this study")
            .arg(baselineRecord_->id);
        break;
    case analysis::OptimizationBaselineStatus::Missing:
        status = tr("Optimization baseline — none · the current model must be analyzed first");
        break;
    case analysis::OptimizationBaselineStatus::IncompleteMetadata:
        status = tr("Optimization baseline — unavailable · this older run lacks compatibility metadata");
        break;
    case analysis::OptimizationBaselineStatus::ModelChanged:
        status = tr("Optimization baseline — stale · the model changed after the run");
        break;
    case analysis::OptimizationBaselineStatus::BackendChanged:
        status = tr("Optimization baseline — incompatible · it used a different solver backend");
        break;
    case analysis::OptimizationBaselineStatus::ReferenceImpedanceChanged:
        status = tr("Optimization baseline — incompatible · its SWR reference impedance differs");
        break;
    case analysis::OptimizationBaselineStatus::MissingImpedanceCoverage:
        status = tr("Optimization baseline — incomplete · impedance is missing at required frequencies");
        break;
    case analysis::OptimizationBaselineStatus::MissingRadiationCoverage:
        status = baselineCompatibility_.missingRadiationFrequenciesMHz.empty()
            ? tr("Optimization baseline — incompatible · directional cut grid differs from this study")
            : tr("Optimization baseline — incomplete · radiation data is missing at required frequencies");
        break;
    }
    if (baselineRecord_ && baselineResult_) {
        comparePanel_->setBaseline(*baselineResult_,
            tr("Starting model — Run %1").arg(baselineRecord_->id));
    } else {
        comparePanel_->clearBaseline();
    }
    comparePanel_->setBaselineStatus(status, baselineCompatibility_.compatible());
}

void OptimizationWorkspace::requestOptimizationStart()
{
    if (!runButton_->isEnabled()) {
        const auto blocker = readinessBlocker(currentStudySetup());
        if (!blocker.isEmpty()) statusLabel_->setText(blocker);
        return;
    }
    updateBaselineState();
    if (baselineCompatibility_.compatible()) {
        startSweep();
        return;
    }
    if (!baselineRunCallback_) {
        statusLabel_->setText(tr("A compatible baseline is required before optimization."));
        return;
    }
    const auto setup = currentStudySetup();
    baselineRunInProgress_ = true;
    startAfterBaseline_ = false;
    statusLabel_->setText(tr("Running the current model as the optimization baseline…"));
    updateReadiness();
    const analysis::FrequencyPlan frequencyPlan{
        analysis::FrequencyPlanMode::Explicit, setup.frequenciesMHz, {}};
    if (!baselineRunCallback_(frequencyPlan, setup.objective)) {
        baselineRunInProgress_ = false;
        statusLabel_->setText(tr("The baseline analysis could not be started."));
        updateReadiness();
    }
}

auto OptimizationWorkspace::readinessBlocker(const StudySetup& setup) const -> QString
{
    if (!symbolsResolved_)
        return tr("Resolve the model's SY expression errors before optimizing.");
    if (definitions_.empty())
        return tr("Add SY declarations to the model to enable parameter optimization.");
    if (setup.variables.empty()) return tr("Check at least one parameter to optimize.");
    if (setup.searchMethod == SearchMethod::ParameterSweep && setup.variables.size() > 1)
        return tr("Parameter Sweep varies one parameter. Check exactly one, or choose another method.");
    const auto boundsValid = std::ranges::all_of(setup.variables, [](const auto& variable) {
        return std::isfinite(variable.minimum) && std::isfinite(variable.maximum)
            && std::isfinite(variable.tolerance) && variable.minimum < variable.maximum
            && variable.tolerance > 0.0;
    });
    if (!boundsValid)
        return tr("Each checked parameter needs Minimum below Maximum and a positive Tolerance.");
    if (setup.searchMethod == SearchMethod::DifferentialEvolution
        && (setup.populationSize < 4 || setup.maximumGenerations < 1))
        return tr("Use a population of at least four and at least one generation.");
    if (isMultivariable(setup.searchMethod)
        && setup.searchMethod != SearchMethod::DifferentialEvolution
        && setup.candidateLimit < static_cast<int>(setup.variables.size()) + 1)
        return tr("Increase Maximum evaluations to at least the checked parameter count plus one.");
    if (setup.frequenciesMHz.empty()) return tr("Choose at least one valid frequency.");
    const auto& objective = setup.objective;
    if (objective.swrWeight + objective.resistanceWeight + objective.reactanceWeight
            + objective.forwardGainWeight + objective.frontToBackWeight
            + objective.frontToRearWeight <= 0.0)
        return tr("Set at least one objective weight above zero.");
    if (directionalPresetRequiresSetup() && !directionSetupReady_)
        return tr("Direction setup required. Open Objective Edit… and use Find Sampled Peak or Choose.");
    if (!modelValid_) return tr("Fix the model check errors before optimizing.");
    if (externalRunActive_)
        return tr("Another analysis is running. Optimization can start when it finishes.");
    if (!baselineCompatibility_.compatible() && !baselineRunCallback_)
        return tr("An optimization-baseline callback is unavailable.");
    if (!analysis::isBackendRunnable(backend_.toStdString()))
        return tr("The selected solver backend cannot run optimization candidates.");
    const auto executable = QFileInfo(executable_);
    if (!executable.exists() || !executable.isFile() || !executable.isExecutable())
        return tr("Solver executable not found. Check the solver settings.");
    return {};
}

void OptimizationWorkspace::updateReadiness()
{
    const auto setup = currentStudySetup();
    if (!historicalSession_) updateBaselineState();
    const auto directional = setup.objective.forwardGainWeight > 0.0
        || setup.objective.frontToBackWeight > 0.0
        || setup.objective.frontToRearWeight > 0.0;
    const auto blocker = readinessBlocker(setup);
    runButton_->setEnabled(!historicalSession_ && !isRunning() && blocker.isEmpty());
    if (!historicalSession_) {
        runButton_->setText(baselineRunInProgress_
            ? tr("Building Baseline…")
            : baselineCompatibility_.compatible()
                ? selectedSearchMethod() == SearchMethod::ParameterSweep
                    ? tr("Run Parameter Sweep")
                    : tr("Run %1").arg(searchMethodName(selectedSearchMethod()))
                : baselineRecord_.has_value()
                    ? tr("Rebuild Baseline && Optimize")
                    : tr("Build Baseline && Optimize"));
    }
    runButton_->setToolTip(!blocker.isEmpty()
        ? blocker
        : !baselineCompatibility_.compatible()
            ? tr("Runs only the frequencies and outputs required by this optimization study, then starts optimization. Normal Results are not populated.")
            : tr("Runs this optimization study using the existing compatible baseline. Normal Results remain unchanged."));
    // Only replace status text that a previous readiness check wrote, so run progress
    // and completion messages stay visible.
    if (!historicalSession_ && !isRunning()) {
        if (!blocker.isEmpty()) {
            statusLabel_->setText(blocker);
            readinessMessage_ = blocker;
        } else if (!readinessMessage_.isEmpty()) {
            if (statusLabel_->text() == readinessMessage_) statusLabel_->clear();
            readinessMessage_.clear();
        }
    }
    cancelButton_->setEnabled(isRunning());
    resultsTable_->horizontalHeader()->setSectionsClickable(!isRunning());
    applyBestButton_->setEnabled(!historicalSession_ && modelValid_ && !isRunning()
        && bestRow_ >= 0 && static_cast<std::size_t>(bestRow_) < candidates_.size()
        && static_cast<bool>(applyParameterCallback_));
    updateCandidateActionState();
    const auto editable = !isRunning() && !historicalSession_;
    editFrequencyButton_->setEnabled(editable);
    editObjectiveButton_->setEnabled(editable);
    variablesTable_->setEnabled(editable);
    frequencyModeControl_->setEnabled(editable);
    const auto multivariable = isMultivariable(selectedSearchMethod());
    searchMethodControl_->setEnabled(editable);
    pointsControl_->setEnabled(editable && !multivariable);
    const auto differentialEvolution = selectedSearchMethod()
        == SearchMethod::DifferentialEvolution;
    adaptiveMaximumEvaluationsControl_->setEnabled(
        editable && multivariable && !differentialEvolution);
    adaptiveScoreToleranceControl_->setEnabled(
        editable && multivariable && !differentialEvolution);
    differentialEvolutionSettings_->setEnabled(editable && differentialEvolution);
    referenceImpedanceControl_->setEnabled(editable);
    for (auto* control : {swrWeightControl_, resistanceWeightControl_,
             reactanceWeightControl_, forwardGainWeightControl_,
             frontToBackWeightControl_, frontToRearWeightControl_})
        control->setEnabled(editable);
    for (auto row = 0; row < 6; ++row) {
        objectiveGoalControls_[row]->setEnabled(editable);
        objectiveAggregationControls_[row]->setEnabled(editable);
        const auto goal = selectedGoal(objectiveGoalControls_[row]);
        objectiveValueControls_[row]->setEnabled(editable
            && (goal == analysis::OptimizationGoal::Target
                || goal == analysis::OptimizationGoal::GoodEnough));
    }
    radiationComponentControl_->setEnabled(editable && directional);
    frontToRearRegionControl_->setEnabled(editable && directional);
    updateLatestPatternPeakControls();
    const auto explicitMode = selectedFrequencyMode() == FrequencyMode::Explicit;
    frequencyTable_->setEnabled(editable && explicitMode);
    frequencyEntryControl_->setEnabled(editable && explicitMode);
    addFrequencyButton_->setEnabled(editable && explicitMode);
    removeFrequencyButton_->setEnabled(editable && explicitMode);
    pasteFrequencyButton_->setEnabled(editable && explicitMode);
    clearFrequencyButton_->setEnabled(
        editable && explicitMode && frequencyTable_->count() > 0);
    addAmateurBandButton_->setEnabled(editable && explicitMode);
    const auto continuousMode = selectedFrequencyMode() == FrequencyMode::Continuous;
    continuousStartControl_->setEnabled(editable && continuousMode);
    continuousStopControl_->setEnabled(editable && continuousMode);
    continuousStepControl_->setEnabled(editable && continuousMode);
}

void OptimizationWorkspace::startSweep()
{
    if (!runButton_->isEnabled()) {
        const auto blocker = readinessBlocker(currentStudySetup());
        if (!blocker.isEmpty()) statusLabel_->setText(blocker);
        return;
    }
    const auto setup = currentStudySetup();
    activeVariables_ = setup.variables;
    QStringList activeNames;
    for (const auto& variable : activeVariables_) activeNames.append(variable.name);
    selectedSymbol_ = activeNames.join(QStringLiteral(", "));
    activeObjective_ = setup.objective;
    activeSearchMethod_ = setup.searchMethod;
    activeFrequenciesMHz_ = setup.frequenciesMHz;
    if (activeFrequenciesMHz_.empty()) {
        statusLabel_->setText(tr("Choose at least one valid frequency."));
        return;
    }
    resultsTable_->horizontalHeaderItem(ScoreColumn)->setText(tr("Objective Score"));
    sessionRecord_ = runStore_.create(backend_, sourceFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : sourceFile_,
        QStringLiteral("optimization-session"));
    sessionRecord_->status = QStringLiteral("Running");
    const auto candidateLimit = setup.candidateLimit;
    sessionRecord_->candidateCount = candidateLimit;
    const auto objectiveLabel = optimizationPresetName(selectedObjectivePreset());
    sessionRecord_->summary = isMultivariable(activeSearchMethod_)
        ? tr("%1 %2 · %3 · up to %4 candidates × %5 frequencies")
            .arg(searchMethodName(activeSearchMethod_))
            .arg(selectedSymbol_, objectiveLabel)
            .arg(candidateLimit).arg(activeFrequenciesMHz_.size())
        : tr("Parameter sweep %1 · %2 · %3 candidates × %4 frequencies")
            .arg(selectedSymbol_, objectiveLabel)
            .arg(candidateLimit).arg(activeFrequenciesMHz_.size());
    runStore_.save(*sessionRecord_);
    QJsonArray variableMetadata;
    for (const auto& variable : activeVariables_) {
        variableMetadata.append(QJsonObject{
            {QStringLiteral("name"), variable.name},
            {QStringLiteral("resolvedValue"), variable.resolvedValue},
            {QStringLiteral("minimum"), variable.minimum},
            {QStringLiteral("maximum"), variable.maximum},
            {QStringLiteral("tolerance"), variable.tolerance},
        });
    }
    auto sessionMetadata = QJsonObject{
        {QStringLiteral("version"), 12},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("minimum"), setup.minimum},
        {QStringLiteral("maximum"), setup.maximum},
        {QStringLiteral("variables"), variableMetadata},
        {QStringLiteral("candidateCount"), candidateLimit},
        {QStringLiteral("searchMethod"), searchMethodId(activeSearchMethod_)},
        {QStringLiteral("scoreTolerance"), setup.scoreTolerance},
        {QStringLiteral("populationSize"), setup.populationSize},
        {QStringLiteral("maximumGenerations"), setup.maximumGenerations},
        {QStringLiteral("mutationFactor"), setup.mutationFactor},
        {QStringLiteral("crossoverRate"), setup.crossoverRate},
        {QStringLiteral("randomSeed"), setup.randomSeed},
        {QStringLiteral("objective"), objectiveId(activeObjective_.kind)},
        {QStringLiteral("referenceImpedance"), activeObjective_.referenceImpedance},
        {QStringLiteral("targetFrequencyMHz"), activeObjective_.targetFrequencyMHz},
        {QStringLiteral("swrWeight"), activeObjective_.swrWeight},
        {QStringLiteral("resistanceWeight"), activeObjective_.resistanceWeight},
        {QStringLiteral("resistanceTargetOhms"), activeObjective_.resistanceTargetOhms},
        {QStringLiteral("reactanceWeight"), activeObjective_.reactanceWeight},
        {QStringLiteral("reactanceTargetOhms"), activeObjective_.reactanceTargetOhms},
        {QStringLiteral("forwardGainWeight"), activeObjective_.forwardGainWeight},
        {QStringLiteral("frontToBackWeight"), activeObjective_.frontToBackWeight},
        {QStringLiteral("frontToRearWeight"), activeObjective_.frontToRearWeight},
        {QStringLiteral("swrGoal"), static_cast<int>(activeObjective_.swrGoal)},
        {QStringLiteral("resistanceGoal"), static_cast<int>(activeObjective_.resistanceGoal)},
        {QStringLiteral("reactanceGoal"), static_cast<int>(activeObjective_.reactanceGoal)},
        {QStringLiteral("forwardGainGoal"), static_cast<int>(activeObjective_.forwardGainGoal)},
        {QStringLiteral("frontToBackGoal"), static_cast<int>(activeObjective_.frontToBackGoal)},
        {QStringLiteral("frontToRearGoal"), static_cast<int>(activeObjective_.frontToRearGoal)},
        {QStringLiteral("swrTarget"), activeObjective_.swrTarget},
        {QStringLiteral("forwardGainTarget"), activeObjective_.forwardGainTarget},
        {QStringLiteral("frontToBackTarget"), activeObjective_.frontToBackTarget},
        {QStringLiteral("frontToRearTarget"), activeObjective_.frontToRearTarget},
        {QStringLiteral("swrGoodEnoughDirection"),
            static_cast<int>(activeObjective_.swrGoodEnoughDirection)},
        {QStringLiteral("resistanceGoodEnoughDirection"),
            static_cast<int>(activeObjective_.resistanceGoodEnoughDirection)},
        {QStringLiteral("reactanceGoodEnoughDirection"),
            static_cast<int>(activeObjective_.reactanceGoodEnoughDirection)},
        {QStringLiteral("forwardGainGoodEnoughDirection"),
            static_cast<int>(activeObjective_.forwardGainGoodEnoughDirection)},
        {QStringLiteral("frontToBackGoodEnoughDirection"),
            static_cast<int>(activeObjective_.frontToBackGoodEnoughDirection)},
        {QStringLiteral("frontToRearGoodEnoughDirection"),
            static_cast<int>(activeObjective_.frontToRearGoodEnoughDirection)},
        {QStringLiteral("swrAggregation"), static_cast<int>(activeObjective_.swrAggregation)},
        {QStringLiteral("resistanceAggregation"),
            static_cast<int>(activeObjective_.resistanceAggregation)},
        {QStringLiteral("reactanceAggregation"),
            static_cast<int>(activeObjective_.reactanceAggregation)},
        {QStringLiteral("forwardGainAggregation"),
            static_cast<int>(activeObjective_.forwardGainAggregation)},
        {QStringLiteral("frontToBackAggregation"),
            static_cast<int>(activeObjective_.frontToBackAggregation)},
        {QStringLiteral("frontToRearAggregation"),
            static_cast<int>(activeObjective_.frontToRearAggregation)},
        {QStringLiteral("forwardThetaDegrees"), activeObjective_.forwardThetaDegrees},
        {QStringLiteral("forwardPhiDegrees"), activeObjective_.forwardPhiDegrees},
        {QStringLiteral("directionalEvaluationMode"),
            static_cast<int>(activeObjective_.directionalEvaluationMode)},
        {QStringLiteral("radiationComponent"),
            static_cast<int>(activeObjective_.radiationComponent)},
        {QStringLiteral("frontToRearRegion"),
            static_cast<int>(activeObjective_.frontToRearRegion)},
        {QStringLiteral("frequencyMode"), activeObjective_.kind
                == analysis::OptimizationObjectiveKind::SwrAtFrequency
            ? QStringLiteral("objective-frequency")
            : selectedFrequencyMode() == FrequencyMode::Explicit
                ? QStringLiteral("explicit")
                : selectedFrequencyMode() == FrequencyMode::Continuous
                    ? QStringLiteral("continuous") : QStringLiteral("model-fr")},
        {QStringLiteral("frequenciesMHz"), frequencyArray(activeFrequenciesMHz_)},
        {QStringLiteral("continuousStartMHz"), setup.frequencySelection.continuousStartMHz},
        {QStringLiteral("continuousStopMHz"), setup.frequencySelection.continuousStopMHz},
        {QStringLiteral("continuousStepMHz"), setup.frequencySelection.continuousStepMHz},
    };
    if (baselineRecord_) {
        sessionMetadata.insert(QStringLiteral("baselineRunId"), baselineRecord_->id);
        sessionMetadata.insert(QStringLiteral("baselineModelFingerprint"),
            baselineRecord_->modelFingerprint);
    }
    writeFile(QDir(sessionRecord_->directory).filePath(QStringLiteral("optimization-session.json")),
        QJsonDocument(sessionMetadata).toJson(QJsonDocument::Indented));
    candidates_.clear();
    resultsTable_->clearSelection();
    resetCandidateDetails();
    candidatePlots_->clear();
    resultsTable_->setRowCount(0);
    const auto minimum = setup.minimum;
    const auto maximum = setup.maximum;
    adaptiveVectorSearch_.reset();
    nelderMeadSearch_.reset();
    differentialEvolutionSearch_.reset();
    if (activeSearchMethod_ == SearchMethod::Adaptive) {
        std::vector<analysis::AdaptiveVectorVariable> variables;
        variables.reserve(activeVariables_.size());
        for (const auto& variable : activeVariables_)
            variables.push_back({variable.minimum, variable.maximum, variable.tolerance});
        adaptiveVectorSearch_.emplace(analysis::AdaptiveVectorSearchSettings{
            std::move(variables), candidateLimit, setup.scoreTolerance});
        for (const auto& proposal : adaptiveVectorSearch_->initialCandidates()) {
            const auto role = proposal.variableIndex < 0
                ? QStringLiteral("initial-center")
                : tr("initial %1 %2").arg(activeVariables_[static_cast<std::size_t>(
                      proposal.variableIndex)].name,
                      proposal.direction < 0 ? tr("minimum") : tr("maximum"));
            appendCandidate(proposal.values, 0, role);
        }
    } else if (activeSearchMethod_ == SearchMethod::NelderMead) {
        std::vector<analysis::NelderMeadVariable> variables;
        variables.reserve(activeVariables_.size());
        for (const auto& variable : activeVariables_) {
            variables.push_back({variable.resolvedValue, variable.minimum,
                variable.maximum, variable.tolerance});
        }
        nelderMeadSearch_.emplace(analysis::NelderMeadSettings{
            std::move(variables), candidateLimit, setup.scoreTolerance});
        for (const auto& proposal : nelderMeadSearch_->initialCandidates()) {
            appendCandidate(proposal.values, proposal.iteration,
                QString::fromLatin1(proposal.role.data(), static_cast<qsizetype>(proposal.role.size())));
        }
    } else if (activeSearchMethod_ == SearchMethod::DifferentialEvolution) {
        std::vector<analysis::DifferentialEvolutionVariable> variables;
        variables.reserve(activeVariables_.size());
        for (const auto& variable : activeVariables_) {
            variables.push_back({variable.resolvedValue, variable.minimum,
                variable.maximum, variable.tolerance});
        }
        differentialEvolutionSearch_.emplace(
            analysis::DifferentialEvolutionSettings{
                std::move(variables), setup.populationSize, setup.maximumGenerations,
                setup.mutationFactor, setup.crossoverRate, setup.scoreTolerance, 3,
                static_cast<std::uint32_t>(setup.randomSeed)});
        for (const auto& proposal : differentialEvolutionSearch_->initialCandidates()) {
            appendCandidate(proposal.values, proposal.generation,
                proposal.targetIndex == 0 ? tr("initial model")
                                          : tr("initial population %1")
                                                .arg(proposal.targetIndex + 1));
        }
    } else {
        for (auto index = 0; index < candidateLimit; ++index) {
            const auto fraction = static_cast<double>(index)
                / static_cast<double>(candidateLimit - 1);
            appendCandidate(minimum + fraction * (maximum - minimum));
        }
    }
    const auto count = static_cast<int>(candidates_.size());
    candidateIndex_ = 0;
    bestScore_ = std::numeric_limits<double>::infinity();
    bestRow_ = -1;
    searchStopReason_.clear();
    cancelRequested_ = false;
    progress_->setRange(0, candidateLimit);
    progress_->setValue(0);
    progress_->setFormat(isMultivariable(activeSearchMethod_)
        ? tr("%v of up to %m evaluations") : tr("%v / %m candidates (%p%)"));
    bestLabel_->setText(isMultivariable(activeSearchMethod_)
        ? tr("%1 in progress…").arg(searchMethodName(activeSearchMethod_))
        : tr("Sweep in progress…"));
    statusLabel_->setText(tr("Running %1 with %2 initial candidates × %3 %4 for %5.")
        .arg(activeSearchMethod_ == SearchMethod::ParameterSweep
            ? tr("parameter sweep") : searchMethodName(activeSearchMethod_))
        .arg(count).arg(activeFrequenciesMHz_.size())
        .arg(activeFrequenciesMHz_.size() == 1 ? tr("frequency") : tr("frequencies"))
        .arg(selectedSymbol_));
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
    startNextCandidate();
}

void OptimizationWorkspace::cancelSweep()
{
    cancelRequested_ = true;
    cancelButton_->setEnabled(false);
    if (baselineRunInProgress_) {
        startAfterBaseline_ = false;
        if (cancelBaselineRunCallback_) cancelBaselineRunCallback_();
        return;
    }
    evaluator_->cancel();
}

void OptimizationWorkspace::appendCandidate(double value, int refinementRound,
    QString trialRole)
{
    appendCandidate(std::vector<double>{value}, refinementRound, std::move(trialRole));
}

void OptimizationWorkspace::appendCandidate(std::vector<double> values,
    int refinementRound, QString trialRole)
{
    const auto row = resultsTable_->rowCount();
    resultsTable_->insertRow(row);
    Candidate candidate;
    candidate.value = values.empty() ? 0.0 : values.front();
    candidate.values = std::move(values);
    candidate.row = row;
    candidate.refinementRound = refinementRound;
    candidate.trialRole = std::move(trialRole);
    candidates_.push_back(std::move(candidate));
    const auto candidateIndex = static_cast<int>(candidates_.size()) - 1;
    auto* valueItem = new SortableResultItem(
        candidateDescription(candidates_.back()), candidates_.back().value);
    valueItem->setData(CandidateIndexRole, candidateIndex);
    valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    resultsTable_->setItem(row, ValueColumn, valueItem);
    for (auto column = static_cast<int>(ScoreColumn);
         column <= static_cast<int>(FrontToRearColumn); ++column)
        resultsTable_->setItem(row, column, unavailableResultItem());
    setCandidateStatus(row, tr("Pending"));
}

auto OptimizationWorkspace::candidateDescription(const Candidate& candidate) const -> QString
{
    if (activeVariables_.size() <= 1 || candidate.values.size() <= 1)
        return formatDecimal(candidate.value) + selectedValueSuffix_;
    QStringList values;
    for (auto index = std::size_t{};
         index < activeVariables_.size() && index < candidate.values.size(); ++index) {
        values.append(tr("%1=%2").arg(activeVariables_[index].name,
            formatDecimal(candidate.values[index])));
    }
    return values.join(QStringLiteral("; "));
}

auto OptimizationWorkspace::prepareNextSearchCandidates() -> bool
{
    if (activeSearchMethod_ == SearchMethod::DifferentialEvolution
        && differentialEvolutionSearch_) {
        if (const auto proposal = differentialEvolutionSearch_->nextCandidate()) {
            appendCandidate(proposal->values, proposal->generation,
                tr("population target %1").arg(proposal->targetIndex + 1));
            return true;
        }
        switch (differentialEvolutionSearch_->stopReason()) {
        case analysis::DifferentialEvolutionStopReason::MaximumGenerations:
            searchStopReason_ = tr("the maximum generation limit was reached");
            break;
        case analysis::DifferentialEvolutionStopReason::ParameterTolerance:
            searchStopReason_ = tr(
                "the population contracted within every parameter tolerance");
            break;
        case analysis::DifferentialEvolutionStopReason::ScoreTolerance:
            searchStopReason_ = tr(
                "the best score improved by no more than %1 for three consecutive generations")
                .arg(differentialEvolutionScoreToleranceControl_->value(), 0, 'f',
                    DisplayDecimalPlaces);
            break;
        case analysis::DifferentialEvolutionStopReason::NoSuccessfulCandidate:
            searchStopReason_ = tr("the initial population produced no successful candidate");
            break;
        case analysis::DifferentialEvolutionStopReason::None:
            searchStopReason_ = tr("search completed");
            break;
        }
        return false;
    }
    if (activeSearchMethod_ == SearchMethod::NelderMead && nelderMeadSearch_) {
        if (const auto proposal = nelderMeadSearch_->nextCandidate()) {
            appendCandidate(proposal->values, proposal->iteration,
                QString::fromLatin1(proposal->role.data(),
                    static_cast<qsizetype>(proposal->role.size())));
            return true;
        }
        switch (nelderMeadSearch_->stopReason()) {
        case analysis::NelderMeadStopReason::MaximumEvaluations:
            searchStopReason_ = tr("the maximum evaluation limit was reached");
            break;
        case analysis::NelderMeadStopReason::ParameterTolerance:
            searchStopReason_ = tr("the simplex contracted within every parameter tolerance");
            break;
        case analysis::NelderMeadStopReason::ScoreTolerance:
            searchStopReason_ = tr("the contracted simplex scores differ by no more than %1")
                .arg(adaptiveScoreToleranceControl_->value(), 0, 'f', DisplayDecimalPlaces);
            break;
        case analysis::NelderMeadStopReason::NoSuccessfulCandidate:
            searchStopReason_ = tr("the initial simplex produced no successful candidate");
            break;
        case analysis::NelderMeadStopReason::None:
            searchStopReason_ = tr("search completed");
            break;
        }
        return false;
    }
    if (!adaptiveVectorSearch_) return false;
    const auto proposals = adaptiveVectorSearch_->nextCandidates();
    const auto round = adaptiveVectorSearch_->refinementRound();
    for (const auto& proposal : proposals) {
        const auto variableName = proposal.variableIndex >= 0
            && static_cast<std::size_t>(proposal.variableIndex) < activeVariables_.size()
            ? activeVariables_[static_cast<std::size_t>(proposal.variableIndex)].name : QString{};
        appendCandidate(proposal.values, round,
            tr("%1 %2").arg(variableName,
                proposal.direction < 0 ? tr("lower") : tr("upper")));
    }
    if (!proposals.empty()) return true;
    switch (adaptiveVectorSearch_->stopReason()) {
    case analysis::AdaptiveStopReason::MaximumEvaluations:
        searchStopReason_ = tr("the maximum evaluation limit was reached");
        break;
    case analysis::AdaptiveStopReason::ParameterTolerance:
        searchStopReason_ = tr(
            "no new coordinate trial could satisfy its parameter tolerance");
        break;
    case analysis::AdaptiveStopReason::ScoreTolerance:
        if (const auto& improvements = adaptiveVectorSearch_->scoreImprovements();
            improvements.size() >= 2) {
            searchStopReason_ = tr(
                "no meaningful best-score improvement in two consecutive refinement rounds "
                "(%1 and %2; score tolerance %3)")
                .arg(improvements[improvements.size() - 2], 0, 'f', DisplayDecimalPlaces)
                .arg(improvements.back(), 0, 'f', DisplayDecimalPlaces)
                .arg(adaptiveScoreToleranceControl_->value(), 0, 'f', DisplayDecimalPlaces);
        } else {
            searchStopReason_ = tr(
                "no meaningful best-score improvement in two consecutive refinement rounds "
                "(score tolerance %1)")
                .arg(adaptiveScoreToleranceControl_->value(), 0, 'f', DisplayDecimalPlaces);
        }
        break;
    case analysis::AdaptiveStopReason::NoSuccessfulCandidate:
        searchStopReason_ = tr("no successful candidate was available to refine");
        break;
    case analysis::AdaptiveStopReason::None:
        searchStopReason_ = tr("search completed");
        break;
    }
    return !proposals.empty();
}

void OptimizationWorkspace::startNextCandidate()
{
    if (!cancelRequested_ && candidateIndex_ >= candidates_.size()
        && isMultivariable(activeSearchMethod_) && prepareNextSearchCandidates()) {
        const auto iteration = candidates_.empty()
            ? 0 : candidates_.back().refinementRound;
        progress_->setFormat(tr("%1 iteration %2 · %v of up to %m evaluations")
            .arg(searchMethodName(activeSearchMethod_)).arg(iteration));
    }
    if (cancelRequested_ || candidateIndex_ >= candidates_.size()) {
        finishSweep();
        return;
    }
    auto& candidate = candidates_[candidateIndex_];
    if (isMultivariable(activeSearchMethod_)) {
        statusLabel_->setText(candidate.refinementRound == 0
            ? tr("Initial search: evaluating %1.").arg(candidateDescription(candidate))
            : tr("Iteration %1: evaluating %2 · %3.")
                .arg(candidate.refinementRound)
                .arg(candidate.trialRole)
                .arg(candidateDescription(candidate)));
    }
    candidate.record = runStore_.create(backend_, sourceFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : sourceFile_,
        QStringLiteral("optimization-candidate"), sessionRecord_ ? sessionRecord_->id : QString{});
    if (!writeCandidateMetadata(candidate)) {
        candidate.record.status = QStringLiteral("Failed");
        runStore_.save(candidate.record);
        setCandidateStatus(candidate.row, tr("Could not write candidate metadata"));
        if (activeSearchMethod_ == SearchMethod::Adaptive && adaptiveVectorSearch_)
            adaptiveVectorSearch_->record(candidate.values, std::nullopt);
        if (activeSearchMethod_ == SearchMethod::NelderMead && nelderMeadSearch_)
            nelderMeadSearch_->record(candidate.values, std::nullopt);
        if (activeSearchMethod_ == SearchMethod::DifferentialEvolution
            && differentialEvolutionSearch_)
            differentialEvolutionSearch_->record(candidate.values, std::nullopt);
        ++candidateIndex_;
        progress_->setValue(static_cast<int>(candidateIndex_));
        QTimer::singleShot(0, this, [this] { startNextCandidate(); });
        return;
    }

    candidate.record.status = QStringLiteral("Running");
    runStore_.save(candidate.record);
    setCandidateStatus(candidate.row, tr("Running"));
    std::unordered_map<std::string, double> variableValues;
    for (auto index = std::size_t{};
         index < activeVariables_.size() && index < candidate.values.size(); ++index) {
        variableValues.emplace(activeVariables_[index].name.toStdString(), candidate.values[index]);
    }
    evaluator_->start({
        .authoredSource = source_,
        .variableValues = std::move(variableValues),
        .frequencyPlan = activeObjective_.kind != analysis::OptimizationObjectiveKind::SwrAtFrequency
                && selectedFrequencyMode() == FrequencyMode::ModelSweep
            ? analysis::FrequencyPlan{analysis::FrequencyPlanMode::ModelSweep,
                  activeFrequenciesMHz_, {}}
            : analysis::FrequencyPlan{analysis::FrequencyPlanMode::Explicit,
                  activeFrequenciesMHz_, {}},
        .objective = activeObjective_,
        .backend = backend_,
        .executable = executable_,
        .directory = candidate.record.directory,
        .timeoutSeconds = timeoutSeconds_,
    });
}

void OptimizationWorkspace::finishCurrentCandidate(CandidateEvaluationResult result)
{
    if (candidateIndex_ >= candidates_.size()) return;
    auto& candidate = candidates_[candidateIndex_];
    candidate.record.durationSeconds = result.durationSeconds;
    candidate.record.outputBytes = result.outputBytes;
    candidate.feedpoints = result.analysis.feedpoints;
    candidate.radiation = result.analysis.radiation;
    candidate.analysisLoaded = true;
    candidate.evaluation.reset();
    candidate.availableMeasurements = result.availableMeasurements;
    candidate.failureDetail.clear();
    candidate.record.frequencyCount = result.frequencyCount;
    candidate.record.hasImpedance = !candidate.feedpoints.empty();
    candidate.record.hasRadiation = !candidate.radiation.empty();
    const auto coverage = analysis::optimizationResultCoverage(result.analysis);
    candidate.record.referenceImpedanceOhms = coverage.referenceImpedanceOhms;
    candidate.record.impedanceFrequenciesMHz = coverage.impedanceFrequenciesMHz;
    candidate.record.radiationCoverage = coverage.radiation;

    if (result.status == CandidateEvaluationStatus::Completed && result.objective) {
        const auto& evaluation = *result.objective;
        candidate.evaluation = evaluation;
        resultsTable_->setItem(candidate.row, ScoreColumn, numericItem(evaluation.score));
        resultsTable_->setItem(candidate.row, FrequencyColumn,
            evaluation.evaluatedFrequencyCount == 1
                ? numericItem(evaluation.evaluationFrequencyMHz)
                : unavailableResultItem());
        if (evaluation.feedpoint) {
            resultsTable_->setItem(candidate.row, SwrColumn, numericItem(evaluation.swr));
            resultsTable_->setItem(candidate.row, ResistanceColumn,
                numericItem(evaluation.feedpoint->impedance.real()));
            resultsTable_->setItem(candidate.row, ReactanceColumn,
                numericItem(evaluation.feedpoint->impedance.imag()));
        }
        resultsTable_->setItem(candidate.row, GainColumn, evaluation.forwardGainDb
            ? numericItem(*evaluation.forwardGainDb)
            : unavailableResultItem());
        resultsTable_->setItem(candidate.row, FrontToBackColumn, evaluation.frontToBackDb
            ? numericItem(*evaluation.frontToBackDb)
            : unavailableResultItem());
        resultsTable_->setItem(candidate.row, FrontToRearColumn, evaluation.frontToRearDb
            ? numericItem(*evaluation.frontToRearDb)
            : unavailableResultItem());
        setCandidateStatus(candidate.row, tr("Completed"));
        candidate.record.status = QStringLiteral("Completed");
        if (evaluation.score < bestScore_) {
            bestScore_ = evaluation.score;
            bestRow_ = candidate.row;
        }
        updateCandidatePlots();
        updateCandidateRowToolTip(candidate.row);
        if (candidateDetailsWindow_->isVisible() && detailCandidateRow_ == candidate.row)
            updateCandidateDetails(candidate.row);
    } else {
        QString status;
        switch (result.status) {
        case CandidateEvaluationStatus::ExpressionError: status = tr("Expression error"); break;
        case CandidateEvaluationStatus::InvalidModel: status = tr("Invalid candidate model"); break;
        case CandidateEvaluationStatus::FileError: status = tr("Could not write run files"); break;
        case CandidateEvaluationStatus::CommandError:
        case CandidateEvaluationStatus::FailedToStart:
        case CandidateEvaluationStatus::SolverFailed:
            status = result.detail.isEmpty() ? tr("Solver failed") : result.detail;
            break;
        case CandidateEvaluationStatus::TimedOut: status = tr("Timed out"); break;
        case CandidateEvaluationStatus::Canceled: status = tr("Canceled"); break;
        case CandidateEvaluationStatus::ObjectiveUnavailable:
            status = result.detail.isEmpty()
                ? tr("Required objective results unavailable") : result.detail;
            break;
        case CandidateEvaluationStatus::Completed: status = tr("Solver failed"); break;
        }
        candidate.failureDetail = status;
        setCandidateStatus(candidate.row, status);
        candidate.record.status = result.status == CandidateEvaluationStatus::Canceled
            ? QStringLiteral("Canceled")
            : result.status == CandidateEvaluationStatus::TimedOut
                ? QStringLiteral("Timed Out") : QStringLiteral("Failed");
        candidate.record.summary = status;
        if (candidate.availableMeasurements) {
            const auto& measurements = *candidate.availableMeasurements;
            resultsTable_->setItem(candidate.row, FrequencyColumn,
                measurements.evaluatedFrequencyCount == 1
                    ? numericItem(measurements.evaluationFrequencyMHz)
                    : unavailableResultItem());
            if (measurements.feedpoint) {
                resultsTable_->setItem(candidate.row, SwrColumn,
                    numericItem(measurements.swr));
                resultsTable_->setItem(candidate.row, ResistanceColumn,
                    numericItem(measurements.feedpoint->impedance.real()));
                resultsTable_->setItem(candidate.row, ReactanceColumn,
                    numericItem(measurements.feedpoint->impedance.imag()));
            }
            if (measurements.forwardGainDb)
                resultsTable_->setItem(candidate.row, GainColumn,
                    numericItem(*measurements.forwardGainDb));
            if (measurements.frontToBackDb)
                resultsTable_->setItem(candidate.row, FrontToBackColumn,
                    numericItem(*measurements.frontToBackDb));
            if (measurements.frontToRearDb)
                resultsTable_->setItem(candidate.row, FrontToRearColumn,
                    numericItem(*measurements.frontToRearDb));
        }
        updateCandidateRowToolTip(candidate.row);
    }
    if (activeSearchMethod_ == SearchMethod::Adaptive && adaptiveVectorSearch_)
        adaptiveVectorSearch_->record(candidate.values,
            candidate.evaluation
                ? std::optional<double>{candidate.evaluation->score} : std::nullopt);
    if (activeSearchMethod_ == SearchMethod::NelderMead && nelderMeadSearch_)
        nelderMeadSearch_->record(candidate.values,
            candidate.evaluation
                ? std::optional<double>{candidate.evaluation->score} : std::nullopt);
    if (activeSearchMethod_ == SearchMethod::DifferentialEvolution
        && differentialEvolutionSearch_)
        differentialEvolutionSearch_->record(candidate.values,
            candidate.evaluation
                ? std::optional<double>{candidate.evaluation->score} : std::nullopt);
    writeObjectiveResultCache(candidate.record.directory,
        candidate.evaluation, candidate.availableMeasurements);
    runStore_.save(candidate.record);
    ++candidateIndex_;
    progress_->setValue(static_cast<int>(candidateIndex_));
    if (sessionRecord_) {
        sessionRecord_->summary = tr("%1 %2 · %3/%4 candidates complete")
            .arg(activeSearchMethod_ == SearchMethod::ParameterSweep
                ? tr("Parameter sweep") : searchMethodName(activeSearchMethod_))
            .arg(selectedSymbol_).arg(candidateIndex_).arg(candidates_.size());
        runStore_.save(*sessionRecord_);
    }
    QTimer::singleShot(0, this, [this] { startNextCandidate(); });
}

void OptimizationWorkspace::finishSweep()
{
    const auto boundaryDescription = bestBoundaryDescription();
    if (!cancelRequested_ && !boundaryDescription.isEmpty()) {
        if (!searchStopReason_.isEmpty()) searchStopReason_ += QStringLiteral(". ");
        searchStopReason_ += boundaryDescription;
    }
    if (cancelRequested_) {
        for (std::size_t index = candidateIndex_; index < candidates_.size(); ++index)
            setCandidateStatus(candidates_[index].row, tr("Skipped"));
        statusLabel_->setText(isMultivariable(activeSearchMethod_)
            ? tr("%1 canceled.").arg(searchMethodName(activeSearchMethod_))
            : tr("Parameter sweep canceled."));
        progress_->setFormat(isMultivariable(activeSearchMethod_)
            ? tr("Canceled — %v of up to %m evaluations")
            : tr("Canceled — %v / %m candidates"));
    } else {
        statusLabel_->setText(isMultivariable(activeSearchMethod_)
            ? tr("%1 complete: %2.").arg(
                searchMethodName(activeSearchMethod_), searchStopReason_)
            : searchStopReason_.isEmpty() ? tr("Parameter sweep complete.")
                                          : tr("Parameter sweep complete. %1.")
                                                .arg(searchStopReason_));
        if (isMultivariable(activeSearchMethod_)) {
            const auto evaluations = static_cast<int>(candidates_.size());
            progress_->setRange(0, std::max(1, evaluations));
            progress_->setValue(evaluations);
            progress_->setFormat(tr("Complete — %v evaluations"));
        } else {
            progress_->setFormat(tr("Complete — %v candidates"));
        }
    }
    candidateIndex_ = candidates_.size();
    if (bestRow_ >= 0) {
        const auto bestTableRow = tableRowForCandidateIndex(bestRow_);
        auto* bestValueItem = bestTableRow < 0
            ? nullptr : resultsTable_->item(bestTableRow, ValueColumn);
        auto font = bestValueItem == nullptr ? resultsTable_->font() : bestValueItem->font();
        font.setBold(true);
        if (bestTableRow >= 0) {
            for (auto column = 0; column < ResultColumnCount; ++column) {
                if (auto* item = resultsTable_->item(bestTableRow, column)) item->setFont(font);
            }
        }
        bestLabel_->setText(tr("Best candidate: %1, objective score = %2")
            .arg(candidateDescription(candidates_[static_cast<std::size_t>(bestRow_)]))
            .arg(bestScore_, 0, 'f', 3));
        applyCandidateSort();
        const auto sortedBestRow = tableRowForCandidateIndex(bestRow_);
        if (sortedBestRow >= 0) {
            resultsTable_->setCurrentCell(sortedBestRow, ValueColumn);
            resultsTable_->selectRow(sortedBestRow);
            resultsTable_->scrollToItem(resultsTable_->item(sortedBestRow, ValueColumn),
                QAbstractItemView::PositionAtCenter);
        }
    } else {
        const auto firstFailure = std::ranges::find_if(candidates_, [](const Candidate& candidate) {
            return !candidate.failureDetail.isEmpty();
        });
        const auto failureCount = std::ranges::count_if(candidates_, [](const Candidate& candidate) {
            return !candidate.failureDetail.isEmpty();
        });
        bestLabel_->setText(firstFailure != candidates_.end()
            ? tr("No candidate completed successfully. %1 of %2 failed. First failure: %3")
                  .arg(static_cast<qulonglong>(failureCount))
                  .arg(static_cast<qulonglong>(candidates_.size()))
                  .arg(firstFailure->failureDetail)
            : tr("No successful candidate produced the required objective results."));
    }
    if (sessionRecord_) {
        sessionRecord_->candidateCount = static_cast<int>(candidates_.size());
        sessionRecord_->status = cancelRequested_ ? QStringLiteral("Canceled") : QStringLiteral("Completed");
        sessionRecord_->summary = searchStopReason_.isEmpty()
            ? bestLabel_->text()
            : isMultivariable(activeSearchMethod_)
                ? tr("%1 · Stopped: %2").arg(bestLabel_->text(), searchStopReason_)
                : tr("%1 · Note: %2").arg(bestLabel_->text(), searchStopReason_);
        saveSessionCompletionMetadata();
        runStore_.save(*sessionRecord_);
        if (runsChangedCallback_) runsChangedCallback_();
        sessionRecord_.reset();
    }
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
}

auto OptimizationWorkspace::bestBoundaryDescription() const -> QString
{
    if (bestRow_ < 0 || static_cast<std::size_t>(bestRow_) >= candidates_.size()) return {};
    const auto& values = candidates_[static_cast<std::size_t>(bestRow_)].values;
    QStringList lowerBounds;
    QStringList upperBounds;
    for (auto index = std::size_t{};
         index < values.size() && index < activeVariables_.size(); ++index) {
        const auto& variable = activeVariables_[index];
        const auto scale = std::max({1.0, std::abs(variable.minimum),
            std::abs(variable.maximum)});
        const auto epsilon = scale * 1.0e-9;
        if (std::abs(values[index] - variable.minimum) <= epsilon)
            lowerBounds.append(variable.name);
        if (std::abs(values[index] - variable.maximum) <= epsilon)
            upperBounds.append(variable.name);
    }
    QStringList messages;
    if (!lowerBounds.isEmpty()) {
        messages.append(tr("Best candidate reached the lower bound for %1")
            .arg(lowerBounds.join(QStringLiteral(", "))));
    }
    if (!upperBounds.isEmpty()) {
        messages.append(tr("Best candidate reached the upper bound for %1")
            .arg(upperBounds.join(QStringLiteral(", "))));
    }
    if (messages.isEmpty()) return {};
    return messages.join(tr("; ")) + tr("; consider expanding the search range");
}

auto OptimizationWorkspace::selectedSearchMethod() const -> SearchMethod
{
    return static_cast<SearchMethod>(searchMethodControl_->currentData().toInt());
}

auto OptimizationWorkspace::currentStudySetup() const -> StudySetup
{
    const auto searchMethod = selectedSearchMethod();
    const auto differentialEvolution = searchMethod == SearchMethod::DifferentialEvolution;
    auto variables = selectedVariables();
    const auto sweepVariable = variables.empty()
        ? StudySetup::VariableRange{} : variables.front();
    return {
        .searchMethod = searchMethod,
        .variable = sweepVariable.name,
        .minimum = sweepVariable.minimum,
        .maximum = sweepVariable.maximum,
        .candidateLimit = differentialEvolution
            ? differentialEvolutionPopulationControl_->value()
                * (differentialEvolutionGenerationControl_->value() + 1)
            : isMultivariable(searchMethod)
                ? adaptiveMaximumEvaluationsControl_->value() : pointsControl_->value(),
        .scoreTolerance = differentialEvolution
            ? differentialEvolutionScoreToleranceControl_->value()
            : adaptiveScoreToleranceControl_->value(),
        .populationSize = differentialEvolutionPopulationControl_->value(),
        .maximumGenerations = differentialEvolutionGenerationControl_->value(),
        .mutationFactor = differentialEvolutionMutationControl_->value(),
        .crossoverRate = differentialEvolutionCrossoverControl_->value(),
        .randomSeed = differentialEvolutionSeedControl_->value(),
        .variables = std::move(variables),
        .frequencySelection = captureFrequencySelection(),
        .frequenciesMHz = analysis::frequencyPlanPoints(selectedFrequencyPlan()),
        .objective = selectedObjective(),
    };
}

auto OptimizationWorkspace::isMultivariable(SearchMethod method) noexcept -> bool
{
    return method != SearchMethod::ParameterSweep;
}

auto OptimizationWorkspace::searchMethodName(SearchMethod method) const -> QString
{
    switch (method) {
    case SearchMethod::ParameterSweep: return tr("Parameter Sweep");
    case SearchMethod::Adaptive: return tr("Adaptive Optimize");
    case SearchMethod::NelderMead: return tr("Nelder–Mead");
    case SearchMethod::DifferentialEvolution: return tr("Differential Evolution");
    }
    return tr("Optimization");
}

auto OptimizationWorkspace::searchMethodId(SearchMethod method) -> QString
{
    switch (method) {
    case SearchMethod::ParameterSweep: return QStringLiteral("parameter-sweep");
    case SearchMethod::Adaptive: return QStringLiteral("adaptive");
    case SearchMethod::NelderMead: return QStringLiteral("nelder-mead");
    case SearchMethod::DifferentialEvolution:
        return QStringLiteral("differential-evolution");
    }
    return QStringLiteral("parameter-sweep");
}

auto OptimizationWorkspace::currentSearchIteration() const noexcept -> int
{
    if (adaptiveVectorSearch_) return adaptiveVectorSearch_->refinementRound();
    if (nelderMeadSearch_) return nelderMeadSearch_->iteration();
    if (differentialEvolutionSearch_) return differentialEvolutionSearch_->generation();
    return 0;
}

auto OptimizationWorkspace::currentStopReasonId() const -> QString
{
    if (differentialEvolutionSearch_) {
        switch (differentialEvolutionSearch_->stopReason()) {
        case analysis::DifferentialEvolutionStopReason::MaximumGenerations:
            return QStringLiteral("maximum-generations");
        case analysis::DifferentialEvolutionStopReason::ParameterTolerance:
            return QStringLiteral("parameter-tolerance");
        case analysis::DifferentialEvolutionStopReason::ScoreTolerance:
            return QStringLiteral("score-tolerance");
        case analysis::DifferentialEvolutionStopReason::NoSuccessfulCandidate:
            return QStringLiteral("no-successful-candidate");
        case analysis::DifferentialEvolutionStopReason::None: break;
        }
    }
    if (nelderMeadSearch_) {
        switch (nelderMeadSearch_->stopReason()) {
        case analysis::NelderMeadStopReason::MaximumEvaluations:
            return QStringLiteral("maximum-evaluations");
        case analysis::NelderMeadStopReason::ParameterTolerance:
            return QStringLiteral("parameter-tolerance");
        case analysis::NelderMeadStopReason::ScoreTolerance:
            return QStringLiteral("score-tolerance");
        case analysis::NelderMeadStopReason::NoSuccessfulCandidate:
            return QStringLiteral("no-successful-candidate");
        case analysis::NelderMeadStopReason::None: break;
        }
    }
    if (adaptiveVectorSearch_) {
        switch (adaptiveVectorSearch_->stopReason()) {
        case analysis::AdaptiveStopReason::MaximumEvaluations:
            return QStringLiteral("maximum-evaluations");
        case analysis::AdaptiveStopReason::ParameterTolerance:
            return QStringLiteral("parameter-tolerance");
        case analysis::AdaptiveStopReason::ScoreTolerance:
            return QStringLiteral("score-tolerance");
        case analysis::AdaptiveStopReason::NoSuccessfulCandidate:
            return QStringLiteral("no-successful-candidate");
        case analysis::AdaptiveStopReason::None: break;
        }
    }
    return QStringLiteral("completed");
}

}
