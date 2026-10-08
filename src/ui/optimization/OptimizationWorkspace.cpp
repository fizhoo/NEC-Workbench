#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/DisplayFormat.h"
#include "ui/FileIo.h"
#include "ui/analysis/SweepPlotsView.h"

#include "analysis/AnalysisResult.h"
#include "analysis/NecOutputParser.h"
#include "analysis/OptimizationObjective.h"
#include "analysis/SolverCommand.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSplitter>
#include <QSpinBox>
#include <QScrollBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>

namespace necwb::ui {
namespace {

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

class FocusWheelSpinBox final : public QSpinBox {
public:
    using QSpinBox::QSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (!hasFocus()) {
            event->ignore();
            return;
        }
        QSpinBox::wheelEvent(event);
    }
};

class FocusWheelDoubleSpinBox final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (!hasFocus()) {
            event->ignore();
            return;
        }
        QDoubleSpinBox::wheelEvent(event);
    }
};

auto numericItem(double value) -> QTableWidgetItem*
{
    auto* item = new QTableWidgetItem(formatDecimal(value));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

auto optionalNumericItem(const std::optional<double>& value) -> QTableWidgetItem*
{
    if (value) return numericItem(*value);
    auto* item = new QTableWidgetItem(QString::fromUtf8("—"));
    item->setTextAlignment(Qt::AlignCenter);
    return item;
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

auto selectedGoal(const QComboBox* control) -> analysis::OptimizationGoal
{
    switch (control->currentIndex()) {
    case 0: return analysis::OptimizationGoal::Minimize;
    case 1: return analysis::OptimizationGoal::Maximize;
    case 2: return analysis::OptimizationGoal::Target;
    default: return analysis::OptimizationGoal::GoodEnough;
    }
}

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

void configureAggregationControl(QComboBox* control, analysis::OptimizationGoal goal,
    std::optional<analysis::OptimizationAggregation> aggregation = std::nullopt)
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

auto frequencyAt(const model::FrequencyDefinition& frequency, int index) -> double
{
    return frequency.steppingMode == 1
        ? frequency.startMHz * std::pow(frequency.step, index)
        : frequency.startMHz + index * frequency.step;
}

auto frequencyArray(std::span<const double> frequenciesMHz) -> QJsonArray
{
    QJsonArray result;
    for (const auto frequencyMHz : frequenciesMHz) result.append(frequencyMHz);
    return result;
}

}

OptimizationWorkspace::OptimizationWorkspace(QWidget* parent)
    : QWidget(parent), bestScore_(std::numeric_limits<double>::infinity())
{
    evaluator_ = new CandidateEvaluator(this);
    auto* layout = new QVBoxLayout(this);
    historicalBanner_ = new QFrame(this);
    historicalBanner_->setObjectName(QStringLiteral("optimizationHistoricalBanner"));
    static_cast<QFrame*>(historicalBanner_)->setFrameShape(QFrame::StyledPanel);
    auto* historicalLayout = new QHBoxLayout(historicalBanner_);
    historicalBannerTitle_ = new QLabel(historicalBanner_);
    historicalBannerTitle_->setWordWrap(true);
    auto historicalFont = historicalBannerTitle_->font();
    historicalFont.setBold(true);
    historicalBannerTitle_->setFont(historicalFont);
    returnToCurrentWorkButton_ = new QPushButton(tr("Return to Current Work"), historicalBanner_);
    returnToCurrentWorkButton_->setObjectName(
        QStringLiteral("optimizationReturnToCurrentWorkButton"));
    historicalLayout->addWidget(historicalBannerTitle_, 1);
    historicalLayout->addWidget(returnToCurrentWorkButton_);
    historicalBanner_->hide();
    variablesTable_ = new QTableWidget(this);
    variablesTable_->setObjectName(QStringLiteral("optimizationVariablesTable"));
    variablesTable_->setColumnCount(6);
    variablesTable_->setHorizontalHeaderLabels(
        {tr("Use"), tr("Symbol"), tr("Value"), tr("Minimum"), tr("Maximum"), tr("Tolerance")});
    variablesTable_->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    variablesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    variablesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    variablesTable_->verticalHeader()->hide();
    variablesTable_->setTextElideMode(Qt::ElideRight);
    variablesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    variablesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (auto column = 2; column < 6; ++column)
        variablesTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
    variablesTable_->setToolTip(tr(
        "Adaptive Optimize and Nelder–Mead change every checked parameter. "
        "Double-click Minimum, Maximum, or Tolerance to edit it."));

    variableControl_ = new QComboBox(this);
    variableControl_->setObjectName(QStringLiteral("optimizationVariableControl"));
    objectiveControl_ = new QComboBox(this);
    objectiveControl_->setObjectName(QStringLiteral("optimizationObjectiveControl"));
    objectiveControl_->addItem(tr("Per-Criterion"),
        static_cast<int>(analysis::OptimizationObjectiveKind::PerCriterion));
    targetFrequencyControl_ = new FocusWheelDoubleSpinBox(this);
    targetFrequencyControl_->setObjectName(QStringLiteral("optimizationTargetFrequency"));
    targetFrequencyControl_->setRange(0.000001, 1.0e9);
    targetFrequencyControl_->setDecimals(DisplayDecimalPlaces);
    targetFrequencyControl_->setValue(14.175);
    targetFrequencyControl_->setSuffix(tr(" MHz"));
    frequencyModeControl_ = new QComboBox(this);
    frequencyModeControl_->setObjectName(QStringLiteral("optimizationFrequencyMode"));
    frequencyModeControl_->addItem(tr("Use Model FR Sweep"),
        static_cast<int>(FrequencyMode::ModelSweep));
    frequencyModeControl_->addItem(tr("Use Selected Frequencies"),
        static_cast<int>(FrequencyMode::Explicit));
    frequencyModeControl_->addItem(tr("Custom Continuous Sweep"),
        static_cast<int>(FrequencyMode::Continuous));
    minimumControl_ = new FocusWheelDoubleSpinBox(this);
    maximumControl_ = new FocusWheelDoubleSpinBox(this);
    minimumControl_->setObjectName(QStringLiteral("optimizationMinimum"));
    maximumControl_->setObjectName(QStringLiteral("optimizationMaximum"));
    pointsControl_ = new FocusWheelSpinBox(this);
    referenceImpedanceControl_ = new FocusWheelDoubleSpinBox(this);
    referenceImpedanceControl_->setObjectName(
        QStringLiteral("optimizationReferenceImpedance"));
    for (auto* control : {minimumControl_, maximumControl_}) {
        control->setRange(-1.0e12, 1.0e12);
        control->setDecimals(DisplayDecimalPlaces);
    }
    pointsControl_->setRange(2, 101);
    pointsControl_->setObjectName(QStringLiteral("optimizationCandidateCount"));
    pointsControl_->setValue(7);
    pointsControl_->setToolTip(tr(
        "Number of evenly spaced parameter values tested from Minimum through Maximum, "
        "including both endpoints. Each candidate value is evaluated at every selected frequency."));
    referenceImpedanceControl_->setRange(1.0, 10000.0);
    referenceImpedanceControl_->setDecimals(DisplayDecimalPlaces);
    referenceImpedanceControl_->setValue(50.0);
    referenceImpedanceControl_->setSuffix(QStringLiteral(" Ω"));

    objectiveCriteriaTable_ = new QTableWidget(6, 5, this);
    objectiveCriteriaTable_->setObjectName(QStringLiteral("optimizationObjectiveCriteria"));
    objectiveCriteriaTable_->setHorizontalHeaderLabels(
        {tr("Criterion"), tr("Weight"), tr("Goal"), tr("Value"), tr("Band Evaluation")});
    objectiveCriteriaTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objectiveCriteriaTable_->setSelectionMode(QAbstractItemView::NoSelection);
    objectiveCriteriaTable_->verticalHeader()->hide();
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    const auto criterionItem = [this](int row, const QString& text, const QString& toolTip) {
        auto* item = new QTableWidgetItem(text);
        item->setFlags(Qt::ItemIsEnabled);
        item->setToolTip(toolTip);
        objectiveCriteriaTable_->setItem(row, 0, item);
    };
    criterionItem(0, tr("SWR"), tr("Standing-wave ratio at each calculated frequency."));
    criterionItem(1, tr("Resistance (Ω)"), tr("Feedpoint resistance."));
    criterionItem(2, tr("Reactance (Ω)"), tr("Feedpoint reactance."));
    criterionItem(3, tr("Forward Gain (dBi)"), tr("Gain in the configured physical direction."));
    criterionItem(4, tr("F/B (dB)"),
        tr("Forward gain minus gain exactly 180 degrees opposite."));
    criterionItem(5, tr("F/R (dB)"),
        tr("Forward gain minus the strongest response in the rear azimuth half."));
    const auto weightControl = [this](const QString& name, double value) {
        auto* control = new FocusWheelDoubleSpinBox(objectiveCriteriaTable_);
        control->setObjectName(name);
        control->setRange(0.0, 100.0);
        control->setDecimals(DisplayDecimalPlaces);
        control->setValue(value);
        control->setToolTip(tr("Relative importance; zero disables this criterion."));
        return control;
    };
    swrWeightControl_ = weightControl(QStringLiteral("optimizationSwrWeight"), 1.0);
    resistanceWeightControl_ = weightControl(
        QStringLiteral("optimizationResistanceWeight"), 0.0);
    reactanceWeightControl_ = weightControl(
        QStringLiteral("optimizationReactanceWeight"), 0.0);
    forwardGainWeightControl_ = weightControl(
        QStringLiteral("optimizationForwardGainWeight"), 0.0);
    frontToBackWeightControl_ = weightControl(
        QStringLiteral("optimizationFrontToBackWeight"), 0.0);
    frontToRearWeightControl_ = weightControl(
        QStringLiteral("optimizationFrontToRearWeight"), 0.0);
    const std::array weights{swrWeightControl_, resistanceWeightControl_, reactanceWeightControl_,
        forwardGainWeightControl_, frontToBackWeightControl_, frontToRearWeightControl_};
    const std::array defaultGoals{analysis::OptimizationGoal::Minimize,
        analysis::OptimizationGoal::Target, analysis::OptimizationGoal::Target,
        analysis::OptimizationGoal::Maximize, analysis::OptimizationGoal::Maximize,
        analysis::OptimizationGoal::Maximize};
    const std::array defaultValues{2.0, 50.0, 0.0, 0.0, 20.0, 15.0};
    const std::array defaultAggregations{analysis::OptimizationAggregation::Maximum,
        analysis::OptimizationAggregation::Maximum, analysis::OptimizationAggregation::Maximum,
        analysis::OptimizationAggregation::Minimum, analysis::OptimizationAggregation::Minimum,
        analysis::OptimizationAggregation::Minimum};
    for (auto row = 0; row < 6; ++row) {
        auto* goal = new QComboBox(objectiveCriteriaTable_);
        goal->addItem(tr("Minimize"), 0);
        goal->addItem(tr("Maximize"), 1);
        goal->addItem(tr("Target"), 2);
        goal->addItem(tr("Good Enough ≤"), 3);
        goal->addItem(tr("Good Enough ≥"), 4);
        goal->setItemData(0, tr("Lower measured values improve the objective score."),
            Qt::ToolTipRole);
        goal->setItemData(1, tr("Higher measured values improve the objective score."),
            Qt::ToolTipRole);
        goal->setItemData(2, tr("Minimizes absolute error from Value."),
            Qt::ToolTipRole);
        goal->setItemData(3, tr("Only the amount above Value is penalized."),
            Qt::ToolTipRole);
        goal->setItemData(4, tr("Only the amount below Value is penalized."),
            Qt::ToolTipRole);
        goal->setCurrentIndex(static_cast<int>(defaultGoals[row]));
        goal->setToolTip(goal->currentData(Qt::ToolTipRole).toString());
        goal->setObjectName(QStringLiteral("optimizationGoal%1").arg(row));
        objectiveGoalControls_[row] = goal;

        auto* value = new FocusWheelDoubleSpinBox(objectiveCriteriaTable_);
        value->setRange(-1.0e9, 1.0e9);
        value->setDecimals(DisplayDecimalPlaces);
        value->setValue(defaultValues[row]);
        value->setObjectName(QStringLiteral("optimizationObjectiveValue%1").arg(row));
        if (row == 1 || row == 2) value->setSuffix(QStringLiteral(" Ω"));
        else if (row == 3) value->setSuffix(tr(" dBi"));
        else if (row >= 4) value->setSuffix(tr(" dB"));
        objectiveValueControls_[row] = value;

        auto* aggregation = new QComboBox(objectiveCriteriaTable_);
        aggregation->setObjectName(QStringLiteral("optimizationAggregation%1").arg(row));
        configureAggregationControl(aggregation, defaultGoals[row], defaultAggregations[row]);
        objectiveAggregationControls_[row] = aggregation;

        objectiveCriteriaTable_->setCellWidget(row, 1, weights[row]);
        objectiveCriteriaTable_->setCellWidget(row, 2, goal);
        objectiveCriteriaTable_->setCellWidget(row, 3, value);
        objectiveCriteriaTable_->setCellWidget(row, 4, aggregation);
    }
    resistanceTargetControl_ = objectiveValueControls_[1];
    reactanceTargetControl_ = objectiveValueControls_[2];
    resistanceTargetControl_->setObjectName(QStringLiteral("optimizationResistanceTarget"));
    reactanceTargetControl_->setObjectName(QStringLiteral("optimizationReactanceTarget"));

    forwardThetaControl_ = new FocusWheelDoubleSpinBox(this);
    forwardThetaControl_->setObjectName(QStringLiteral("optimizationForwardTheta"));
    forwardThetaControl_->setRange(-360.0, 360.0);
    forwardThetaControl_->setDecimals(DisplayDecimalPlaces);
    forwardThetaControl_->setValue(90.0);
    forwardThetaControl_->setSuffix(QStringLiteral("°"));
    forwardPhiControl_ = new FocusWheelDoubleSpinBox(this);
    forwardPhiControl_->setObjectName(QStringLiteral("optimizationForwardPhi"));
    forwardPhiControl_->setRange(-360.0, 720.0);
    forwardPhiControl_->setDecimals(DisplayDecimalPlaces);
    forwardPhiControl_->setSuffix(QStringLiteral("°"));
    radiationComponentControl_ = new QComboBox(this);
    radiationComponentControl_->setObjectName(QStringLiteral("optimizationRadiationComponent"));
    radiationComponentControl_->addItem(tr("Total"), static_cast<int>(analysis::RadiationComponent::Total));
    radiationComponentControl_->addItem(tr("Vertical"), static_cast<int>(analysis::RadiationComponent::Vertical));
    radiationComponentControl_->addItem(tr("Horizontal"), static_cast<int>(analysis::RadiationComponent::Horizontal));
    radiationComponentControl_->addItem(tr("RHCP"), static_cast<int>(analysis::RadiationComponent::RightHandCircular));
    radiationComponentControl_->addItem(tr("LHCP"), static_cast<int>(analysis::RadiationComponent::LeftHandCircular));
    objectiveCriteriaTable_->resizeRowsToContents();
    auto criteriaHeight = objectiveCriteriaTable_->horizontalHeader()->sizeHint().height()
        + 2 * objectiveCriteriaTable_->frameWidth();
    for (auto row = 0; row < objectiveCriteriaTable_->rowCount(); ++row)
        criteriaHeight += objectiveCriteriaTable_->rowHeight(row);
    objectiveCriteriaTable_->setFixedHeight(criteriaHeight);
    searchMethodTabs_ = new QTabBar(this);
    searchMethodTabs_->setObjectName(QStringLiteral("optimizationSearchMethodTabs"));
    searchMethodTabs_->setExpanding(true);
    searchMethodTabs_->addTab(tr("Parameter Sweep"));
    searchMethodTabs_->addTab(tr("Adaptive Optimize"));
    searchMethodTabs_->addTab(tr("Nelder-Mead"));
    searchMethodTabs_->addTab(tr("Differential Evolution"));
    searchMethodTabs_->setTabToolTip(0, tr(
        "Exhaustively test evenly spaced values for one parameter."));
    searchMethodTabs_->setTabToolTip(1, tr(
        "Refine one or more parameters with transparent coordinate trials around the current best."));
    searchMethodTabs_->setTabToolTip(2, tr(
        "Optimize one or more continuous parameters with a bounded derivative-free simplex search."));
    searchMethodTabs_->setTabToolTip(3, tr(
        "Explore one or more continuous parameters with a seeded population-based global search."));

    adaptiveMaximumEvaluationsControl_ = new FocusWheelSpinBox(this);
    adaptiveMaximumEvaluationsControl_->setObjectName(
        QStringLiteral("optimizationAdaptiveMaximumEvaluations"));
    adaptiveMaximumEvaluationsControl_->setRange(5, 101);
    adaptiveMaximumEvaluationsControl_->setValue(21);
    adaptiveMaximumEvaluationsControl_->setToolTip(tr(
        "Maximum solver candidates. Adaptive starts near the center and boundaries; "
        "Nelder–Mead starts with one simplex vertex per selected parameter plus one."));
    adaptiveParameterToleranceControl_ = new FocusWheelDoubleSpinBox(this);
    adaptiveParameterToleranceControl_->setObjectName(
        QStringLiteral("optimizationAdaptiveParameterTolerance"));
    adaptiveParameterToleranceControl_->setRange(0.001, 1.0e12);
    adaptiveParameterToleranceControl_->setDecimals(DisplayDecimalPlaces);
    adaptiveParameterToleranceControl_->setValue(0.010);
    adaptiveParameterToleranceControl_->setToolTip(tr(
        "Stop when no new candidate can be placed this far from the current best value."));
    adaptiveScoreToleranceControl_ = new FocusWheelDoubleSpinBox(this);
    adaptiveScoreToleranceControl_->setObjectName(
        QStringLiteral("optimizationAdaptiveScoreTolerance"));
    adaptiveScoreToleranceControl_->setRange(0.0, 1.0e9);
    adaptiveScoreToleranceControl_->setDecimals(DisplayDecimalPlaces);
    adaptiveScoreToleranceControl_->setValue(0.001);
    adaptiveScoreToleranceControl_->setToolTip(tr(
        "Minimum required improvement in the best objective score between completed refinement rounds. "
        "A round normally tests both a left and right midpoint; the search stops after two consecutive "
        "rounds improve the best score by no more than this amount."));
    differentialEvolutionPopulationControl_ = new FocusWheelSpinBox(this);
    differentialEvolutionPopulationControl_->setObjectName(
        QStringLiteral("optimizationDifferentialEvolutionPopulation"));
    differentialEvolutionPopulationControl_->setRange(4, 50);
    differentialEvolutionPopulationControl_->setValue(12);
    differentialEvolutionPopulationControl_->setToolTip(tr(
        "Number of candidate vectors retained in each generation. At least four are required."));
    differentialEvolutionGenerationControl_ = new FocusWheelSpinBox(this);
    differentialEvolutionGenerationControl_->setObjectName(
        QStringLiteral("optimizationDifferentialEvolutionGenerations"));
    differentialEvolutionGenerationControl_->setRange(1, 200);
    differentialEvolutionGenerationControl_->setValue(20);
    differentialEvolutionGenerationControl_->setToolTip(tr(
        "Maximum evolved generations after the initial population is evaluated."));
    differentialEvolutionMutationControl_ = new FocusWheelDoubleSpinBox(this);
    differentialEvolutionMutationControl_->setObjectName(
        QStringLiteral("optimizationDifferentialEvolutionMutation"));
    differentialEvolutionMutationControl_->setRange(0.1, 2.0);
    differentialEvolutionMutationControl_->setSingleStep(0.1);
    differentialEvolutionMutationControl_->setDecimals(DisplayDecimalPlaces);
    differentialEvolutionMutationControl_->setValue(0.8);
    differentialEvolutionMutationControl_->setToolTip(tr(
        "Differential mutation factor F. Larger values explore more aggressively."));
    differentialEvolutionCrossoverControl_ = new FocusWheelDoubleSpinBox(this);
    differentialEvolutionCrossoverControl_->setObjectName(
        QStringLiteral("optimizationDifferentialEvolutionCrossover"));
    differentialEvolutionCrossoverControl_->setRange(0.0, 1.0);
    differentialEvolutionCrossoverControl_->setSingleStep(0.05);
    differentialEvolutionCrossoverControl_->setDecimals(DisplayDecimalPlaces);
    differentialEvolutionCrossoverControl_->setValue(0.9);
    differentialEvolutionCrossoverControl_->setToolTip(tr(
        "Binomial crossover probability CR applied independently to each parameter."));
    differentialEvolutionScoreToleranceControl_ = new FocusWheelDoubleSpinBox(this);
    differentialEvolutionScoreToleranceControl_->setObjectName(
        QStringLiteral("optimizationDifferentialEvolutionScoreTolerance"));
    differentialEvolutionScoreToleranceControl_->setRange(0.0, 1.0e9);
    differentialEvolutionScoreToleranceControl_->setDecimals(DisplayDecimalPlaces);
    differentialEvolutionScoreToleranceControl_->setValue(0.001);
    differentialEvolutionScoreToleranceControl_->setToolTip(tr(
        "Stop after three generations whose best-score improvement is no greater than this value."));
    differentialEvolutionSeedControl_ = new FocusWheelSpinBox(this);
    differentialEvolutionSeedControl_->setObjectName(
        QStringLiteral("optimizationDifferentialEvolutionSeed"));
    differentialEvolutionSeedControl_->setRange(0, std::numeric_limits<int>::max());
    differentialEvolutionSeedControl_->setValue(5489);
    differentialEvolutionSeedControl_->setToolTip(tr(
        "Random seed. Reusing the same seed and settings reproduces the same candidate proposals."));
    explicitFrequencyPanel_ = new QWidget(this);
    explicitFrequencyPanel_->setObjectName(QStringLiteral("optimizationExplicitFrequencyPanel"));
    auto* frequencyLayout = new QVBoxLayout(explicitFrequencyPanel_);
    frequencyLayout->setContentsMargins(0, 0, 0, 0);
    frequencyTable_ = new QListWidget(explicitFrequencyPanel_);
    frequencyTable_->setObjectName(QStringLiteral("optimizationFrequencyTable"));
    frequencyTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    frequencyTable_->setViewMode(QListView::IconMode);
    frequencyTable_->setFlow(QListView::LeftToRight);
    frequencyTable_->setWrapping(true);
    frequencyTable_->setResizeMode(QListView::Adjust);
    frequencyTable_->setMovement(QListView::Static);
    frequencyTable_->setUniformItemSizes(true);
    frequencyTable_->setGridSize(QSize(92, 28));
    frequencyTable_->setSpacing(2);
    frequencyTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    frequencyTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    frequencyEntryControl_ = new FocusWheelDoubleSpinBox(explicitFrequencyPanel_);
    frequencyEntryControl_->setObjectName(QStringLiteral("optimizationFrequencyEntry"));
    frequencyEntryControl_->setRange(0.000001, 1.0e9);
    frequencyEntryControl_->setDecimals(DisplayDecimalPlaces);
    frequencyEntryControl_->setValue(14.175);
    frequencyEntryControl_->setSuffix(tr(" MHz"));
    addFrequencyButton_ = new QPushButton(tr("Add Frequency"), explicitFrequencyPanel_);
    addFrequencyButton_->setObjectName(QStringLiteral("optimizationAddFrequency"));
    removeFrequencyButton_ = new QPushButton(tr("Remove"), explicitFrequencyPanel_);
    removeFrequencyButton_->setObjectName(QStringLiteral("optimizationRemoveFrequency"));
    pasteFrequencyButton_ = new QPushButton(tr("Paste List…"), explicitFrequencyPanel_);
    pasteFrequencyButton_->setObjectName(QStringLiteral("optimizationPasteFrequencies"));
    clearFrequencyButton_ = new QPushButton(tr("Clear All"), explicitFrequencyPanel_);
    clearFrequencyButton_->setObjectName(QStringLiteral("optimizationClearFrequencies"));
    addAmateurBandButton_ = new QPushButton(tr("Add Amateur Bands…"), explicitFrequencyPanel_);
    addAmateurBandButton_->setObjectName(QStringLiteral("optimizationAddAmateurBand"));
    addAmateurBandButton_->setToolTip(tr(
        "Choose one or more common amateur-band engineering ranges."));
    auto* frequencyActions = new QGridLayout;
    frequencyActions->setContentsMargins(0, 0, 0, 0);
    frequencyActions->addWidget(frequencyEntryControl_, 0, 0);
    frequencyActions->addWidget(addFrequencyButton_, 0, 1);
    frequencyActions->addWidget(addAmateurBandButton_, 1, 0, 1, 2);
    auto* listActions = new QHBoxLayout;
    listActions->setContentsMargins(0, 0, 0, 0);
    listActions->addStretch();
    listActions->addWidget(pasteFrequencyButton_);
    listActions->addWidget(removeFrequencyButton_);
    listActions->addWidget(clearFrequencyButton_);
    frequencyActions->addLayout(listActions, 2, 0, 1, 2);
    frequencyActions->setColumnStretch(0, 1);
    auto* frequencyHeading = new QLabel(tr("Selected frequencies (MHz)"),
        explicitFrequencyPanel_);
    frequencyLayout->addLayout(frequencyActions);
    frequencyLayout->addWidget(frequencyHeading);
    frequencyLayout->addWidget(frequencyTable_, 1);

    continuousFrequencyPanel_ = new QWidget(this);
    continuousFrequencyPanel_->setObjectName(
        QStringLiteral("optimizationContinuousFrequencyPanel"));
    auto* continuousLayout = new QGridLayout(continuousFrequencyPanel_);
    continuousLayout->setContentsMargins(0, 0, 0, 0);
    continuousStartControl_ = new FocusWheelDoubleSpinBox(continuousFrequencyPanel_);
    continuousStartControl_->setObjectName(
        QStringLiteral("optimizationContinuousStart"));
    continuousStopControl_ = new FocusWheelDoubleSpinBox(continuousFrequencyPanel_);
    continuousStopControl_->setObjectName(
        QStringLiteral("optimizationContinuousStop"));
    continuousStepControl_ = new FocusWheelDoubleSpinBox(continuousFrequencyPanel_);
    continuousStepControl_->setObjectName(
        QStringLiteral("optimizationContinuousStep"));
    for (auto* control : {continuousStartControl_, continuousStopControl_,
             continuousStepControl_}) {
        control->setRange(0.000001, 1.0e9);
        control->setDecimals(DisplayDecimalPlaces);
        control->setSuffix(tr(" MHz"));
    }
    continuousStartControl_->setValue(14.0);
    continuousStopControl_->setValue(14.35);
    continuousStepControl_->setValue(0.05);
    continuousLayout->addWidget(new QLabel(tr("Start"), continuousFrequencyPanel_), 0, 0);
    continuousLayout->addWidget(continuousStartControl_, 0, 1);
    continuousLayout->addWidget(new QLabel(tr("Stop"), continuousFrequencyPanel_), 1, 0);
    continuousLayout->addWidget(continuousStopControl_, 1, 1);
    continuousLayout->addWidget(new QLabel(tr("Step"), continuousFrequencyPanel_), 2, 0);
    continuousLayout->addWidget(continuousStepControl_, 2, 1);
    continuousLayout->setColumnStretch(1, 1);
    workloadLabel_ = new QLabel(this);
    workloadLabel_->setObjectName(QStringLiteral("optimizationWorkload"));
    workloadLabel_->setWordWrap(true);

    auto* setupContent = new QWidget(this);
    setupContent->setObjectName(QStringLiteral("optimizationSetupContent"));
    auto* setupLayout = new QVBoxLayout(setupContent);
    setupLayout->setContentsMargins(6, 6, 6, 6);
    setupLayout->setSpacing(6);

    auto* variableSection = new QGroupBox(tr("Variable"), setupContent);
    variableSection->setObjectName(QStringLiteral("optimizationVariableSection"));
    parameterSettings_ = new QWidget(variableSection);
    parameterSettings_->setObjectName(QStringLiteral("optimizationParameterSettings"));
    auto* variableSectionLayout = new QVBoxLayout(variableSection);
    variableSectionLayout->setContentsMargins(6, 8, 6, 6);
    variableSectionLayout->addWidget(parameterSettings_);
    auto* parameterizationBar = new QWidget(variableSection);
    parameterizationBar->setObjectName(QStringLiteral("optimizationParameterizationHint"));
    auto* parameterizationLayout = new QHBoxLayout(parameterizationBar);
    parameterizationLayout->setContentsMargins(0, 0, 0, 0);
    parameterizationLayout->setSpacing(6);
    auto* parameterizationHint = new QLabel(tr(
        "Need another variable? Parameterize a numeric NEC field in Structured Cards."),
        parameterizationBar);
    parameterizationHint->setWordWrap(true);
    auto* chooseParameterFields = new QPushButton(
        tr("Choose Parameter Fields…"), parameterizationBar);
    chooseParameterFields->setObjectName(
        QStringLiteral("optimizationChooseParameterFields"));
    chooseParameterFields->setToolTip(tr(
        "Open Model → NEC Deck → Structured Cards. Right-click a numeric field to create "
        "or link an SY parameter, then return here to optimize it."));
    parameterizationLayout->addWidget(parameterizationHint, 1);
    parameterizationLayout->addWidget(chooseParameterFields);
    variableSectionLayout->addWidget(parameterizationBar);
    variableSectionLayout->addWidget(variablesTable_, 1);
    auto* parameterGrid = new QGridLayout(parameterSettings_);
    parameterGrid->setContentsMargins(0, 0, 0, 0);
    parameterGrid->setHorizontalSpacing(6);
    parameterGrid->setVerticalSpacing(4);
    const auto gridLabel = [this](const QString& text,
                               const QString& objectName = {}) {
        auto* label = new QLabel(text, parameterSettings_);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (!objectName.isEmpty()) label->setObjectName(objectName);
        return label;
    };
    variableControl_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (auto* control : {minimumControl_, maximumControl_,
             adaptiveParameterToleranceControl_, adaptiveScoreToleranceControl_}) {
        control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    pointsControl_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    adaptiveMaximumEvaluationsControl_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    searchBudgetLabel_ = new QLabel(tr("Candidate count"), parameterSettings_);
    searchBudgetLabel_->setObjectName(QStringLiteral("optimizationSearchBudgetLabel"));
    searchBudgetLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    variableLabel_ = gridLabel(tr("Variable"));
    minimumLabel_ = gridLabel(tr("Minimum"));
    maximumLabel_ = gridLabel(tr("Maximum"));
    parameterGrid->addWidget(variableLabel_, 0, 0);
    parameterGrid->addWidget(variableControl_, 0, 1);
    parameterGrid->addWidget(minimumLabel_, 1, 0);
    parameterGrid->addWidget(minimumControl_, 1, 1);
    parameterGrid->addWidget(maximumLabel_, 2, 0);
    parameterGrid->addWidget(maximumControl_, 2, 1);
    parameterGrid->addWidget(searchBudgetLabel_, 3, 0);
    parameterGrid->addWidget(pointsControl_, 3, 1);
    parameterGrid->addWidget(adaptiveMaximumEvaluationsControl_, 3, 1);
    adaptiveParameterToleranceLabel_ = gridLabel(tr("Parameter tolerance"),
        QStringLiteral("optimizationParameterToleranceLabel"));
    adaptiveScoreToleranceLabel_ = gridLabel(tr("Score tolerance"),
        QStringLiteral("optimizationScoreToleranceLabel"));
    parameterGrid->addWidget(adaptiveParameterToleranceLabel_, 4, 0);
    parameterGrid->addWidget(adaptiveParameterToleranceControl_, 4, 1);
    parameterGrid->addWidget(adaptiveScoreToleranceLabel_, 5, 0);
    parameterGrid->addWidget(adaptiveScoreToleranceControl_, 5, 1);

    differentialEvolutionSettings_ = new QWidget(parameterSettings_);
    differentialEvolutionSettings_->setObjectName(
        QStringLiteral("optimizationDifferentialEvolutionSettings"));
    auto* differentialEvolutionGrid = new QGridLayout(differentialEvolutionSettings_);
    differentialEvolutionGrid->setContentsMargins(0, 0, 0, 0);
    differentialEvolutionGrid->setHorizontalSpacing(6);
    differentialEvolutionGrid->setVerticalSpacing(4);
    const auto addDifferentialEvolutionControl = [&](int row, const QString& text,
                                                     QWidget* control) {
        auto* label = new QLabel(text, differentialEvolutionSettings_);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        differentialEvolutionGrid->addWidget(label, row, 0);
        differentialEvolutionGrid->addWidget(control, row, 1);
    };
    addDifferentialEvolutionControl(0, tr("Population"),
        differentialEvolutionPopulationControl_);
    addDifferentialEvolutionControl(1, tr("Generations"),
        differentialEvolutionGenerationControl_);
    addDifferentialEvolutionControl(2, tr("Mutation factor"),
        differentialEvolutionMutationControl_);
    addDifferentialEvolutionControl(3, tr("Crossover rate"),
        differentialEvolutionCrossoverControl_);
    addDifferentialEvolutionControl(4, tr("Score tolerance"),
        differentialEvolutionScoreToleranceControl_);
    addDifferentialEvolutionControl(5, tr("Random seed"),
        differentialEvolutionSeedControl_);
    differentialEvolutionGrid->setColumnStretch(1, 1);
    parameterGrid->addWidget(differentialEvolutionSettings_, 3, 0, 3, 2);

    auto* resetSearchDefaultsButton = new QPushButton(tr("Reset Search Defaults"), parameterSettings_);
    resetSearchDefaultsButton->setObjectName(
        QStringLiteral("optimizationResetSearchDefaults"));
    resetSearchDefaultsButton->setToolTip(tr(
        "Restore the default sweep count or optimizer search settings without changing the variables, ranges, frequencies, or objective."));
    parameterGrid->addWidget(resetSearchDefaultsButton, 6, 0, 1, 2, Qt::AlignRight);
    parameterGrid->setColumnStretch(1, 1);

    connect(resetSearchDefaultsButton, &QPushButton::clicked, this, [this] {
        if (selectedSearchMethod() == SearchMethod::DifferentialEvolution) {
            differentialEvolutionPopulationControl_->setValue(12);
            differentialEvolutionGenerationControl_->setValue(20);
            differentialEvolutionMutationControl_->setValue(0.8);
            differentialEvolutionCrossoverControl_->setValue(0.9);
            differentialEvolutionScoreToleranceControl_->setValue(0.001);
            differentialEvolutionSeedControl_->setValue(5489);
        } else if (isMultivariable(selectedSearchMethod())) {
            adaptiveMaximumEvaluationsControl_->setValue(21);
            adaptiveParameterToleranceControl_->setValue(0.010);
            adaptiveScoreToleranceControl_->setValue(0.001);
        } else {
            pointsControl_->setValue(7);
        }
    });

    auto* frequencySection = new QGroupBox(tr("Frequencies"), setupContent);
    frequencySection->setObjectName(QStringLiteral("optimizationFrequencySection"));
    auto* frequencyPageLayout = new QVBoxLayout(frequencySection);
    frequencyPageLayout->setContentsMargins(6, 8, 6, 6);
    frequencyPageLayout->setSpacing(4);
    frequencySummaryLabel_ = new QLabel(frequencySection);
    frequencySummaryLabel_->setObjectName(
        QStringLiteral("optimizationFrequencySummary"));
    frequencySummaryLabel_->setWordWrap(true);
    editFrequencyButton_ = new QPushButton(tr("Edit…"), frequencySection);
    editFrequencyButton_->setObjectName(
        QStringLiteral("optimizationEditFrequencies"));
    auto* frequencySummaryRow = new QHBoxLayout;
    frequencySummaryRow->addWidget(frequencySummaryLabel_, 1);
    frequencySummaryRow->addWidget(editFrequencyButton_, 0, Qt::AlignTop);
    frequencyPageLayout->addLayout(frequencySummaryRow);
    frequencyPageLayout->addWidget(workloadLabel_);

    frequencyDialog_ = new QDialog(this);
    frequencyDialog_->setObjectName(QStringLiteral("optimizationFrequencyDialog"));
    frequencyDialog_->setWindowTitle(tr("Optimization Frequencies"));
    frequencyDialog_->setModal(true);
    frequencyDialog_->resize(720, 480);
    auto* frequencyDialogLayout = new QVBoxLayout(frequencyDialog_);
    auto* frequencyModeRow = new QHBoxLayout;
    frequencyModeRow->addWidget(new QLabel(tr("Frequency source"), frequencyDialog_));
    frequencyModeRow->addWidget(frequencyModeControl_, 1);
    frequencyDialogLayout->addLayout(frequencyModeRow);
    frequencyDialogLayout->addWidget(explicitFrequencyPanel_, 1);
    frequencyDialogLayout->addWidget(continuousFrequencyPanel_);
    auto* frequencyDialogButtons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, frequencyDialog_);
    frequencyDialogLayout->addWidget(frequencyDialogButtons);
    connect(frequencyDialogButtons, &QDialogButtonBox::accepted,
        frequencyDialog_, &QDialog::accept);
    connect(frequencyDialogButtons, &QDialogButtonBox::rejected,
        frequencyDialog_, &QDialog::reject);

    auto* objectiveSection = new QGroupBox(tr("Objective"), setupContent);
    objectiveSection->setObjectName(QStringLiteral("optimizationObjectiveSection"));
    auto* objectiveLayout = new QVBoxLayout(objectiveSection);
    objectiveLayout->setContentsMargins(6, 8, 6, 6);
    objectiveLayout->setSpacing(4);
    objectiveSummaryLabel_ = new QLabel(objectiveSection);
    objectiveSummaryLabel_->setObjectName(
        QStringLiteral("optimizationObjectiveSummary"));
    objectiveSummaryLabel_->setWordWrap(true);
    editObjectiveButton_ = new QPushButton(tr("Edit…"), objectiveSection);
    editObjectiveButton_->setObjectName(
        QStringLiteral("optimizationEditObjective"));
    auto* objectiveSummaryRow = new QHBoxLayout;
    objectiveSummaryRow->addWidget(objectiveSummaryLabel_, 1);
    objectiveSummaryRow->addWidget(editObjectiveButton_, 0, Qt::AlignTop);
    objectiveLayout->addLayout(objectiveSummaryRow);

    objectiveDialog_ = new QDialog(this);
    objectiveDialog_->setObjectName(QStringLiteral("optimizationObjectiveDialog"));
    objectiveDialog_->setWindowTitle(tr("Optimization Objective"));
    objectiveDialog_->setModal(true);
    objectiveDialog_->resize(920, 460);
    auto* objectiveDialogLayout = new QVBoxLayout(objectiveDialog_);
    auto* objectiveSettings = new QWidget(objectiveDialog_);
    objectiveSettings->setObjectName(QStringLiteral("optimizationObjectiveSettings"));
    auto* objectiveForm = new QFormLayout(objectiveSettings);
    objectiveForm->setContentsMargins(0, 0, 0, 0);
    targetFrequencyLabel_ = new QLabel(tr("Selected frequency"), objectiveSettings);
    targetFrequencyLabel_->setObjectName(
        QStringLiteral("optimizationTargetFrequencyLabel"));
    objectiveControl_->hide();
    targetFrequencyLabel_->hide();
    targetFrequencyControl_->hide();
    objectiveForm->addRow(tr("Reference impedance"), referenceImpedanceControl_);
    objectiveForm->addRow(tr("Forward theta"), forwardThetaControl_);
    objectiveForm->addRow(tr("Forward phi"), forwardPhiControl_);
    objectiveForm->addRow(tr("Radiation component"), radiationComponentControl_);
    directionalFrequencyNote_ = new QLabel(objectiveSettings);
    directionalFrequencyNote_->setObjectName(
        QStringLiteral("optimizationDirectionalFrequencyNote"));
    directionalFrequencyNote_->setWordWrap(true);
    objectiveForm->addRow(directionalFrequencyNote_);
    objectiveForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    objectiveDialogLayout->addWidget(objectiveSettings);
    objectiveDialogLayout->addWidget(objectiveCriteriaTable_);
    auto* objectiveExplanationHeading = new QLabel(tr("Objective Summary"), objectiveDialog_);
    auto explanationHeadingFont = objectiveExplanationHeading->font();
    explanationHeadingFont.setBold(true);
    objectiveExplanationHeading->setFont(explanationHeadingFont);
    objectiveDialogLayout->addWidget(objectiveExplanationHeading);
    objectiveExplanationLabel_ = new QLabel(objectiveDialog_);
    objectiveExplanationLabel_->setObjectName(
        QStringLiteral("optimizationObjectiveExplanation"));
    objectiveExplanationLabel_->setWordWrap(true);
    objectiveDialogLayout->addWidget(objectiveExplanationLabel_);
    resetObjectiveDefaultsButton_ = new QPushButton(tr("Restore Objective Defaults"), objectiveDialog_);
    resetObjectiveDefaultsButton_->setObjectName(QStringLiteral("optimizationResetObjectiveDefaults"));
    objectiveDialogLayout->addWidget(resetObjectiveDefaultsButton_, 0, Qt::AlignRight);
    auto* objectiveDialogButtons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, objectiveDialog_);
    objectiveDialogLayout->addWidget(objectiveDialogButtons);
    connect(objectiveDialogButtons, &QDialogButtonBox::accepted,
        objectiveDialog_, &QDialog::accept);
    connect(objectiveDialogButtons, &QDialogButtonBox::rejected,
        objectiveDialog_, &QDialog::reject);

    setupLayout->addWidget(variableSection);
    setupLayout->addWidget(frequencySection);
    setupLayout->addWidget(objectiveSection);
    setupLayout->addStretch();
    variablesTable_->hide();

    studySummaryLabel_ = new QLabel(this);
    studySummaryLabel_->setObjectName(QStringLiteral("optimizationStudySummary"));
    studySummaryLabel_->setWordWrap(true);
    auto* configurationScrollArea = new QScrollArea(this);
    configurationScrollArea->setObjectName(
        QStringLiteral("optimizationConfigurationScrollArea"));
    configurationScrollArea->setFrameShape(QFrame::NoFrame);
    configurationScrollArea->setWidgetResizable(true);
    configurationScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    configurationScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    configurationScrollArea->setWidget(setupContent);

    auto* configurationPanel = new QWidget(this);
    configurationPanel->setObjectName(QStringLiteral("optimizationConfigurationPanel"));
    auto* configurationLayout = new QVBoxLayout(configurationPanel);
    configurationLayout->setContentsMargins(0, 0, 0, 0);
    configurationLayout->addWidget(configurationScrollArea, 1);
    configurationPanel->setMinimumWidth(280);
    configurationPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* buttons = new QHBoxLayout;
    runButton_ = new QPushButton(tr("Run Parameter Sweep"), this);
    cancelButton_ = new QPushButton(tr("Stop"), this);
    cancelButton_->setEnabled(false);
    applyBestButton_ = new QPushButton(tr("Apply Best to Model"), this);
    applyBestButton_->setObjectName(QStringLiteral("optimizationApplyBest"));
    applyBestButton_->setEnabled(false);
    applyBestButton_->setToolTip(tr(
        "Replace the selected SY expression with the best numeric value. This model edit is undoable."));
    progress_ = new QProgressBar(this);
    progress_->setTextVisible(true);
    searchMethodTabs_->setExpanding(false);
    buttons->addWidget(searchMethodTabs_);
    buttons->addWidget(studySummaryLabel_, 1);
    buttons->addWidget(runButton_);
    buttons->addWidget(cancelButton_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    bestLabel_ = new QLabel(tr("No optimization results yet."), this);
    bestLabel_->setWordWrap(true);
    resultsTable_ = new QTableWidget(this);
    resultsTable_->setObjectName(QStringLiteral("optimizationResultsTable"));
    resultsTable_->setColumnCount(ResultColumnCount);
    resultsTable_->setHorizontalHeaderLabels({tr("Value"), tr("Objective Score"), tr("SWR"), tr("Frequency"),
        tr("R (Ω)"), tr("X (Ω)"), tr("Gain (dBi)"), tr("F/B (dB)"), tr("F/R (dB)"), tr("Status"), tr("Run")});
    resultsTable_->horizontalHeaderItem(GainColumn)->setToolTip(tr(
        "Forward gain at the configured theta, phi, frequency, and radiation component."));
    resultsTable_->horizontalHeaderItem(FrontToBackColumn)->setToolTip(tr(
        "Gain in the configured forward direction minus gain at the physical opposite direction."));
    resultsTable_->horizontalHeaderItem(FrontToRearColumn)->setToolTip(tr(
        "Gain in the configured forward direction minus the strongest response in the rear 180° half of the configured azimuth cut."));
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable_->verticalHeader()->hide();
    resultsTable_->horizontalHeader()->setSectionResizeMode(StatusColumn, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(RunColumn, QHeaderView::ResizeToContents);

    candidatePlots_ = new CandidatePlotsView(this);
    candidatePlots_->setCandidateActivatedCallback([this](int row) {
        if (row < 0 || row >= resultsTable_->rowCount()) return;
        resultsTable_->selectRow(row);
        showCandidateDetails(row);
    });

    candidateDetailsWindow_ = new QDialog(nullptr, Qt::Window);
    candidateDetailsWindow_->setObjectName(
        QStringLiteral("optimizationCandidateDetailsWindow"));
    candidateDetailsWindow_->setWindowTitle(tr("Candidate Frequency Results"));
    candidateDetailsWindow_->setModal(false);
    candidateDetailsWindow_->setAttribute(Qt::WA_QuitOnClose, false);
    candidateDetailsWindow_->resize(900, 650);
    auto* detailLayout = new QVBoxLayout(candidateDetailsWindow_);
    candidateDetailLabel_ = new QLabel(candidateDetailsWindow_);
    candidateDetailLabel_->setObjectName(QStringLiteral("optimizationCandidateDetailLabel"));
    candidateDetailLabel_->setWordWrap(true);
    candidateDetailsTable_ = new QTableWidget(candidateDetailsWindow_);
    candidateDetailsTable_->setObjectName(QStringLiteral("optimizationCandidateDetails"));
    candidateDetailsTable_->setColumnCount(7);
    candidateDetailsTable_->setHorizontalHeaderLabels(
        {tr("Frequency (MHz)"), tr("SWR"), tr("R (Ω)"), tr("X (Ω)"),
            tr("Gain (dBi)"), tr("F/B (dB)"), tr("F/R (dB)")});
    candidateDetailsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    candidateDetailsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    candidateDetailsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    candidateDetailsTable_->verticalHeader()->hide();
    candidateDetailsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    candidateDetailPlots_ = new SweepPlotsView(candidateDetailsWindow_);
    candidateDetailPlots_->setObjectName(
        QStringLiteral("optimizationCandidateDetailPlots"));
    candidateDirectionalPlots_ = new DirectionalMetricsView(candidateDetailsWindow_);
    candidateDetailViews_ = new QTabWidget(candidateDetailsWindow_);
    candidateDetailViews_->setObjectName(
        QStringLiteral("optimizationCandidateDetailViews"));
    candidateDetailViews_->setDocumentMode(true);
    candidateDetailViews_->addTab(candidateDetailsTable_, tr("Frequency Table"));
    candidateDetailViews_->addTab(candidateDetailPlots_, tr("SWR & Impedance Plots"));
    const auto directionalTab = candidateDetailViews_->addTab(
        candidateDirectionalPlots_, tr("Directional Plots"));
    candidateDetailViews_->setTabEnabled(directionalTab, false);
    candidateDetailViews_->setTabToolTip(directionalTab, tr(
        "Available after a completed candidate evaluates a Gain, F/B, or F/R objective."));
    applyCandidateButton_ = new QPushButton(tr("Apply This Candidate to Model"),
        candidateDetailsWindow_);
    applyCandidateButton_->setObjectName(QStringLiteral("optimizationApplyCandidate"));
    applyCandidateButton_->setToolTip(tr(
        "Update the active model's optimized SY values without running an analysis."));
    applyCandidateAndRunButton_ = new QPushButton(tr("Apply This Candidate and Run"),
        candidateDetailsWindow_);
    applyCandidateAndRunButton_->setObjectName(
        QStringLiteral("optimizationApplyCandidateAndRun"));
    applyCandidateAndRunButton_->setToolTip(tr(
        "Update the active model's optimized SY values, then start a normal analysis using the model's current requests."));
    auto* candidateActionRow = new QHBoxLayout;
    candidateActionRow->addStretch();
    candidateActionRow->addWidget(applyCandidateButton_);
    candidateActionRow->addWidget(applyCandidateAndRunButton_);
    detailLayout->addWidget(candidateDetailLabel_);
    detailLayout->addWidget(candidateDetailViews_, 1);
    detailLayout->addLayout(candidateActionRow);
    resetCandidateDetails();

    auto* resultsPanel = new QWidget(this);
    resultsPanel->setObjectName(QStringLiteral("optimizationResultsPanel"));
    auto* resultsLayout = new QVBoxLayout(resultsPanel);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    auto* resultStatusRow = new QHBoxLayout;
    resultStatusRow->addWidget(bestLabel_, 1);
    resultStatusRow->addWidget(applyBestButton_);
    resultsLayout->addWidget(progress_);
    resultsLayout->addWidget(statusLabel_);
    resultsLayout->addLayout(resultStatusRow);
    auto* resultsSplitter = new QSplitter(Qt::Vertical, resultsPanel);
    resultsSplitter->setObjectName(QStringLiteral("optimizationResultsSplitter"));
    resultsSplitter->addWidget(resultsTable_);
    resultsSplitter->addWidget(candidatePlots_);
    resultsSplitter->setChildrenCollapsible(false);
    resultsSplitter->setStretchFactor(0, 3);
    resultsSplitter->setStretchFactor(1, 2);
    resultsSplitter->setSizes({360, 280});
    resultsLayout->addWidget(resultsSplitter, 1);
    resultsPanel->setMinimumWidth(420);

    auto* workspaceSplitter = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter->setObjectName(QStringLiteral("optimizationWorkspaceSplitter"));
    workspaceSplitter->addWidget(configurationPanel);
    workspaceSplitter->addWidget(resultsPanel);
    workspaceSplitter->setChildrenCollapsible(false);
    workspaceSplitter->setStretchFactor(0, 0);
    workspaceSplitter->setStretchFactor(1, 1);
    workspaceSplitter->setSizes({320, 760});

    layout->addWidget(historicalBanner_);
    layout->addLayout(buttons);
    layout->addWidget(workspaceSplitter, 1);

    connect(variableControl_, &QComboBox::currentIndexChanged, this, [this] { updateBounds(); });
    for (auto* control : {swrWeightControl_, resistanceWeightControl_,
             reactanceWeightControl_, forwardGainWeightControl_,
             frontToBackWeightControl_,
             frontToRearWeightControl_,
             forwardThetaControl_, forwardPhiControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
            updateWorkload();
            updateReadiness();
        });
    }
    for (auto row = 0; row < 6; ++row) {
        connect(objectiveValueControls_[row], &QDoubleSpinBox::valueChanged,
            this, [this] { updateObjectiveControls(); });
        connect(objectiveGoalControls_[row], &QComboBox::currentIndexChanged,
            this, [this, row] {
                objectiveGoalControls_[row]->setToolTip(
                    objectiveGoalControls_[row]->currentData(Qt::ToolTipRole).toString());
                configureAggregationControl(objectiveAggregationControls_[row],
                    selectedGoal(objectiveGoalControls_[row]));
                updateObjectiveControls();
            });
        connect(objectiveAggregationControls_[row], &QComboBox::currentIndexChanged,
            this, [this, row] {
                objectiveAggregationControls_[row]->setToolTip(
                    objectiveAggregationControls_[row]
                        ->currentData(Qt::ToolTipRole).toString());
                updateObjectiveControls();
            });
    }
    connect(resetObjectiveDefaultsButton_, &QPushButton::clicked, this, [this] {
        auto defaults = analysis::OptimizationObjectiveSpec{};
        defaults.referenceImpedance = referenceImpedanceControl_->value();
        restoreObjectiveSetup(defaults);
        updateObjectiveControls();
    });
    connect(radiationComponentControl_, &QComboBox::currentIndexChanged, this, [this] {
        updateWorkload();
        updateReadiness();
    });
    connect(frequencyModeControl_, &QComboBox::currentIndexChanged,
        this, [this] { updateFrequencyControls(); });
    connect(pointsControl_, &QSpinBox::valueChanged, this, [this] { updateWorkload(); });
    for (auto* control : {minimumControl_, maximumControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] { updateWorkload(); });
    }
    connect(searchMethodTabs_, &QTabBar::currentChanged,
        this, [this] { updateSearchMethodControls(); });
    connect(adaptiveMaximumEvaluationsControl_, &QSpinBox::valueChanged,
        this, [this] { updateWorkload(); });
    for (auto* control : {differentialEvolutionPopulationControl_,
             differentialEvolutionGenerationControl_, differentialEvolutionSeedControl_}) {
        connect(control, &QSpinBox::valueChanged, this, [this] {
            updateWorkload();
            updateReadiness();
        });
    }
    for (auto* control : {differentialEvolutionMutationControl_,
             differentialEvolutionCrossoverControl_,
             differentialEvolutionScoreToleranceControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
            updateWorkload();
            updateReadiness();
        });
    }
    connect(frequencyTable_, &QListWidget::itemChanged, this, [this] {
        updateWorkload();
        updateReadiness();
    });
    for (auto* control : {continuousStartControl_, continuousStopControl_,
             continuousStepControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
            updateWorkload();
            updateReadiness();
        });
    }
    connect(addFrequencyButton_, &QPushButton::clicked, this, [this] {
        addExplicitFrequency(frequencyEntryControl_->value());
    });
    connect(removeFrequencyButton_, &QPushButton::clicked,
        this, [this] { removeSelectedFrequencies(); });
    connect(pasteFrequencyButton_, &QPushButton::clicked,
        this, [this] { pasteExplicitFrequencies(); });
    connect(clearFrequencyButton_, &QPushButton::clicked,
        this, [this] { clearExplicitFrequencies(); });
    connect(editFrequencyButton_, &QPushButton::clicked,
        this, [this] { editFrequencySetup(); });
    connect(editObjectiveButton_, &QPushButton::clicked,
        this, [this] { editObjectiveSetup(); });
    auto* deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), frequencyTable_);
    connect(deleteShortcut, &QShortcut::activated,
        this, [this] { removeSelectedFrequencies(); });
    auto* backspaceShortcut = new QShortcut(
        QKeySequence(Qt::Key_Backspace), frequencyTable_);
    connect(backspaceShortcut, &QShortcut::activated,
        this, [this] { removeSelectedFrequencies(); });
    connect(addAmateurBandButton_, &QPushButton::clicked,
        this, [this] { chooseAmateurBands(); });
    connect(resultsTable_, &QTableWidget::cellDoubleClicked,
        this, [this](int row, int) { showCandidateDetails(row); });
    connect(variablesTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        const auto* item = variablesTable_->item(row, 1);
        const auto index = item == nullptr ? -1 : variableControl_->findText(item->text());
        if (index >= 0) variableControl_->setCurrentIndex(index);
    });
    connect(variablesTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        updateWorkload();
        updateReadiness();
    });
    connect(runButton_, &QPushButton::clicked, this, [this] { startSweep(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelSweep(); });
    connect(applyBestButton_, &QPushButton::clicked, this, [this] {
        applyCandidate(bestRow_, false);
    });
    connect(applyCandidateButton_, &QPushButton::clicked,
        this, [this] { applyCandidate(detailCandidateRow_, false); });
    connect(applyCandidateAndRunButton_, &QPushButton::clicked,
        this, [this] { applyCandidate(detailCandidateRow_, true); });
    connect(evaluator_, &CandidateEvaluator::finished, this,
        [this](CandidateEvaluationResult result) {
            finishCurrentCandidate(std::move(result));
        });
    connect(returnToCurrentWorkButton_, &QPushButton::clicked, this, [this] {
        if (returnToCurrentWorkCallback_) returnToCurrentWorkCallback_();
    });
    connect(chooseParameterFields, &QPushButton::clicked, this, [this] {
        if (parameterizationHelpCallback_) parameterizationHelpCallback_();
    });
    updateObjectiveControls();
    updateFrequencyControls();
    updateSearchMethodControls();
}

