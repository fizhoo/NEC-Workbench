#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/FileIo.h"
#include "ui/optimization/OptimizationComparePanel.h"
#include "ui/optimization/OptimizationWorkspaceSupport.h"

#include "analysis/AnalysisResult.h"
#include "analysis/NecOutputParser.h"
#include "analysis/OptimizationObjective.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace necwb::ui {
using namespace detail;

namespace {

auto optionalNumber(const QJsonObject& object, const QString& key)
    -> std::optional<double>
{
    const auto value = object.value(key);
    return value.isDouble() ? std::optional<double>{value.toDouble()} : std::nullopt;
}

auto metricSummaryFromJson(const QJsonValue& value)
    -> std::optional<analysis::OptimizationMetricSummary>
{
    if (!value.isObject()) return std::nullopt;
    const auto object = value.toObject();
    return analysis::OptimizationMetricSummary{
        object.value(QStringLiteral("minimum")).toDouble(),
        object.value(QStringLiteral("average")).toDouble(),
        object.value(QStringLiteral("maximum")).toDouble(),
        object.value(QStringLiteral("minimumFrequencyMHz")).toDouble(),
        object.value(QStringLiteral("maximumFrequencyMHz")).toDouble()};
}

auto objectiveResultFromJson(const QJsonValue& value)
    -> std::optional<analysis::OptimizationObjectiveResult>
{
    if (!value.isObject()) return std::nullopt;
    const auto object = value.toObject();
    if (!object.contains(QStringLiteral("score"))) return std::nullopt;
    analysis::OptimizationObjectiveResult result;
    result.score = object.value(QStringLiteral("score")).toDouble();
    result.evaluationFrequencyMHz = object.value(
        QStringLiteral("evaluationFrequencyMHz")).toDouble();
    result.evaluatedFrequencyCount = static_cast<std::size_t>(object.value(
        QStringLiteral("evaluatedFrequencyCount")).toInteger());
    result.swr = object.value(QStringLiteral("swr")).toDouble();
    result.swrComponent = object.value(QStringLiteral("swrComponent")).toDouble();
    result.resistanceComponent = object.value(
        QStringLiteral("resistanceComponent")).toDouble();
    result.reactanceComponent = object.value(
        QStringLiteral("reactanceComponent")).toDouble();
    result.forwardGainComponent = object.value(
        QStringLiteral("forwardGainComponent")).toDouble();
    result.frontToBackComponent = object.value(
        QStringLiteral("frontToBackComponent")).toDouble();
    result.frontToRearComponent = object.value(
        QStringLiteral("frontToRearComponent")).toDouble();
    result.forwardGainDb = optionalNumber(object, QStringLiteral("forwardGainDb"));
    result.forwardGainFrequencyMHz = optionalNumber(
        object, QStringLiteral("forwardGainFrequencyMHz"));
    result.forwardThetaDegrees = optionalNumber(object, QStringLiteral("forwardThetaDegrees"));
    result.forwardPhiDegrees = optionalNumber(object, QStringLiteral("forwardPhiDegrees"));
    result.backGainDb = optionalNumber(object, QStringLiteral("backGainDb"));
    result.frontToBackDb = optionalNumber(object, QStringLiteral("frontToBackDb"));
    result.frontToBackFrequencyMHz = optionalNumber(
        object, QStringLiteral("frontToBackFrequencyMHz"));
    result.rearGainDb = optionalNumber(object, QStringLiteral("rearGainDb"));
    result.frontToRearDb = optionalNumber(object, QStringLiteral("frontToRearDb"));
    result.frontToRearFrequencyMHz = optionalNumber(
        object, QStringLiteral("frontToRearFrequencyMHz"));
    result.swrSummary = metricSummaryFromJson(object.value(QStringLiteral("swrSummary")));
    result.forwardGainSummary = metricSummaryFromJson(
        object.value(QStringLiteral("forwardGainSummary")));
    result.frontToBackSummary = metricSummaryFromJson(
        object.value(QStringLiteral("frontToBackSummary")));
    result.frontToRearSummary = metricSummaryFromJson(
        object.value(QStringLiteral("frontToRearSummary")));
    const auto feedpoint = object.value(QStringLiteral("feedpoint"));
    if (feedpoint.isObject()) {
        const auto feedpointObject = feedpoint.toObject();
        analysis::FeedpointResult value;
        value.frequencyMHz = feedpointObject.value(QStringLiteral("frequencyMHz")).toDouble();
        value.impedance = {feedpointObject.value(QStringLiteral("resistanceOhms")).toDouble(),
            feedpointObject.value(QStringLiteral("reactanceOhms")).toDouble()};
        result.feedpoint = value;
    }
    for (const auto value : object.value(QStringLiteral("frequencyMetrics")).toArray()) {
        const auto metric = value.toObject();
        result.frequencyMetrics.push_back({
            metric.value(QStringLiteral("frequencyMHz")).toDouble(),
            optionalNumber(metric, QStringLiteral("swr")),
            optionalNumber(metric, QStringLiteral("resistanceOhms")),
            optionalNumber(metric, QStringLiteral("reactanceOhms")),
            optionalNumber(metric, QStringLiteral("forwardGainDb")),
            optionalNumber(metric, QStringLiteral("frontToBackDb")),
            optionalNumber(metric, QStringLiteral("frontToRearDb")),
            optionalNumber(metric, QStringLiteral("forwardThetaDegrees")),
            optionalNumber(metric, QStringLiteral("forwardPhiDegrees"))});
    }
    return result;
}

auto objectiveKind(const QString& id) -> analysis::OptimizationObjectiveKind
{
    if (id == QStringLiteral("minimize-swr-at-frequency"))
        return analysis::OptimizationObjectiveKind::SwrAtFrequency;
    if (id == QStringLiteral("average-across-frequencies"))
        return analysis::OptimizationObjectiveKind::AverageAcrossFrequencies;
    if (id == QStringLiteral("per-criterion"))
        return analysis::OptimizationObjectiveKind::PerCriterion;
    return analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies;
}

}

