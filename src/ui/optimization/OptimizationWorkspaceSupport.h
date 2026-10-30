#pragma once

// Helpers shared by the OptimizationWorkspace source files; not used elsewhere.

#include "analysis/OptimizationObjective.h"
#include "analysis/OptimizationPreset.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTableWidget>

#include <optional>
#include <span>
#include <utility>

class QComboBox;

namespace necwb::ui::detail {

enum ResultColumn {
    ValueColumn,
    ScoreColumn,
    SwrColumn,
    FrequencyColumn,
    ResistanceColumn,
    ReactanceColumn,
    GainColumn,
    FrontToBackColumn,
    FrontToRearColumn,
    StatusColumn,
    RunColumn,
    ResultColumnCount
};

constexpr auto CandidateIndexRole = Qt::UserRole + 1;

class SortableResultItem final : public QTableWidgetItem {
public:
    explicit SortableResultItem(QString text, std::optional<double> sortValue = std::nullopt)
        : QTableWidgetItem(std::move(text)), sortValue_(sortValue)
    {
    }

    auto operator<(const QTableWidgetItem& other) const -> bool override
    {
        const auto* sortable = dynamic_cast<const SortableResultItem*>(&other);
        if (sortable == nullptr) return QTableWidgetItem::operator<(other);
        if (sortValue_ && sortable->sortValue_) return *sortValue_ < *sortable->sortValue_;
        if (sortValue_.has_value() != sortable->sortValue_.has_value()) {
            const auto order = tableWidget() == nullptr
                ? Qt::AscendingOrder
                : tableWidget()->horizontalHeader()->sortIndicatorOrder();
            return order == Qt::AscendingOrder ? sortValue_.has_value()
                                               : !sortValue_.has_value();
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    std::optional<double> sortValue_;
};

auto numericItem(double value) -> QTableWidgetItem*;

auto unavailableResultItem() -> QTableWidgetItem*;

void addOptionalNumber(QJsonObject& object, const QString& key,
    const std::optional<double>& value);

auto metricSummaryJson(const analysis::OptimizationMetricSummary& summary) -> QJsonObject;

auto objectiveResultJson(const analysis::OptimizationObjectiveResult& result) -> QJsonObject;

void writeObjectiveResultCache(const QString& directory,
    const std::optional<analysis::OptimizationObjectiveResult>& evaluation,
    const std::optional<analysis::OptimizationObjectiveResult>& availableMeasurements);

auto objectiveId(analysis::OptimizationObjectiveKind kind) -> QString;

auto selectedGoal(const QComboBox* control) -> analysis::OptimizationGoal;

void configureAggregationControl(QComboBox* control, analysis::OptimizationGoal goal,
    std::optional<analysis::OptimizationAggregation> aggregation = std::nullopt);

auto optimizationPresetName(analysis::OptimizationPresetId id) -> QString;

auto frequencyArray(std::span<const double> frequenciesMHz) -> QJsonArray;

}