OptimizationWorkspace::~OptimizationWorkspace()
{
    delete candidateDetailsWindow_;
}

void OptimizationWorkspace::setContext(QString source, QString sourceFile, QString backend,
    QString executable, int timeoutSeconds, bool modelValid)
{
    if (isRunning() || historicalSession_) return;
    const auto preserveStudy = std::exchange(preserveStudyOnNextContextUpdate_, false);
    const auto sourceUnchanged = source_ == source && sourceFile_ == sourceFile;
    const auto preserveFrequencySelection = contextInitialized_ && sourceFile_ == sourceFile;
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
        updateWorkload();
        updateReadiness();
        return;
    }
    const auto resolution = nec::NecSymbolResolver{}.resolve(source_.toStdString());
    populateVariables(resolution);
    if (resolution.ok()) {
        const auto setup = nec::NecSetupConverter{}.convert(
            nec::NecParser{}.parse(resolution.generatedDeck));
        referenceImpedanceControl_->setValue(model::referenceImpedanceOhms(setup));
        resistanceTargetControl_->setValue(model::referenceImpedanceOhms(setup));
        if (setup.frequency) {
            targetFrequencyControl_->setValue(setup.frequency->startMHz);
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
        bestLabel_->setText(tr("No optimization results yet."));
        resetCandidateDetails();
    }
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::setExternalRunActive(bool active)
{
    externalRunActive_ = active;
    updateReadiness();
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

auto OptimizationWorkspace::isRunning() const noexcept -> bool
{
    return evaluator_->isRunning() || candidateIndex_ < candidates_.size();
}

auto OptimizationWorkspace::loadSession(const QString& sessionId) -> bool
{
    auto records = runStore_.load();
    const auto session = std::ranges::find(records, sessionId, &AnalysisRunRecord::id);
    if (session == records.end() || session->runType != QStringLiteral("optimization-session")) return false;
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
    searchMethodTabs_->setCurrentIndex(static_cast<int>(restoredSearchMethod));
    activeSearchMethod_ = restoredSearchMethod;
    adaptiveMaximumEvaluationsControl_->setValue(sessionMetadata.value(
        QStringLiteral("candidateCount")).toInt(21));
    adaptiveParameterToleranceControl_->setValue(sessionMetadata.value(
        QStringLiteral("parameterTolerance")).toDouble(0.010));
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
        resultsTable_->setItem(row, GainColumn,
            new QTableWidgetItem(QStringLiteral("—")));
        resultsTable_->setItem(row, FrontToBackColumn,
            new QTableWidgetItem(QStringLiteral("—")));
        resultsTable_->setItem(row, FrontToRearColumn,
            new QTableWidgetItem(QStringLiteral("—")));
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
        QFile outputFile(QDir(candidateRecords[index].directory).filePath(QStringLiteral("model.out")));
        analysis::AnalysisResult result;
        if (outputFile.open(QIODevice::ReadOnly))
            result = analysis::NecOutputParser{}.parse(outputFile.readAll().toStdString());
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
        restoredObjective.radiationComponent = static_cast<analysis::RadiationComponent>(
            metadata.value(QStringLiteral("radiationComponent")).toInt(
                static_cast<int>(analysis::RadiationComponent::Total)));
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
        const auto evaluation = analysis::evaluateOptimizationObjective(
            result, restoredObjective);
        if (evaluation) {
            resultsTable_->setItem(row, ScoreColumn, numericItem(evaluation->score));
            resultsTable_->setItem(row, FrequencyColumn,
                evaluation->evaluatedFrequencyCount == 1
                    ? numericItem(evaluation->evaluationFrequencyMHz)
                    : new QTableWidgetItem(QStringLiteral("—")));
            if (evaluation->feedpoint) {
                resultsTable_->setItem(row, SwrColumn, numericItem(evaluation->swr));
                resultsTable_->setItem(row, ResistanceColumn,
                    numericItem(evaluation->feedpoint->impedance.real()));
                resultsTable_->setItem(row, ReactanceColumn,
                    numericItem(evaluation->feedpoint->impedance.imag()));
            }
            resultsTable_->setItem(row, GainColumn, evaluation->forwardGainDb
                ? numericItem(*evaluation->forwardGainDb)
                : new QTableWidgetItem(QStringLiteral("—")));
            resultsTable_->setItem(row, FrontToBackColumn, evaluation->frontToBackDb
                ? numericItem(*evaluation->frontToBackDb)
                : new QTableWidgetItem(QStringLiteral("—")));
            resultsTable_->setItem(row, FrontToRearColumn, evaluation->frontToRearDb
                ? numericItem(*evaluation->frontToRearDb)
                : new QTableWidgetItem(QStringLiteral("—")));
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
        candidate.refinementRound = metadata.value(QStringLiteral("refinementRound")).toInt();
        candidate.trialRole = metadata.value(QStringLiteral("trialRole")).toString();
        candidates_.push_back(std::move(candidate));
        resultsTable_->setItem(row, ValueColumn,
            new QTableWidgetItem(candidateDescription(candidates_.back())));
        setCandidateStatus(row, candidateRecords[index].status);
        updateCandidateRowToolTip(row);
    }
    historicalSession_ = true;
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
    if (bestRow_ >= 0) {
        resultsTable_->selectRow(bestRow_);
    }
    updateWorkload();
    updateReadiness();
    return true;
}

void OptimizationWorkspace::leaveHistoricalSession()
{
    if (!historicalSession_) return;
    historicalSession_ = false;
    historicalBanner_->hide();
    if (activeFrequencySelection_) {
        restoreFrequencySelection(*activeFrequencySelection_);
        activeFrequencySelection_.reset();
    }
    updateSearchMethodControls();
    updateReadiness();
}

void OptimizationWorkspace::cancelAndWait()
{
    cancelRequested_ = true;
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
    variablesTable_->setRowCount(static_cast<int>(definitions_.size()));
    variableControl_->clear();
    selectedValueSuffix_.clear();
    minimumControl_->setSuffix({});
    maximumControl_->setSuffix({});
    resultsTable_->horizontalHeaderItem(ValueColumn)->setText(tr("Value"));
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
        variableControl_->addItem(name, definition.value);
    }
    statusLabel_->setText(resolution.ok()
        ? definitions_.empty() ? tr("Add SY declarations to enable parameter optimization.")
            : tr("Choose one resolved symbol and a bounded range.")
        : tr("Resolve the model's SY expression errors before optimizing."));
    updateBounds();
}

auto OptimizationWorkspace::selectedAdaptiveVariables() const
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

void OptimizationWorkspace::updateBounds()
{
    if (variableControl_->currentIndex() < 0) return;
    const auto value = variableControl_->currentData().toDouble();
    selectedValueSuffix_.clear();
    minimumControl_->setSuffix({});
    maximumControl_->setSuffix({});
    resultsTable_->horizontalHeaderItem(ValueColumn)->setText(
        isMultivariable(selectedSearchMethod()) ? tr("Parameters") : tr("Value"));
    auto first = value == 0.0 ? -1.0 : value * 0.8;
    auto second = value == 0.0 ? 1.0 : value * 1.2;
    if (first > second) std::swap(first, second);
    minimumControl_->setValue(first);
    maximumControl_->setValue(second);
    updateWorkload();
    updateReadiness();
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
    forwardThetaControl_->setEnabled(directional && !isRunning() && !historicalSession_);
    forwardPhiControl_->setEnabled(directional && !isRunning() && !historicalSession_);
    radiationComponentControl_->setEnabled(directional && !isRunning() && !historicalSession_);
    updateFrequencyControls();
}

void OptimizationWorkspace::updateFrequencyControls()
{
    const auto explicitMode = selectedFrequencyMode() == FrequencyMode::Explicit;
    const auto continuousMode = selectedFrequencyMode() == FrequencyMode::Continuous;
    explicitFrequencyPanel_->setVisible(explicitMode);
    continuousFrequencyPanel_->setVisible(continuousMode);
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::updateSearchMethodControls()
{
    const auto method = selectedSearchMethod();
    const auto multivariable = isMultivariable(method);
    const auto differentialEvolution = method == SearchMethod::DifferentialEvolution;
    resultsTable_->horizontalHeaderItem(ValueColumn)->setText(
        multivariable ? tr("Parameters") : tr("Value"));
    variablesTable_->setVisible(multivariable);
    variableLabel_->setVisible(!multivariable);
    variableControl_->setVisible(!multivariable);
    minimumLabel_->setVisible(!multivariable);
    minimumControl_->setVisible(!multivariable);
    maximumLabel_->setVisible(!multivariable);
    maximumControl_->setVisible(!multivariable);
    searchBudgetLabel_->setText(multivariable ? tr("Maximum evaluations") : tr("Candidate count"));
    searchBudgetLabel_->setVisible(!differentialEvolution);
    pointsControl_->setVisible(!multivariable);
    adaptiveMaximumEvaluationsControl_->setVisible(multivariable && !differentialEvolution);
    adaptiveParameterToleranceLabel_->setVisible(false);
    adaptiveParameterToleranceControl_->setVisible(false);
    adaptiveScoreToleranceLabel_->setVisible(multivariable && !differentialEvolution);
    adaptiveScoreToleranceControl_->setVisible(multivariable && !differentialEvolution);
    differentialEvolutionSettings_->setVisible(differentialEvolution);
    pointsControl_->setEnabled(!multivariable && !isRunning() && !historicalSession_);
    adaptiveMaximumEvaluationsControl_->setEnabled(multivariable && !differentialEvolution && !isRunning()
        && !historicalSession_);
    adaptiveParameterToleranceControl_->setEnabled(multivariable && !isRunning()
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
    const std::array names{tr("SWR"), tr("resistance"), tr("reactance"),
        tr("forward gain"), tr("F/B"), tr("F/R")};
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
    objectiveSummaryLabel_->setText(tr("Reference Z %1 Ω\n%2")
        .arg(formatDecimal(setup.objective.referenceImpedance))
        .arg(criteria.isEmpty() ? tr("No enabled criteria") : criteria.join(QLatin1Char('\n'))));
    objectiveExplanationLabel_->setText(criteria.isEmpty()
        ? tr("No criteria are enabled. Set at least one weight above zero.")
        : criteria.join(QLatin1Char('\n')));
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
    const auto directionalSamplesPerFrequency = setup.objective.frontToRearWeight > 0.0
        ? 38U
        : (setup.objective.forwardGainWeight > 0.0
            || setup.objective.frontToBackWeight > 0.0 ? 1U : 0U)
            + (setup.objective.frontToBackWeight > 0.0 ? 1U : 0U);
    if (directionalSamplesPerFrequency > 0) {
        workload += tr(" · %1 directional RP samples")
            .arg(static_cast<qulonglong>(candidateCount * frequencyCount
                * static_cast<std::size_t>(directionalSamplesPerFrequency)));
    }
    workloadLabel_->setText(workload);
    QStringList variableNames;
    for (const auto& variable : setup.variables) variableNames.append(variable.name);
    const auto variableSummary = isMultivariable(setup.searchMethod)
        ? tr("%1 parameter(s): %2").arg(variableNames.size()).arg(variableNames.join(QStringLiteral(", ")))
        : setup.variable.isEmpty() ? tr("None") : setup.variable;
    const auto rangeSummary = isMultivariable(setup.searchMethod)
        ? tr("per-parameter bounds")
        : tr("%1 to %2").arg(formatDecimal(setup.minimum), formatDecimal(setup.maximum));
    studySummaryLabel_->setText(tr("Variables: %1  |  Range: %2  |  Frequencies: %3  |  Goal: %4")
        .arg(variableSummary)
        .arg(rangeSummary)
        .arg(frequencyCount)
        .arg(tr("Per-criterion")));
    updateSetupSummaries();
}

void OptimizationWorkspace::populateModelFrequencies(const model::FrequencyDefinition& frequency)
{
    modelFrequenciesMHz_.clear();
    const auto count = std::max(1, frequency.count);
    modelFrequenciesMHz_.reserve(static_cast<std::size_t>(count));
    for (auto index = 0; index < count; ++index) {
        const auto frequencyMHz = frequencyAt(frequency, index);
        if (std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            modelFrequenciesMHz_.push_back(frequencyMHz);
    }
    if (!modelFrequenciesMHz_.empty()) {
        continuousStartControl_->setValue(modelFrequenciesMHz_.front());
        continuousStopControl_->setValue(modelFrequenciesMHz_.back());
        if (modelFrequenciesMHz_.size() > 1)
            continuousStepControl_->setValue(
                std::abs(modelFrequenciesMHz_[1] - modelFrequenciesMHz_[0]));
    }
    setExplicitFrequencies(modelFrequenciesMHz_);
}

void OptimizationWorkspace::setExplicitFrequencies(
    const std::vector<double>& frequenciesMHz)
{
    const QSignalBlocker blocker(frequencyTable_);
    frequencyTable_->clear();
    for (const auto frequencyMHz : frequenciesMHz) {
        auto* item = new QListWidgetItem(formatDecimal(frequencyMHz), frequencyTable_);
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(tr("%1 MHz").arg(formatDecimal(frequencyMHz)));
    }
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::addExplicitFrequency(double frequencyMHz)
{
    if (!std::isfinite(frequencyMHz) || frequencyMHz <= 0.0) return;
    auto frequencies = explicitFrequencies();
    frequencies.push_back(frequencyMHz);
    setExplicitFrequencies(analysis::frequencyPlanPoints({
        analysis::FrequencyPlanMode::Explicit, std::move(frequencies), {}}));
}

void OptimizationWorkspace::removeSelectedFrequencies()
{
    if (!frequencyTable_->isEnabled()) return;
    std::set<int, std::greater<>> rows;
    for (const auto* item : frequencyTable_->selectedItems())
        rows.insert(frequencyTable_->row(item));
    for (const auto row : rows) delete frequencyTable_->takeItem(row);
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::clearExplicitFrequencies()
{
    if (!frequencyTable_->isEnabled() || frequencyTable_->count() == 0) return;
    QMessageBox confirmation(this);
    confirmation.setWindowTitle(tr("Clear Selected Frequencies"));
    confirmation.setText(tr("Clear every frequency from the editable list?\n"
                            "The model's FR card will not be changed."));
    auto* clearButton = confirmation.addButton(
        tr("Clear All"), QMessageBox::DestructiveRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != clearButton) return;
    frequencyTable_->clear();
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::chooseAmateurBands()
{
    const auto& presets = analysis::amateurBandPresets();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("optimizationAmateurBandDialog"));
    dialog.setWindowTitle(tr("Add Amateur Bands"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(tr(
        "Select one or more engineering ranges. These presets do not define local operating privileges."),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* bandLayout = new QGridLayout;
    std::vector<QCheckBox*> checks;
    checks.reserve(presets.size());
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto& preset = presets[index];
        auto* check = new QCheckBox(tr("%1  (%2–%3 MHz)")
            .arg(QString::fromUtf8(preset.name.data(),
                static_cast<qsizetype>(preset.name.size())))
            .arg(preset.startMHz, 0, 'f', 3)
            .arg(preset.endMHz, 0, 'f', 3), &dialog);
        check->setObjectName(QStringLiteral("optimizationAmateurBandCheck%1").arg(index));
        checks.push_back(check);
        bandLayout->addWidget(check, static_cast<int>(index % 6),
            static_cast<int>(index / 6));
    }
    layout->addLayout(bandLayout);

    auto* summary = new QLabel(&dialog);
    summary->setObjectName(QStringLiteral("optimizationAmateurBandSummary"));
    auto* selectionButtons = new QHBoxLayout;
    auto* selectAll = new QPushButton(tr("Select All"), &dialog);
    auto* clearSelection = new QPushButton(tr("Clear"), &dialog);
    selectionButtons->addWidget(selectAll);
    selectionButtons->addWidget(clearSelection);
    selectionButtons->addStretch();
    selectionButtons->addWidget(summary);
    layout->addLayout(selectionButtons);

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* addSelected = dialogButtons->addButton(
        tr("Add Selected"), QDialogButtonBox::AcceptRole);
    addSelected->setObjectName(QStringLiteral("optimizationAddSelectedBands"));
    layout->addWidget(dialogButtons);

    const auto updateSummary = [&checks, &presets, summary, addSelected] {
        analysis::FrequencyPlan selected{
            .mode = analysis::FrequencyPlanMode::Explicit,
            .pointsMHz = {},
            .ranges = {},
        };
        auto selectedCount = 0;
        for (std::size_t index = 0; index < checks.size(); ++index) {
            if (!checks[index]->isChecked()) continue;
            ++selectedCount;
            selected.ranges.push_back({presets[index].startMHz,
                presets[index].endMHz, presets[index].stepMHz});
        }
        const auto pointCount = analysis::frequencyPlanPoints(selected).size();
        summary->setText(tr("%1 bands · %2 frequency points")
            .arg(selectedCount).arg(pointCount));
        addSelected->setEnabled(selectedCount > 0);
    };
    for (auto* check : checks)
        connect(check, &QCheckBox::toggled, &dialog, updateSummary);
    connect(selectAll, &QPushButton::clicked, &dialog, [&checks] {
        for (auto* check : checks) check->setChecked(true);
    });
    connect(clearSelection, &QPushButton::clicked, &dialog, [&checks] {
        for (auto* check : checks) check->setChecked(false);
    });
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateSummary();
    if (dialog.exec() != QDialog::Accepted) return;

    analysis::FrequencyPlan plan{
        .mode = analysis::FrequencyPlanMode::Explicit,
        .pointsMHz = explicitFrequencies(),
        .ranges = {},
    };
    for (std::size_t index = 0; index < checks.size(); ++index) {
        if (checks[index]->isChecked()) plan.ranges.push_back({presets[index].startMHz,
            presets[index].endMHz, presets[index].stepMHz});
    }
    setExplicitFrequencies(analysis::frequencyPlanPoints(plan));
}

void OptimizationWorkspace::pasteExplicitFrequencies()
{
    bool accepted{};
    const auto text = QInputDialog::getMultiLineText(this, tr("Paste Frequencies"),
        tr("Enter MHz values separated by spaces, commas, semicolons, or new lines:"),
        {}, &accepted);
    if (!accepted) return;
    const auto values = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
        Qt::SkipEmptyParts);
    std::vector<double> frequenciesMHz;
    frequenciesMHz.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values) {
        bool valid{};
        const auto frequencyMHz = value.toDouble(&valid);
        if (valid && std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            frequenciesMHz.push_back(frequencyMHz);
    }
    setExplicitFrequencies(frequenciesMHz);
}

void OptimizationWorkspace::editFrequencySetup()
{
    const auto original = captureFrequencySelection();
    if (frequencyDialog_->exec() != QDialog::Accepted)
        restoreFrequencySelection(original);
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::editObjectiveSetup()
{
    const auto original = selectedObjective();
    if (objectiveDialog_->exec() != QDialog::Accepted)
        restoreObjectiveSetup(original);
    updateObjectiveControls();
}

void OptimizationWorkspace::restoreObjectiveSetup(
    const analysis::OptimizationObjectiveSpec& objective)
{
    const auto objectiveIndex = objectiveControl_->findData(
        static_cast<int>(objective.kind));
    if (objectiveIndex >= 0) objectiveControl_->setCurrentIndex(objectiveIndex);
    targetFrequencyControl_->setValue(objective.targetFrequencyMHz);
    referenceImpedanceControl_->setValue(objective.referenceImpedance);
    swrWeightControl_->setValue(objective.swrWeight);
    resistanceWeightControl_->setValue(objective.resistanceWeight);
    resistanceTargetControl_->setValue(objective.resistanceTargetOhms);
    reactanceWeightControl_->setValue(objective.reactanceWeight);
    reactanceTargetControl_->setValue(objective.reactanceTargetOhms);
    forwardGainWeightControl_->setValue(objective.forwardGainWeight);
    frontToBackWeightControl_->setValue(objective.frontToBackWeight);
    frontToRearWeightControl_->setValue(objective.frontToRearWeight);
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
    forwardThetaControl_->setValue(objective.forwardThetaDegrees);
    forwardPhiControl_->setValue(objective.forwardPhiDegrees);
    const auto componentIndex = radiationComponentControl_->findData(
        static_cast<int>(objective.radiationComponent));
    if (componentIndex >= 0) radiationComponentControl_->setCurrentIndex(componentIndex);
}

void OptimizationWorkspace::showCandidateDetails(int row)
{
    detailCandidateRow_ = row;
    updateCandidateDetails(row);
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
    constexpr auto directionalTab = 2;
    candidateDetailsTable_->setRowCount(0);
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()) {
        candidateDetailLabel_->setText(
            tr("Candidate details are unavailable."));
        candidateDetailPlots_->setResults({}, tr("this candidate"));
        candidateDirectionalPlots_->setResults({});
        candidateDetailViews_->setTabEnabled(directionalTab, false);
        candidateDetailViews_->setTabToolTip(directionalTab, tr(
            "Double-click a completed candidate that evaluated Gain, F/B, or F/R."));
        if (candidateDetailViews_->currentIndex() == directionalTab)
            candidateDetailViews_->setCurrentIndex(0);
        updateCandidateActionState();
        return;
    }

    const auto& candidate = candidates_[static_cast<std::size_t>(row)];
    auto feedpoints = candidate.feedpoints;
    std::ranges::sort(feedpoints, {}, &analysis::FeedpointResult::frequencyMHz);
    analysis::AnalysisResult candidateAnalysis;
    candidateAnalysis.feedpoints = feedpoints;
    candidateAnalysis.radiation = candidate.radiation;
    const auto evaluation = analysis::evaluateOptimizationObjective(
        candidateAnalysis, activeObjective_);
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
        if (evaluation && evaluation->evaluatedFrequencyCount == 1
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
    if (evaluation) {
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
            addCriterion(tr("Forward gain"), activeObjective_.forwardGainWeight,
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
    }
    candidateDetailLabel_->setText(tr(
        "Optimization Candidate — %1 · %2 frequencies · %3\n%4\n"
        "This is candidate data and does not represent the active model's official results.")
        .arg(candidateDescription(candidate))
        .arg(feedpoints.size())
        .arg(evaluation && evaluation->evaluatedFrequencyCount == 1
                ? tr("evaluated frequency bold") : tr("per-criterion frequency reductions"))
        .arg(breakdown.isEmpty() ? tr("Objective breakdown unavailable")
                                 : breakdown.join(tr(" · "))));
    analysis::AnalysisResult detailResult = std::move(candidateAnalysis);
    detailResult.referenceImpedanceOhms = activeObjective_.referenceImpedance;
    candidateDetailPlots_->setResults(detailResult,
        tr("candidate %1").arg(candidateDescription(candidate)));
    candidateDirectionalPlots_->setResults(evaluation.value_or(
        analysis::OptimizationObjectiveResult{}));
    const auto hasDirectionalMetrics = evaluation
        && std::ranges::any_of(evaluation->frequencyMetrics, [](const auto& metrics) {
            return metrics.forwardGainDb || metrics.frontToBackDb || metrics.frontToRearDb;
        });
    candidateDetailViews_->setTabEnabled(directionalTab, hasDirectionalMetrics);
    candidateDetailViews_->setTabToolTip(directionalTab, hasDirectionalMetrics
        ? tr("Forward Gain, F/B, and F/R across this candidate's evaluated frequencies.")
        : tr("Unavailable because this candidate did not evaluate a Gain, F/B, or F/R objective."));
    if (!hasDirectionalMetrics && candidateDetailViews_->currentIndex() == directionalTab)
        candidateDetailViews_->setCurrentIndex(0);
    if (evaluation && evaluation->evaluatedFrequencyCount == 1) {
        candidateDetailPlots_->setSelectedFrequency(
            evaluation->evaluationFrequencyMHz);
        candidateDirectionalPlots_->setSelectedFrequency(
            evaluation->evaluationFrequencyMHz);
    }
    updateCandidateActionState();
}

void OptimizationWorkspace::resetCandidateDetails()
{
    constexpr auto directionalTab = 2;
    detailCandidateRow_ = -1;
    candidateDetailsTable_->setRowCount(0);
    candidateDetailLabel_->setText(
        tr("Double-click a completed candidate to view its frequency table and plots."));
    candidateDetailPlots_->setResults({}, tr("a selected candidate"));
    candidateDirectionalPlots_->setResults({});
    candidateDetailViews_->setTabEnabled(directionalTab, false);
    candidateDetailViews_->setTabToolTip(directionalTab, tr(
        "Double-click a completed candidate that evaluated Gain, F/B, or F/R."));
    if (candidateDetailViews_->currentIndex() == directionalTab)
        candidateDetailViews_->setCurrentIndex(0);
    updateCandidateActionState();
}

void OptimizationWorkspace::updateCandidateActionState()
{
    const auto validCandidate = detailCandidateRow_ >= 0
        && static_cast<std::size_t>(detailCandidateRow_) < candidates_.size()
        && candidates_[static_cast<std::size_t>(detailCandidateRow_)].evaluation.has_value();
    const auto editable = validCandidate && !historicalSession_ && modelValid_ && !isRunning();
    applyCandidateButton_->setEnabled(editable && static_cast<bool>(applyParameterCallback_));
    applyCandidateAndRunButton_->setEnabled(editable && static_cast<bool>(applyAndRunCallback_));
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
        statusLabel_->setText(row == bestRow_
            ? tr("Applied the best parameter values to the active model.")
            : tr("Applied the selected candidate values to the active model."));
    }
}

void OptimizationWorkspace::updateCandidateRowToolTip(int row)
{
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()) return;
    const auto& candidate = candidates_[static_cast<std::size_t>(row)];
    const auto hasDetails = !candidate.feedpoints.empty();
    const auto* statusItem = resultsTable_->item(row, StatusColumn);
    const auto inProgress = statusItem != nullptr
        && (statusItem->text() == tr("Pending") || statusItem->text() == tr("Running"));
    auto toolTip = hasDetails
        ? tr("Double-click to view the frequency table, SWR, and impedance plots for this candidate.")
        : inProgress
            ? tr("Frequency details will be available after this candidate completes.")
            : tr("Frequency details are unavailable because this candidate did not complete with impedance results.");
    if (candidate.evaluation) {
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
    for (auto column = 0; column < resultsTable_->columnCount(); ++column) {
        if (auto* item = resultsTable_->item(row, column)) item->setToolTip(toolTip);
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

void OptimizationWorkspace::updateReadiness()
{
    const auto setup = currentStudySetup();
    const auto executable = QFileInfo(executable_);
    const auto hasFrequencies = !setup.frequenciesMHz.empty();
    const auto hasObjective = setup.objective.swrWeight + setup.objective.resistanceWeight
        + setup.objective.reactanceWeight + setup.objective.forwardGainWeight
        + setup.objective.frontToBackWeight + setup.objective.frontToRearWeight > 0.0;
    const auto directional = setup.objective.forwardGainWeight > 0.0
        || setup.objective.frontToBackWeight > 0.0
        || setup.objective.frontToRearWeight > 0.0;
    const auto hasVariables = !setup.variables.empty()
        && std::ranges::all_of(setup.variables, [](const auto& variable) {
            return std::isfinite(variable.minimum) && std::isfinite(variable.maximum)
                && std::isfinite(variable.tolerance) && variable.minimum < variable.maximum
                && variable.tolerance > 0.0;
        });
    const auto hasSearchBudget = setup.searchMethod == SearchMethod::DifferentialEvolution
        ? setup.populationSize >= 4 && setup.maximumGenerations >= 1
        : !isMultivariable(setup.searchMethod)
            || setup.candidateLimit >= static_cast<int>(setup.variables.size()) + 1;
    const auto ready = !historicalSession_ && modelValid_ && hasVariables
        && !externalRunActive_
        && hasFrequencies && hasObjective && hasSearchBudget
        && !isRunning() && analysis::isBackendRunnable(backend_.toStdString())
        && executable.exists() && executable.isFile() && executable.isExecutable();
    runButton_->setEnabled(ready);
    runButton_->setToolTip(!hasVariables
        ? tr("Select at least one parameter and enter valid bounds and tolerance.")
        : !hasSearchBudget
            ? setup.searchMethod == SearchMethod::DifferentialEvolution
                ? tr("Use a population of at least four and at least one generation.")
                : tr("Increase Maximum evaluations to at least the selected parameter count plus one.")
        : !hasObjective ? tr("Set at least one objective weight above zero.")
        : QString{});
    cancelButton_->setEnabled(isRunning());
    applyBestButton_->setEnabled(!historicalSession_ && modelValid_ && !isRunning()
        && bestRow_ >= 0 && static_cast<std::size_t>(bestRow_) < candidates_.size()
        && static_cast<bool>(applyParameterCallback_));
    updateCandidateActionState();
    const auto editable = !isRunning() && !historicalSession_;
    editFrequencyButton_->setEnabled(editable);
    editObjectiveButton_->setEnabled(editable);
    variablesTable_->setEnabled(editable);
    variableControl_->setEnabled(editable);
    frequencyModeControl_->setEnabled(editable);
    minimumControl_->setEnabled(editable);
    maximumControl_->setEnabled(editable);
    const auto multivariable = isMultivariable(selectedSearchMethod());
    searchMethodTabs_->setEnabled(editable);
    pointsControl_->setEnabled(editable && !multivariable);
    const auto differentialEvolution = selectedSearchMethod()
        == SearchMethod::DifferentialEvolution;
    adaptiveMaximumEvaluationsControl_->setEnabled(
        editable && multivariable && !differentialEvolution);
    adaptiveParameterToleranceControl_->setEnabled(editable && multivariable);
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
    forwardThetaControl_->setEnabled(editable && directional);
    forwardPhiControl_->setEnabled(editable && directional);
    radiationComponentControl_->setEnabled(editable && directional);
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
        statusLabel_->setText(tr("Select valid parameter bounds, frequencies, and an objective."));
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
    sessionRecord_->summary = isMultivariable(activeSearchMethod_)
        ? tr("%1 %2 · %3 · up to %4 candidates × %5 frequencies")
            .arg(searchMethodName(activeSearchMethod_))
            .arg(selectedSymbol_, objectiveName(activeObjective_.kind))
            .arg(candidateLimit).arg(activeFrequenciesMHz_.size())
        : tr("Parameter sweep %1 · %2 · %3 candidates × %4 frequencies")
            .arg(selectedSymbol_, objectiveName(activeObjective_.kind))
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
    const auto sessionMetadata = QJsonObject{
        {QStringLiteral("version"), 11},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("minimum"), setup.minimum},
        {QStringLiteral("maximum"), setup.maximum},
        {QStringLiteral("variables"), variableMetadata},
        {QStringLiteral("candidateCount"), candidateLimit},
        {QStringLiteral("searchMethod"), searchMethodId(activeSearchMethod_)},
        {QStringLiteral("parameterTolerance"), setup.parameterTolerance},
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
        {QStringLiteral("radiationComponent"),
            static_cast<int>(activeObjective_.radiationComponent)},
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
    auto* valueItem = new QTableWidgetItem(candidateDescription(candidates_.back()));
    valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    resultsTable_->setItem(row, ValueColumn, valueItem);
    resultsTable_->setItem(row, GainColumn, new QTableWidgetItem(QStringLiteral("—")));
    resultsTable_->setItem(row, FrontToBackColumn,
        new QTableWidgetItem(QStringLiteral("—")));
    resultsTable_->setItem(row, FrontToRearColumn,
        new QTableWidgetItem(QStringLiteral("—")));
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
    candidate.evaluation.reset();

    if (result.status == CandidateEvaluationStatus::Completed && result.objective) {
        const auto& evaluation = *result.objective;
        candidate.evaluation = evaluation;
        resultsTable_->setItem(candidate.row, ScoreColumn, numericItem(evaluation.score));
        resultsTable_->setItem(candidate.row, FrequencyColumn,
            evaluation.evaluatedFrequencyCount == 1
                ? numericItem(evaluation.evaluationFrequencyMHz)
                : new QTableWidgetItem(QStringLiteral("—")));
        if (evaluation.feedpoint) {
            resultsTable_->setItem(candidate.row, SwrColumn, numericItem(evaluation.swr));
            resultsTable_->setItem(candidate.row, ResistanceColumn,
                numericItem(evaluation.feedpoint->impedance.real()));
            resultsTable_->setItem(candidate.row, ReactanceColumn,
                numericItem(evaluation.feedpoint->impedance.imag()));
        }
        resultsTable_->setItem(candidate.row, GainColumn, evaluation.forwardGainDb
            ? numericItem(*evaluation.forwardGainDb)
            : new QTableWidgetItem(QStringLiteral("—")));
        resultsTable_->setItem(candidate.row, FrontToBackColumn, evaluation.frontToBackDb
            ? numericItem(*evaluation.frontToBackDb)
            : new QTableWidgetItem(QStringLiteral("—")));
        resultsTable_->setItem(candidate.row, FrontToRearColumn, evaluation.frontToRearDb
            ? numericItem(*evaluation.frontToRearDb)
            : new QTableWidgetItem(QStringLiteral("—")));
        setCandidateStatus(candidate.row, tr("Completed"));
        candidate.record.status = QStringLiteral("Completed");
        candidate.record.frequencyCount = result.frequencyCount;
        candidate.record.hasImpedance = !candidate.feedpoints.empty();
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
        case CandidateEvaluationStatus::NoImpedance:
            status = tr("Required objective results unavailable"); break;
        case CandidateEvaluationStatus::Completed: status = tr("Solver failed"); break;
        }
        setCandidateStatus(candidate.row, status);
        candidate.record.status = result.status == CandidateEvaluationStatus::Canceled
            ? QStringLiteral("Canceled")
            : result.status == CandidateEvaluationStatus::TimedOut
                ? QStringLiteral("Timed Out") : QStringLiteral("Failed");
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
        auto font = resultsTable_->item(bestRow_, ValueColumn)->font();
        font.setBold(true);
        for (auto column = 0; column < ResultColumnCount; ++column) {
            if (auto* item = resultsTable_->item(bestRow_, column)) item->setFont(font);
        }
        bestLabel_->setText(tr("Best candidate: %1, %2 = %3")
            .arg(candidateDescription(candidates_[static_cast<std::size_t>(bestRow_)]))
            .arg(objectiveName(activeObjective_.kind))
            .arg(bestScore_, 0, 'f', 3));
        resultsTable_->setCurrentCell(bestRow_, ValueColumn);
        resultsTable_->selectRow(bestRow_);
        resultsTable_->scrollToItem(resultsTable_->item(bestRow_, ValueColumn),
            QAbstractItemView::PositionAtCenter);
    } else {
        bestLabel_->setText(tr("No successful candidate produced impedance results."));
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
        {QStringLiteral("radiationComponent"),
            static_cast<int>(activeObjective_.radiationComponent)},
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

auto OptimizationWorkspace::selectedObjective() const -> analysis::OptimizationObjectiveSpec
{
    return {
        .kind = analysis::OptimizationObjectiveKind::PerCriterion,
        .referenceImpedance = referenceImpedanceControl_->value(),
        .targetFrequencyMHz = targetFrequencyControl_->value(),
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
        .forwardThetaDegrees = forwardThetaControl_->value(),
        .forwardPhiDegrees = forwardPhiControl_->value(),
        .radiationComponent = static_cast<analysis::RadiationComponent>(
            radiationComponentControl_->currentData().toInt()),
    };
}

auto OptimizationWorkspace::selectedFrequencyMode() const -> FrequencyMode
{
    return static_cast<FrequencyMode>(frequencyModeControl_->currentData().toInt());
}

auto OptimizationWorkspace::selectedSearchMethod() const -> SearchMethod
{
    switch (searchMethodTabs_->currentIndex()) {
    case 1: return SearchMethod::Adaptive;
    case 2: return SearchMethod::NelderMead;
    case 3: return SearchMethod::DifferentialEvolution;
    default: return SearchMethod::ParameterSweep;
    }
}

auto OptimizationWorkspace::selectedFrequencyPlan() const -> analysis::FrequencyPlan
{
    if (selectedObjective().kind == analysis::OptimizationObjectiveKind::SwrAtFrequency) {
        return {analysis::FrequencyPlanMode::Explicit,
            {targetFrequencyControl_->value()}, {}};
    }
    switch (selectedFrequencyMode()) {
    case FrequencyMode::Explicit:
        return {analysis::FrequencyPlanMode::Explicit, explicitFrequencies(), {}};
    case FrequencyMode::Continuous:
        return {analysis::FrequencyPlanMode::Explicit, {},
            {{continuousStartControl_->value(), continuousStopControl_->value(),
                continuousStepControl_->value()}}};
    case FrequencyMode::ModelSweep:
        return {analysis::FrequencyPlanMode::ModelSweep, modelFrequenciesMHz_, {}};
    }
    return {};
}

auto OptimizationWorkspace::explicitFrequencies() const -> std::vector<double>
{
    std::vector<double> frequencies;
    frequencies.reserve(static_cast<std::size_t>(frequencyTable_->count()));
    for (auto row = 0; row < frequencyTable_->count(); ++row) {
        const auto* item = frequencyTable_->item(row);
        if (item == nullptr) continue;
        bool valid{};
        const auto frequencyMHz = item->text().toDouble(&valid);
        if (valid && std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            frequencies.push_back(frequencyMHz);
    }
    return analysis::frequencyPlanPoints({
        analysis::FrequencyPlanMode::Explicit, std::move(frequencies), {}});
}

auto OptimizationWorkspace::currentStudySetup() const -> StudySetup
{
    const auto searchMethod = selectedSearchMethod();
    const auto differentialEvolution = searchMethod == SearchMethod::DifferentialEvolution;
    auto variables = isMultivariable(searchMethod)
        ? selectedAdaptiveVariables() : std::vector<StudySetup::VariableRange>{};
    if (searchMethod == SearchMethod::ParameterSweep && variableControl_->currentIndex() >= 0) {
        variables.push_back({variableControl_->currentText(),
            variableControl_->currentData().toDouble(), minimumControl_->value(),
            maximumControl_->value(), adaptiveParameterToleranceControl_->value()});
    }
    return {
        .searchMethod = searchMethod,
        .variable = variableControl_->currentText(),
        .minimum = minimumControl_->value(),
        .maximum = maximumControl_->value(),
        .candidateLimit = differentialEvolution
            ? differentialEvolutionPopulationControl_->value()
                * (differentialEvolutionGenerationControl_->value() + 1)
            : isMultivariable(searchMethod)
                ? adaptiveMaximumEvaluationsControl_->value() : pointsControl_->value(),
        .parameterTolerance = adaptiveParameterToleranceControl_->value(),
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

auto OptimizationWorkspace::captureFrequencySelection() const -> FrequencySelectionState
{
    return {
        .mode = selectedFrequencyMode(),
        .explicitFrequenciesMHz = explicitFrequencies(),
        .continuousStartMHz = continuousStartControl_->value(),
        .continuousStopMHz = continuousStopControl_->value(),
        .continuousStepMHz = continuousStepControl_->value(),
    };
}

void OptimizationWorkspace::restoreFrequencySelection(
    const FrequencySelectionState& state)
{
    setExplicitFrequencies(state.explicitFrequenciesMHz);
    continuousStartControl_->setValue(state.continuousStartMHz);
    continuousStopControl_->setValue(state.continuousStopMHz);
    continuousStepControl_->setValue(state.continuousStepMHz);
    const auto modeIndex = frequencyModeControl_->findData(static_cast<int>(state.mode));
    if (modeIndex >= 0) frequencyModeControl_->setCurrentIndex(modeIndex);
    updateFrequencyControls();
    updateWorkload();
    updateReadiness();
}

auto OptimizationWorkspace::objectiveName(analysis::OptimizationObjectiveKind kind) const -> QString
{
    switch (kind) {
    case analysis::OptimizationObjectiveKind::PerCriterion:
        return tr("Per-criterion score");
    case analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies:
        return tr("Minimax score");
    case analysis::OptimizationObjectiveKind::AverageAcrossFrequencies:
        return tr("Average score");
    case analysis::OptimizationObjectiveKind::SwrAtFrequency:
        return tr("Selected-frequency score");
    }
    return tr("Objective score");
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
