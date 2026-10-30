#include "ui/optimization/OptimizationWorkspaceSupport.h"

#include "ui/DisplayFormat.h"
#include "ui/FileIo.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>

#include <algorithm>
#include <span>

namespace necwb::ui::detail {

auto numericItem(double value) -> QTableWidgetItem*
{
    auto* item = new SortableResultItem(formatDecimal(value), value);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

auto unavailableResultItem() -> QTableWidgetItem*
{
    return new SortableResultItem(QStringLiteral("—"));
}

void addOptionalNumber(QJsonObject& object, const QString& key,
    const std::optional<double>& value)
{
    if (value) object.insert(key, *value);
}

auto metricSummaryJson(const analysis::OptimizationMetricSummary& summary) -> QJsonObject
{
    return {{QStringLiteral("minimum"), summary.minimum},
        {QStringLiteral("average"), summary.average},
        {QStringLiteral("maximum"), summary.maximum},
        {QStringLiteral("minimumFrequencyMHz"), summary.minimumFrequencyMHz},
        {QStringLiteral("maximumFrequencyMHz"), summary.maximumFrequencyMHz}};
}

auto objectiveResultJson(const analysis::OptimizationObjectiveResult& result) -> QJsonObject
{
    QJsonObject object{{QStringLiteral("score"), result.score},
        {QStringLiteral("evaluationFrequencyMHz"), result.evaluationFrequencyMHz},
        {QStringLiteral("evaluatedFrequencyCount"),
            static_cast<qint64>(result.evaluatedFrequencyCount)},
        {QStringLiteral("swr"), result.swr},
        {QStringLiteral("swrComponent"), result.swrComponent},
        {QStringLiteral("resistanceComponent"), result.resistanceComponent},
        {QStringLiteral("reactanceComponent"), result.reactanceComponent},
        {QStringLiteral("forwardGainComponent"), result.forwardGainComponent},
        {QStringLiteral("frontToBackComponent"), result.frontToBackComponent},
        {QStringLiteral("frontToRearComponent"), result.frontToRearComponent}};
    addOptionalNumber(object, QStringLiteral("forwardGainDb"), result.forwardGainDb);
    addOptionalNumber(object, QStringLiteral("forwardGainFrequencyMHz"),
        result.forwardGainFrequencyMHz);
    addOptionalNumber(object, QStringLiteral("forwardThetaDegrees"),
        result.forwardThetaDegrees);
    addOptionalNumber(object, QStringLiteral("forwardPhiDegrees"), result.forwardPhiDegrees);
    addOptionalNumber(object, QStringLiteral("backGainDb"), result.backGainDb);
    addOptionalNumber(object, QStringLiteral("frontToBackDb"), result.frontToBackDb);
    addOptionalNumber(object, QStringLiteral("frontToBackFrequencyMHz"),
        result.frontToBackFrequencyMHz);
    addOptionalNumber(object, QStringLiteral("rearGainDb"), result.rearGainDb);
    addOptionalNumber(object, QStringLiteral("frontToRearDb"), result.frontToRearDb);
    addOptionalNumber(object, QStringLiteral("frontToRearFrequencyMHz"),
        result.frontToRearFrequencyMHz);
    if (result.swrSummary)
        object.insert(QStringLiteral("swrSummary"), metricSummaryJson(*result.swrSummary));
    if (result.forwardGainSummary)
        object.insert(QStringLiteral("forwardGainSummary"),
            metricSummaryJson(*result.forwardGainSummary));
    if (result.frontToBackSummary)
        object.insert(QStringLiteral("frontToBackSummary"),
            metricSummaryJson(*result.frontToBackSummary));
    if (result.frontToRearSummary)
        object.insert(QStringLiteral("frontToRearSummary"),
            metricSummaryJson(*result.frontToRearSummary));
    if (result.feedpoint) {
        object.insert(QStringLiteral("feedpoint"), QJsonObject{
            {QStringLiteral("frequencyMHz"), result.feedpoint->frequencyMHz},
            {QStringLiteral("resistanceOhms"), result.feedpoint->impedance.real()},
            {QStringLiteral("reactanceOhms"), result.feedpoint->impedance.imag()}});
    }
    QJsonArray metrics;
    for (const auto& value : result.frequencyMetrics) {
        QJsonObject metric{{QStringLiteral("frequencyMHz"), value.frequencyMHz}};
        addOptionalNumber(metric, QStringLiteral("swr"), value.swr);
        addOptionalNumber(metric, QStringLiteral("resistanceOhms"), value.resistanceOhms);
        addOptionalNumber(metric, QStringLiteral("reactanceOhms"), value.reactanceOhms);
        addOptionalNumber(metric, QStringLiteral("forwardGainDb"), value.forwardGainDb);
        addOptionalNumber(metric, QStringLiteral("frontToBackDb"), value.frontToBackDb);
        addOptionalNumber(metric, QStringLiteral("frontToRearDb"), value.frontToRearDb);
        addOptionalNumber(metric, QStringLiteral("forwardThetaDegrees"),
            value.forwardThetaDegrees);
        addOptionalNumber(metric, QStringLiteral("forwardPhiDegrees"),
            value.forwardPhiDegrees);
        metrics.append(metric);
    }
    object.insert(QStringLiteral("frequencyMetrics"), metrics);
    return object;
}

void writeObjectiveResultCache(const QString& directory,
    const std::optional<analysis::OptimizationObjectiveResult>& evaluation,
    const std::optional<analysis::OptimizationObjectiveResult>& availableMeasurements)
{
    const auto path = QDir(directory).filePath(QStringLiteral("optimization.json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    auto metadata = QJsonDocument::fromJson(file.readAll()).object();
    if (evaluation)
        metadata.insert(QStringLiteral("objectiveEvaluation"),
            objectiveResultJson(*evaluation));
    if (availableMeasurements)
        metadata.insert(QStringLiteral("availableMeasurements"),
            objectiveResultJson(*availableMeasurements));
    metadata.insert(QStringLiteral("resultCacheVersion"), 1);
    writeFile(path, QJsonDocument(metadata).toJson(QJsonDocument::Indented));
}

auto objectiveId(analysis::OptimizationObjectiveKind kind) -> QString
{
    switch (kind) {
    case analysis::OptimizationObjectiveKind::PerCriterion:
        return QStringLiteral("per-criterion");
    case analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies:
        return QStringLiteral("worst-point-across-frequencies");
    case analysis::OptimizationObjectiveKind::AverageAcrossFrequencies:
        return QStringLiteral("average-across-frequencies");
    case analysis::OptimizationObjectiveKind::SwrAtFrequency:
        return QStringLiteral("minimize-swr-at-frequency");
    }
    return QStringLiteral("worst-point-across-frequencies");
}

auto selectedGoal(const QComboBox* control) -> analysis::OptimizationGoal
{
    switch (control->currentIndex()) {
    case 0: return analysis::OptimizationGoal::Minimize;
    case 1: return analysis::OptimizationGoal::Maximize;
    case 2: return analysis::OptimizationGoal::Target;
    default: return analysis::OptimizationGoal::GoodEnough;
    }
}

void configureAggregationControl(QComboBox* control, analysis::OptimizationGoal goal,
    std::optional<analysis::OptimizationAggregation> aggregation)
{
    auto semanticIndex = std::max(0, control->currentIndex());
    if (aggregation) {
        if (*aggregation == analysis::OptimizationAggregation::Average) {
            semanticIndex = 1;
        } else {
            const auto worstAggregation = goal == analysis::OptimizationGoal::Maximize
                ? analysis::OptimizationAggregation::Minimum
                : analysis::OptimizationAggregation::Maximum;
            semanticIndex = *aggregation == worstAggregation ? 0 : 2;
        }
    }

    const auto worstAggregation = goal == analysis::OptimizationGoal::Maximize
        ? analysis::OptimizationAggregation::Minimum
        : analysis::OptimizationAggregation::Maximum;
    const auto bestAggregation = worstAggregation == analysis::OptimizationAggregation::Minimum
        ? analysis::OptimizationAggregation::Maximum
        : analysis::OptimizationAggregation::Minimum;
    const auto target = goal == analysis::OptimizationGoal::Target;
    const auto threshold = goal == analysis::OptimizationGoal::GoodEnough;
    const QSignalBlocker blocker(control);
    control->clear();
    control->addItem(target ? QObject::tr("Worst Error")
            : threshold ? QObject::tr("Worst Violation") : QObject::tr("Worst Point"),
        static_cast<int>(worstAggregation));
    control->addItem(target ? QObject::tr("Average Error")
            : threshold ? QObject::tr("Average Violation") : QObject::tr("Average"),
        static_cast<int>(analysis::OptimizationAggregation::Average));
    control->addItem(target ? QObject::tr("Best Error")
            : threshold ? QObject::tr("Best Violation") : QObject::tr("Best Point"),
        static_cast<int>(bestAggregation));
    QString worstToolTip;
    QString averageToolTip;
    QString bestToolTip;
    switch (goal) {
    case analysis::OptimizationGoal::Minimize:
        worstToolTip = QObject::tr(
            "Scores the highest measured value across the selected frequencies.");
        averageToolTip = QObject::tr(
            "Scores the arithmetic average measured value across the selected frequencies.");
        bestToolTip = QObject::tr(
            "Scores the lowest measured value across the selected frequencies.");
        break;
    case analysis::OptimizationGoal::Maximize:
        worstToolTip = QObject::tr(
            "Scores the lowest measured value across the selected frequencies.");
        averageToolTip = QObject::tr(
            "Scores the arithmetic average measured value across the selected frequencies.");
        bestToolTip = QObject::tr(
            "Scores the highest measured value across the selected frequencies.");
        break;
    case analysis::OptimizationGoal::Target:
        worstToolTip = QObject::tr(
            "Scores the largest absolute error from Value across the selected frequencies.");
        averageToolTip = QObject::tr(
            "Scores the average absolute error from Value across the selected frequencies.");
        bestToolTip = QObject::tr(
            "Scores the smallest absolute error from Value across the selected frequencies.");
        break;
    case analysis::OptimizationGoal::GoodEnough:
        worstToolTip = QObject::tr(
            "Scores the largest threshold violation across the selected frequencies.");
        averageToolTip = QObject::tr(
            "Scores the average threshold violation across the selected frequencies; compliant points contribute zero.");
        bestToolTip = QObject::tr(
            "Scores the smallest threshold violation across the selected frequencies.");
        break;
    }
    control->setItemData(0, worstToolTip, Qt::ToolTipRole);
    control->setItemData(1, averageToolTip, Qt::ToolTipRole);
    control->setItemData(2, bestToolTip, Qt::ToolTipRole);
    control->setCurrentIndex(std::clamp(semanticIndex, 0, 2));
    control->setToolTip(control->currentData(Qt::ToolTipRole).toString());
}

auto optimizationPresetName(analysis::OptimizationPresetId id) -> QString
{
    switch (id) {
    case analysis::OptimizationPresetId::MinimizeSwr:
        return QObject::tr("Minimize Worst-Case SWR");
    case analysis::OptimizationPresetId::SwrLimit: return QObject::tr("Keep SWR Below Limit");
    case analysis::OptimizationPresetId::FeedpointMatch: return QObject::tr("Match Feedpoint Impedance");
    case analysis::OptimizationPresetId::PeakGain: return QObject::tr("Maximize Peak Gain in Cut");
    case analysis::OptimizationPresetId::FrontToBack: return QObject::tr("Maximize Front-to-Back");
    case analysis::OptimizationPresetId::FrontToRear: return QObject::tr("Maximize Front-to-Rear");
    case analysis::OptimizationPresetId::SwrAndGain: return QObject::tr("Balance SWR and Gain");
    case analysis::OptimizationPresetId::Custom: return QObject::tr("Custom");
    }
    return {};
}

auto frequencyArray(std::span<const double> frequenciesMHz) -> QJsonArray
{
    QJsonArray result;
    for (const auto frequencyMHz : frequenciesMHz) result.append(frequencyMHz);
    return result;
}

}