auto OptimizationWorkspace::loadSession(const QString& sessionId) -> bool
{
    auto records = runStore_.load();
    const auto session = std::ranges::find(records, sessionId, &AnalysisRunRecord::id);
    if (session == records.end() || session->runType != QStringLiteral("optimization-session")) return false;
    setUpdatesEnabled(false);
    const QSignalBlocker resultsBlocker(resultsTable_);
    if (!historicalSession_) activeFrequencySelection_ = captureFrequencySelection();
    std::vector<AnalysisRunRecord> candidateRecords;
    for (const auto& record : records) {
        if (record.parentId == sessionId) candidateRecords.push_back(record);
    }
    std::ranges::sort(candidateRecords, {}, &AnalysisRunRecord::started);

    QFile sessionMetadataFile(QDir(session->directory).filePath(
        QStringLiteral("optimization-session.json")));
    const auto sessionMetadata = sessionMetadataFile.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(sessionMetadataFile.readAll()).object() : QJsonObject{};
    const auto storedSearchMethod = sessionMetadata.value(
        QStringLiteral("searchMethod")).toString();
    const auto restoredSearchMethod = storedSearchMethod == QStringLiteral("differential-evolution")
        ? SearchMethod::DifferentialEvolution
        : storedSearchMethod == QStringLiteral("nelder-mead")
            ? SearchMethod::NelderMead
            : storedSearchMethod == QStringLiteral("adaptive")
                ? SearchMethod::Adaptive : SearchMethod::ParameterSweep;
    searchStopReason_ = sessionMetadata.value(
        QStringLiteral("stopDescription")).toString();
    searchMethodControl_->setCurrentIndex(searchMethodControl_->findData(
        static_cast<int>(restoredSearchMethod)));
    activeSearchMethod_ = restoredSearchMethod;
    adaptiveMaximumEvaluationsControl_->setValue(sessionMetadata.value(
        QStringLiteral("candidateCount")).toInt(21));
    adaptiveScoreToleranceControl_->setValue(sessionMetadata.value(
        QStringLiteral("scoreTolerance")).toDouble(0.001));
    differentialEvolutionPopulationControl_->setValue(sessionMetadata.value(
        QStringLiteral("populationSize")).toInt(12));
    differentialEvolutionGenerationControl_->setValue(sessionMetadata.value(
        QStringLiteral("maximumGenerations")).toInt(20));
    differentialEvolutionMutationControl_->setValue(sessionMetadata.value(
        QStringLiteral("mutationFactor")).toDouble(0.8));
    differentialEvolutionCrossoverControl_->setValue(sessionMetadata.value(
        QStringLiteral("crossoverRate")).toDouble(0.9));
    differentialEvolutionScoreToleranceControl_->setValue(sessionMetadata.value(
        QStringLiteral("scoreTolerance")).toDouble(0.001));
    differentialEvolutionSeedControl_->setValue(sessionMetadata.value(
        QStringLiteral("randomSeed")).toInt(5489));
    selectedSymbol_ = sessionMetadata.value(QStringLiteral("variable")).toString();
    activeVariables_.clear();
    for (const auto value : sessionMetadata.value(QStringLiteral("variables")).toArray()) {
        const auto variable = value.toObject();
        activeVariables_.push_back({
            variable.value(QStringLiteral("name")).toString(),
            variable.value(QStringLiteral("resolvedValue")).toDouble(),
            variable.value(QStringLiteral("minimum")).toDouble(),
            variable.value(QStringLiteral("maximum")).toDouble(),
            variable.value(QStringLiteral("tolerance")).toDouble(0.01),
        });
    }
    if (activeVariables_.empty() && !selectedSymbol_.isEmpty()) {
        activeVariables_.push_back({selectedSymbol_, 0.0,
            sessionMetadata.value(QStringLiteral("minimum")).toDouble(),
            sessionMetadata.value(QStringLiteral("maximum")).toDouble(),
            sessionMetadata.value(QStringLiteral("parameterTolerance")).toDouble(0.01)});
    }
    activeFrequenciesMHz_.clear();
    for (const auto value : sessionMetadata.value(QStringLiteral("frequenciesMHz")).toArray()) {
        const auto frequencyMHz = value.toDouble();
        if (std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            activeFrequenciesMHz_.push_back(frequencyMHz);
    }
    const auto storedFrequencyMode = sessionMetadata.value(
        QStringLiteral("frequencyMode")).toString();
    const auto restoredMode = storedFrequencyMode == QStringLiteral("explicit")
        ? FrequencyMode::Explicit
        : storedFrequencyMode == QStringLiteral("continuous")
            ? FrequencyMode::Continuous : FrequencyMode::ModelSweep;
    const auto modeIndex = frequencyModeControl_->findData(static_cast<int>(restoredMode));
    if (modeIndex >= 0) frequencyModeControl_->setCurrentIndex(modeIndex);
    if (restoredMode == FrequencyMode::Explicit)
        setExplicitFrequencies(activeFrequenciesMHz_);
    if (restoredMode == FrequencyMode::Continuous) {
        continuousStartControl_->setValue(sessionMetadata.value(
            QStringLiteral("continuousStartMHz")).toDouble(14.0));
        continuousStopControl_->setValue(sessionMetadata.value(
            QStringLiteral("continuousStopMHz")).toDouble(14.35));
        continuousStepControl_->setValue(sessionMetadata.value(
            QStringLiteral("continuousStepMHz")).toDouble(0.05));
    }

    candidates_.clear();
    resultsTable_->setRowCount(static_cast<int>(candidateRecords.size()));
    bestScore_ = std::numeric_limits<double>::infinity();
    bestRow_ = -1;
    auto restoredObjective = analysis::OptimizationObjectiveSpec{};
    for (std::size_t index = 0; index < candidateRecords.size(); ++index) {
        const auto row = static_cast<int>(index);
        for (auto column = static_cast<int>(ScoreColumn);
             column <= static_cast<int>(FrontToRearColumn); ++column)
            resultsTable_->setItem(row, column, unavailableResultItem());
        QFile metadataFile(QDir(candidateRecords[index].directory).filePath(
            QStringLiteral("optimization.json")));
        const auto metadata = metadataFile.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(metadataFile.readAll()).object() : QJsonObject{};
        const auto value = metadata.value(QStringLiteral("value")).toDouble();
        if (selectedSymbol_.isEmpty()) selectedSymbol_ = metadata.value(QStringLiteral("variable")).toString();
        selectedValueSuffix_ = metadata.value(QStringLiteral("unit")).toString();
        if (!selectedValueSuffix_.isEmpty()) selectedValueSuffix_.prepend(' ');
        std::vector<double> candidateValues;
        for (const auto variableValue : metadata.value(QStringLiteral("variables")).toArray())
            candidateValues.push_back(variableValue.toObject().value(QStringLiteral("value")).toDouble());
        if (candidateValues.empty()) candidateValues.push_back(value);
        analysis::AnalysisResult result;
        auto evaluation = objectiveResultFromJson(
            metadata.value(QStringLiteral("objectiveEvaluation")));
        auto availableMeasurements = objectiveResultFromJson(
            metadata.value(QStringLiteral("availableMeasurements")));
        auto analysisLoaded = false;
        const auto objectiveId = metadata.value(QStringLiteral("objective")).toString();
        restoredObjective = {};
        restoredObjective.kind = objectiveKind(objectiveId);
        restoredObjective.referenceImpedance = metadata.value(
            QStringLiteral("referenceImpedance")).toDouble(50.0);
        restoredObjective.targetFrequencyMHz = metadata.value(
            QStringLiteral("targetFrequencyMHz")).toDouble();
        restoredObjective.swrWeight = metadata.value(
            QStringLiteral("swrWeight")).toDouble(1.0);
        restoredObjective.resistanceWeight = metadata.value(
            QStringLiteral("resistanceWeight")).toDouble();
        restoredObjective.resistanceTargetOhms = metadata.value(
            QStringLiteral("resistanceTargetOhms")).toDouble(
                restoredObjective.referenceImpedance);
        restoredObjective.reactanceWeight = metadata.value(
            QStringLiteral("reactanceWeight")).toDouble();
        restoredObjective.reactanceTargetOhms = metadata.value(
            QStringLiteral("reactanceTargetOhms")).toDouble();
        restoredObjective.forwardGainWeight = metadata.value(
            QStringLiteral("forwardGainWeight")).toDouble();
        restoredObjective.frontToBackWeight = metadata.value(
            QStringLiteral("frontToBackWeight")).toDouble();
        restoredObjective.frontToRearWeight = metadata.value(
            QStringLiteral("frontToRearWeight")).toDouble();
        restoredObjective.forwardThetaDegrees = metadata.value(
            QStringLiteral("forwardThetaDegrees")).toDouble(90.0);
        restoredObjective.forwardPhiDegrees = metadata.value(
            QStringLiteral("forwardPhiDegrees")).toDouble();
        restoredObjective.directionalEvaluationMode =
            static_cast<analysis::DirectionalEvaluationMode>(metadata.value(
                QStringLiteral("directionalEvaluationMode")).toInt(
                    static_cast<int>(analysis::DirectionalEvaluationMode::SpecifiedDirection)));
        restoredObjective.radiationComponent = static_cast<analysis::RadiationComponent>(
            metadata.value(QStringLiteral("radiationComponent")).toInt(
                static_cast<int>(analysis::RadiationComponent::Total)));
        restoredObjective.frontToRearRegion = static_cast<analysis::FrontToRearRegion>(
            metadata.value(QStringLiteral("frontToRearRegion")).toInt(
                static_cast<int>(analysis::FrontToRearRegion::ForwardAzimuthCut)));
        if (metadata.contains(QStringLiteral("swrGoal"))) {
            restoredObjective.swrGoal = static_cast<analysis::OptimizationGoal>(
                metadata.value(QStringLiteral("swrGoal")).toInt());
            restoredObjective.resistanceGoal = static_cast<analysis::OptimizationGoal>(
                metadata.value(QStringLiteral("resistanceGoal")).toInt());
            restoredObjective.reactanceGoal = static_cast<analysis::OptimizationGoal>(
                metadata.value(QStringLiteral("reactanceGoal")).toInt());
            restoredObjective.forwardGainGoal = static_cast<analysis::OptimizationGoal>(
                metadata.value(QStringLiteral("forwardGainGoal")).toInt());
            restoredObjective.frontToBackGoal = static_cast<analysis::OptimizationGoal>(
                metadata.value(QStringLiteral("frontToBackGoal")).toInt());
            restoredObjective.frontToRearGoal = static_cast<analysis::OptimizationGoal>(
                metadata.value(QStringLiteral("frontToRearGoal")).toInt());
            restoredObjective.swrTarget = metadata.value(
                QStringLiteral("swrTarget")).toDouble(2.0);
            restoredObjective.forwardGainTarget = metadata.value(
                QStringLiteral("forwardGainTarget")).toDouble();
            restoredObjective.frontToBackTarget = metadata.value(
                QStringLiteral("frontToBackTarget")).toDouble(20.0);
            restoredObjective.frontToRearTarget = metadata.value(
                QStringLiteral("frontToRearTarget")).toDouble(15.0);
            restoredObjective.swrGoodEnoughDirection = static_cast<analysis::GoodEnoughDirection>(
                metadata.value(QStringLiteral("swrGoodEnoughDirection")).toInt());
            restoredObjective.resistanceGoodEnoughDirection = static_cast<analysis::GoodEnoughDirection>(
                metadata.value(QStringLiteral("resistanceGoodEnoughDirection")).toInt());
            restoredObjective.reactanceGoodEnoughDirection = static_cast<analysis::GoodEnoughDirection>(
                metadata.value(QStringLiteral("reactanceGoodEnoughDirection")).toInt());
            restoredObjective.forwardGainGoodEnoughDirection = static_cast<analysis::GoodEnoughDirection>(
                metadata.value(QStringLiteral("forwardGainGoodEnoughDirection")).toInt(1));
            restoredObjective.frontToBackGoodEnoughDirection = static_cast<analysis::GoodEnoughDirection>(
                metadata.value(QStringLiteral("frontToBackGoodEnoughDirection")).toInt(1));
            restoredObjective.frontToRearGoodEnoughDirection = static_cast<analysis::GoodEnoughDirection>(
                metadata.value(QStringLiteral("frontToRearGoodEnoughDirection")).toInt(1));
            restoredObjective.swrAggregation = static_cast<analysis::OptimizationAggregation>(
                metadata.value(QStringLiteral("swrAggregation")).toInt(2));
            restoredObjective.resistanceAggregation = static_cast<analysis::OptimizationAggregation>(
                metadata.value(QStringLiteral("resistanceAggregation")).toInt(2));
            restoredObjective.reactanceAggregation = static_cast<analysis::OptimizationAggregation>(
                metadata.value(QStringLiteral("reactanceAggregation")).toInt(2));
            restoredObjective.forwardGainAggregation = static_cast<analysis::OptimizationAggregation>(
                metadata.value(QStringLiteral("forwardGainAggregation")).toInt());
            restoredObjective.frontToBackAggregation = static_cast<analysis::OptimizationAggregation>(
                metadata.value(QStringLiteral("frontToBackAggregation")).toInt());
            restoredObjective.frontToRearAggregation = static_cast<analysis::OptimizationAggregation>(
                metadata.value(QStringLiteral("frontToRearAggregation")).toInt());
        } else if (restoredObjective.kind
            == analysis::OptimizationObjectiveKind::AverageAcrossFrequencies) {
            restoredObjective.swrAggregation = analysis::OptimizationAggregation::Average;
            restoredObjective.resistanceAggregation = analysis::OptimizationAggregation::Average;
            restoredObjective.reactanceAggregation = analysis::OptimizationAggregation::Average;
            restoredObjective.forwardGainAggregation = analysis::OptimizationAggregation::Average;
            restoredObjective.frontToBackAggregation = analysis::OptimizationAggregation::Average;
            restoredObjective.frontToRearAggregation = analysis::OptimizationAggregation::Average;
        }
        if (!evaluation && !availableMeasurements) {
            QFile outputFile(QDir(candidateRecords[index].directory).filePath(
                QStringLiteral("model.out")));
            if (outputFile.open(QIODevice::ReadOnly)) {
                result = analysis::NecOutputParser{}.parse(
                    outputFile.readAll().toStdString());
                analysisLoaded = true;
                evaluation = analysis::evaluateOptimizationObjective(
                    result, restoredObjective);
                writeObjectiveResultCache(candidateRecords[index].directory,
                    evaluation, std::nullopt);
            }
        }
        if (evaluation) {
            resultsTable_->setItem(row, ScoreColumn, numericItem(evaluation->score));
            resultsTable_->setItem(row, FrequencyColumn,
                evaluation->evaluatedFrequencyCount == 1
                    ? numericItem(evaluation->evaluationFrequencyMHz)
                    : unavailableResultItem());
            if (evaluation->feedpoint) {
                resultsTable_->setItem(row, SwrColumn, numericItem(evaluation->swr));
                resultsTable_->setItem(row, ResistanceColumn,
                    numericItem(evaluation->feedpoint->impedance.real()));
                resultsTable_->setItem(row, ReactanceColumn,
                    numericItem(evaluation->feedpoint->impedance.imag()));
            }
            resultsTable_->setItem(row, GainColumn, evaluation->forwardGainDb
                ? numericItem(*evaluation->forwardGainDb)
                : unavailableResultItem());
            resultsTable_->setItem(row, FrontToBackColumn, evaluation->frontToBackDb
                ? numericItem(*evaluation->frontToBackDb)
                : unavailableResultItem());
            resultsTable_->setItem(row, FrontToRearColumn, evaluation->frontToRearDb
                ? numericItem(*evaluation->frontToRearDb)
                : unavailableResultItem());
            if (evaluation->score < bestScore_) {
                bestScore_ = evaluation->score;
                bestRow_ = row;
            }
        }
        Candidate candidate;
        candidate.value = value;
        candidate.values = std::move(candidateValues);
        candidate.row = row;
        candidate.record = candidateRecords[index];
        candidate.feedpoints = std::move(result.feedpoints);
        candidate.radiation = std::move(result.radiation);
        candidate.evaluation = evaluation;
        candidate.availableMeasurements = availableMeasurements;
        candidate.analysisLoaded = analysisLoaded;
        candidate.refinementRound = metadata.value(QStringLiteral("refinementRound")).toInt();
        candidate.trialRole = metadata.value(QStringLiteral("trialRole")).toString();
        if (!evaluation && candidateRecords[index].status != QStringLiteral("Completed"))
            candidate.failureDetail = candidateRecords[index].summary;
        candidates_.push_back(std::move(candidate));
        auto* valueItem = new SortableResultItem(
            candidateDescription(candidates_.back()), candidates_.back().value);
        valueItem->setData(CandidateIndexRole, row);
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        resultsTable_->setItem(row, ValueColumn, valueItem);
        setCandidateStatus(row, candidateRecords[index].status);
        updateCandidateRowToolTip(row);
    }
    baselineRecord_.reset();
    baselineResult_.reset();
    const auto baselineRunId = sessionMetadata.value(
        QStringLiteral("baselineRunId")).toString();
    if (!baselineRunId.isEmpty()) {
        const auto baseline = std::ranges::find(records, baselineRunId,
            &AnalysisRunRecord::id);
        if (baseline != records.end()) {
            QFile outputFile(QDir(baseline->directory).filePath(
                QStringLiteral("model.out")));
            if (outputFile.open(QIODevice::ReadOnly)) {
                auto result = analysis::NecOutputParser{}.parse(
                    outputFile.readAll().toStdString());
                result.referenceImpedanceOhms = baseline->referenceImpedanceOhms;
                baselineRecord_ = *baseline;
                baselineResult_ = result;
                baselineCompatibility_ = {};
                baselineCompatibility_.status =
                    analysis::OptimizationBaselineStatus::Compatible;
                comparePanel_->setBaseline(result,
                    tr("Optimization Baseline — Run %1").arg(baseline->id));
                comparePanel_->setBaselineStatus(
                    tr("Optimization baseline — Run %1 · saved with this optimization session")
                        .arg(baseline->id), true);
            }
        }
    }
    if (!baselineResult_) {
        comparePanel_->clearBaseline();
        comparePanel_->setBaselineStatus(
            baselineRunId.isEmpty()
                ? tr("Optimization baseline — unavailable · this session did not record one")
                : tr("Optimization baseline — unavailable · saved run %1 could not be loaded")
                    .arg(baselineRunId), false);
    }
    historicalSession_ = true;
    configurationPanel_->hide();
    const auto modelName = session->sourceFile.isEmpty()
        ? tr("Archived model.nec") : QFileInfo(session->sourceFile).fileName();
    historicalBannerTitle_->setText(tr(
        "Historical Optimization Session — %1 — %2 — %3\nArchived and read-only; it cannot be rerun in place.")
        .arg(modelName, session->started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            session->backend.isEmpty() ? tr("Unknown backend") : session->backend));
    historicalBanner_->show();
    runButton_->setText(tr("Historical Session — Read Only"));
    statusLabel_->setText(searchStopReason_.isEmpty()
        ? tr("Historical optimization session · %1").arg(session->status)
        : tr("Historical optimization session · %1 · %2")
            .arg(session->status, searchStopReason_));
    bestLabel_->setText(session->summary.isEmpty() ? tr("No optimization summary is available.") : session->summary);
    activeObjective_ = restoredObjective;
    updateCandidatePlots();
    resultsTable_->horizontalHeaderItem(ScoreColumn)->setText(tr("Objective Score"));
    progress_->setRange(0, static_cast<int>(candidateRecords.size()));
    progress_->setValue(static_cast<int>(candidateRecords.size()));
    progress_->setFormat(tr("Complete — %v candidates"));
    candidateIndex_ = candidates_.size();
    applyCandidateSort();
    if (bestRow_ >= 0) {
        const auto bestTableRow = tableRowForCandidateIndex(bestRow_);
        if (bestTableRow >= 0) resultsTable_->selectRow(bestTableRow);
    }
    updateComparisonCandidate();
    updateWorkload();
    QStringList historicalVariables;
    for (const auto& variable : activeVariables_) historicalVariables.append(variable.name);
    QStringList historicalCriteria;
    const std::array historicalWeights{activeObjective_.swrWeight,
        activeObjective_.resistanceWeight, activeObjective_.reactanceWeight,
        activeObjective_.forwardGainWeight, activeObjective_.frontToBackWeight,
        activeObjective_.frontToRearWeight};
    const std::array historicalNames{tr("SWR"), tr("R"), tr("X"), tr("Gain"),
        tr("F/B"), tr("F/R")};
    for (auto index = std::size_t{}; index < historicalWeights.size(); ++index) {
        if (historicalWeights[index] > 0.0) historicalCriteria.append(historicalNames[index]);
    }
    studySummaryLabel_->setText(tr("Variables: %1 | Frequencies: %2 | Objective: %3")
        .arg(historicalVariables.isEmpty() ? tr("not recorded")
                                           : historicalVariables.join(QStringLiteral(", ")))
        .arg(activeFrequenciesMHz_.size())
        .arg(historicalCriteria.isEmpty() ? tr("not recorded")
                                          : historicalCriteria.join(QStringLiteral(" + "))));
    updateReadiness();
    setUpdatesEnabled(true);
    update();
    return true;
}

void OptimizationWorkspace::leaveHistoricalSession()
{
    if (!historicalSession_) return;
    historicalSession_ = false;
    configurationPanel_->show();
    historicalBanner_->hide();
    if (activeFrequencySelection_) {
        restoreFrequencySelection(*activeFrequencySelection_);
        activeFrequencySelection_.reset();
    }
    updateSearchMethodControls();
    updateReadiness();
}

auto OptimizationWorkspace::ensureCandidateAnalysisLoaded(Candidate& candidate) -> bool
{
    if (candidate.analysisLoaded)
        return !candidate.feedpoints.empty() || !candidate.radiation.empty();
    candidate.analysisLoaded = true;
    QFile outputFile(QDir(candidate.record.directory).filePath(QStringLiteral("model.out")));
    if (!outputFile.open(QIODevice::ReadOnly)) return false;
    auto result = analysis::NecOutputParser{}.parse(outputFile.readAll().toStdString());
    candidate.feedpoints = std::move(result.feedpoints);
    candidate.radiation = std::move(result.radiation);
    return !candidate.feedpoints.empty() || !candidate.radiation.empty();
}

void OptimizationWorkspace::saveSessionCompletionMetadata()
{
    if (!sessionRecord_) return;
    const auto path = QDir(sessionRecord_->directory).filePath(
        QStringLiteral("optimization-session.json"));
    QFile file(path);
    auto metadata = file.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
    file.close();
    metadata.insert(QStringLiteral("evaluations"), static_cast<int>(candidates_.size()));
    metadata.insert(QStringLiteral("refinementRounds"), currentSearchIteration());
    metadata.insert(QStringLiteral("stopReason"), cancelRequested_
        ? QStringLiteral("canceled") : currentStopReasonId());
    metadata.insert(QStringLiteral("stopDescription"), cancelRequested_
        ? tr("canceled by the user") : searchStopReason_);
    writeFile(path, QJsonDocument(metadata).toJson(QJsonDocument::Indented));
}

auto OptimizationWorkspace::writeCandidateMetadata(const Candidate& candidate) -> bool
{
    const QDir directory(candidate.record.directory);
    const auto frequencyMode = selectedFrequencyMode();
    QJsonArray variableValues;
    for (auto index = std::size_t{};
         index < activeVariables_.size() && index < candidate.values.size(); ++index) {
        variableValues.append(QJsonObject{
            {QStringLiteral("name"), activeVariables_[index].name},
            {QStringLiteral("value"), candidate.values[index]},
        });
    }
    const auto metadata = QJsonObject{
        {QStringLiteral("version"), 11},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("value"), candidate.value},
        {QStringLiteral("variables"), variableValues},
        {QStringLiteral("unit"), selectedValueSuffix_.trimmed()},
        {QStringLiteral("searchMethod"), searchMethodId(activeSearchMethod_)},
        {QStringLiteral("refinementRound"), candidate.refinementRound},
        {QStringLiteral("trialRole"), candidate.trialRole},
        {QStringLiteral("populationSize"), differentialEvolutionPopulationControl_->value()},
        {QStringLiteral("maximumGenerations"), differentialEvolutionGenerationControl_->value()},
        {QStringLiteral("mutationFactor"), differentialEvolutionMutationControl_->value()},
        {QStringLiteral("crossoverRate"), differentialEvolutionCrossoverControl_->value()},
        {QStringLiteral("scoreTolerance"), differentialEvolutionScoreToleranceControl_->value()},
        {QStringLiteral("randomSeed"), differentialEvolutionSeedControl_->value()},
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
            : frequencyMode == FrequencyMode::Explicit
                ? QStringLiteral("explicit")
                : frequencyMode == FrequencyMode::Continuous
                    ? QStringLiteral("continuous") : QStringLiteral("model-fr")},
        {QStringLiteral("frequenciesMHz"), frequencyArray(activeFrequenciesMHz_)},
        {QStringLiteral("continuousStartMHz"), continuousStartControl_->value()},
        {QStringLiteral("continuousStopMHz"), continuousStopControl_->value()},
        {QStringLiteral("continuousStepMHz"), continuousStepControl_->value()},
    };
    return writeFile(directory.filePath(QStringLiteral("optimization.json")),
            QJsonDocument(metadata).toJson(QJsonDocument::Indented));
}

}
