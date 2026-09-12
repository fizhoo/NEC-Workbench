#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/DisplayFormat.h"
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

auto writeFile(const QString& path, const QByteArray& data) -> bool
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
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
    variablesTable_->setColumnCount(4);
    variablesTable_->setHorizontalHeaderLabels(
        {tr("Symbol"), tr("Expression"), tr("Resolved Value"), tr("Line")});
    variablesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    variablesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    variablesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    variablesTable_->verticalHeader()->hide();
    variablesTable_->setTextElideMode(Qt::ElideRight);
    variablesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    variablesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    variablesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    variablesTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    variablesTable_->horizontalHeader()->resizeSection(0, 80);
    variablesTable_->horizontalHeader()->resizeSection(1, 130);
    variablesTable_->horizontalHeader()->resizeSection(2, 95);
    variablesTable_->horizontalHeader()->resizeSection(3, 45);

    variableControl_ = new QComboBox(this);
    variableControl_->setObjectName(QStringLiteral("optimizationVariableControl"));
    objectiveControl_ = new QComboBox(this);
    objectiveControl_->setObjectName(QStringLiteral("optimizationObjectiveControl"));
    objectiveControl_->addItem(tr("Minimax"),
        static_cast<int>(analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies));
    objectiveControl_->addItem(tr("Selected Frequency"),
        static_cast<int>(analysis::OptimizationObjectiveKind::SwrAtFrequency));
    objectiveControl_->setItemData(0, tr(
        "Minimax: Minimizes the worst-performing point across the band/pattern, rather than the average."),
        Qt::ToolTipRole);
    objectiveControl_->setItemData(1, tr(
        "Calculates and scores each candidate at only the selected frequency, ignoring the model FR sweep."),
        Qt::ToolTipRole);
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

    objectiveCriteriaTable_ = new QTableWidget(3, 3, this);
    objectiveCriteriaTable_->setObjectName(QStringLiteral("optimizationObjectiveCriteria"));
    objectiveCriteriaTable_->setHorizontalHeaderLabels(
        {tr("Criterion"), tr("Weight"), tr("Target")});
    objectiveCriteriaTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objectiveCriteriaTable_->setSelectionMode(QAbstractItemView::NoSelection);
    objectiveCriteriaTable_->verticalHeader()->hide();
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    objectiveCriteriaTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    const auto criterionItem = [this](int row, const QString& text, const QString& toolTip) {
        auto* item = new QTableWidgetItem(text);
        item->setFlags(Qt::ItemIsEnabled);
        item->setToolTip(toolTip);
        objectiveCriteriaTable_->setItem(row, 0, item);
    };
    criterionItem(0, tr("SWR"), tr("Minimize SWR. Set weight to zero to ignore it."));
    criterionItem(1, tr("Resistance (Ω)"),
        tr("Minimize distance from the resistance target."));
    criterionItem(2, tr("Reactance (Ω)"),
        tr("Minimize distance from the reactance target."));
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
    resistanceTargetControl_ = new FocusWheelDoubleSpinBox(objectiveCriteriaTable_);
    resistanceTargetControl_->setObjectName(QStringLiteral("optimizationResistanceTarget"));
    reactanceTargetControl_ = new FocusWheelDoubleSpinBox(objectiveCriteriaTable_);
    reactanceTargetControl_->setObjectName(QStringLiteral("optimizationReactanceTarget"));
    for (auto* control : {resistanceTargetControl_, reactanceTargetControl_}) {
        control->setRange(-1.0e9, 1.0e9);
        control->setDecimals(DisplayDecimalPlaces);
        control->setSuffix(QStringLiteral(" Ω"));
    }
    resistanceTargetControl_->setValue(50.0);
    reactanceTargetControl_->setValue(0.0);
    objectiveCriteriaTable_->setCellWidget(0, 1, swrWeightControl_);
    objectiveCriteriaTable_->setCellWidget(1, 1, resistanceWeightControl_);
    objectiveCriteriaTable_->setCellWidget(2, 1, reactanceWeightControl_);
    auto* swrTarget = new QTableWidgetItem(tr("1:1 (ideal)"));
    swrTarget->setFlags(Qt::ItemIsEnabled);
    objectiveCriteriaTable_->setItem(0, 2, swrTarget);
    objectiveCriteriaTable_->setCellWidget(1, 2, resistanceTargetControl_);
    objectiveCriteriaTable_->setCellWidget(2, 2, reactanceTargetControl_);
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

    adaptiveMaximumEvaluationsControl_ = new FocusWheelSpinBox(this);
    adaptiveMaximumEvaluationsControl_->setObjectName(
        QStringLiteral("optimizationAdaptiveMaximumEvaluations"));
    adaptiveMaximumEvaluationsControl_->setRange(5, 101);
    adaptiveMaximumEvaluationsControl_->setValue(21);
    adaptiveMaximumEvaluationsControl_->setToolTip(tr(
        "Maximum solver candidates, including the initial five-point search."));
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
    auto* parameterSettings = new QWidget(variableSection);
    parameterSettings->setObjectName(QStringLiteral("optimizationParameterSettings"));
    auto* variableSectionLayout = new QVBoxLayout(variableSection);
    variableSectionLayout->setContentsMargins(6, 8, 6, 6);
    variableSectionLayout->addWidget(parameterSettings);
    auto* parameterGrid = new QGridLayout(parameterSettings);
    parameterGrid->setContentsMargins(0, 0, 0, 0);
    parameterGrid->setHorizontalSpacing(6);
    parameterGrid->setVerticalSpacing(4);
    const auto gridLabel = [parameterSettings](const QString& text,
                               const QString& objectName = {}) {
        auto* label = new QLabel(text, parameterSettings);
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
    searchBudgetLabel_ = new QLabel(tr("Candidate count"), parameterSettings);
    searchBudgetLabel_->setObjectName(QStringLiteral("optimizationSearchBudgetLabel"));
    searchBudgetLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    parameterGrid->addWidget(gridLabel(tr("Variable")), 0, 0);
    parameterGrid->addWidget(variableControl_, 0, 1);
    parameterGrid->addWidget(gridLabel(tr("Minimum")), 1, 0);
    parameterGrid->addWidget(minimumControl_, 1, 1);
    parameterGrid->addWidget(gridLabel(tr("Maximum")), 2, 0);
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

    auto* resetSearchDefaultsButton = new QPushButton(tr("Reset Search Defaults"), parameterSettings);
    resetSearchDefaultsButton->setObjectName(
        QStringLiteral("optimizationResetSearchDefaults"));
    resetSearchDefaultsButton->setToolTip(tr(
        "Restore the default candidate count or adaptive search limits without changing the variable, range, frequencies, or objective."));
    parameterGrid->addWidget(resetSearchDefaultsButton, 6, 0, 1, 2, Qt::AlignRight);
    parameterGrid->setColumnStretch(1, 1);

    connect(resetSearchDefaultsButton, &QPushButton::clicked, this, [this] {
        if (selectedSearchMethod() == SearchMethod::Adaptive) {
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
    objectiveDialog_->resize(680, 340);
    auto* objectiveDialogLayout = new QVBoxLayout(objectiveDialog_);
    auto* objectiveSettings = new QWidget(objectiveDialog_);
    objectiveSettings->setObjectName(QStringLiteral("optimizationObjectiveSettings"));
    auto* objectiveForm = new QFormLayout(objectiveSettings);
    objectiveForm->setContentsMargins(0, 0, 0, 0);
    objectiveForm->addRow(tr("Evaluation"), objectiveControl_);
    objectiveForm->addRow(tr("Selected frequency"), targetFrequencyControl_);
    objectiveForm->addRow(tr("Reference impedance"), referenceImpedanceControl_);
    objectiveForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    objectiveDialogLayout->addWidget(objectiveSettings);
    objectiveDialogLayout->addWidget(objectiveCriteriaTable_);
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
        tr("R (Ω)"), tr("X (Ω)"), tr("Status"), tr("Run")});
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

    candidateDetailsWindow_ = new QDialog(this, Qt::Window);
    candidateDetailsWindow_->setObjectName(
        QStringLiteral("optimizationCandidateDetailsWindow"));
    candidateDetailsWindow_->setWindowTitle(tr("Candidate Frequency Results"));
    candidateDetailsWindow_->setModal(false);
    candidateDetailsWindow_->resize(820, 600);
    auto* detailLayout = new QVBoxLayout(candidateDetailsWindow_);
    candidateDetailLabel_ = new QLabel(candidateDetailsWindow_);
    candidateDetailLabel_->setObjectName(QStringLiteral("optimizationCandidateDetailLabel"));
    candidateDetailLabel_->setWordWrap(true);
    candidateDetailsTable_ = new QTableWidget(candidateDetailsWindow_);
    candidateDetailsTable_->setObjectName(QStringLiteral("optimizationCandidateDetails"));
    candidateDetailsTable_->setColumnCount(4);
    candidateDetailsTable_->setHorizontalHeaderLabels(
        {tr("Frequency (MHz)"), tr("SWR"), tr("R (Ω)"), tr("X (Ω)")});
    candidateDetailsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    candidateDetailsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    candidateDetailsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    candidateDetailsTable_->verticalHeader()->hide();
    candidateDetailsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    candidateDetailPlots_ = new SweepPlotsView(candidateDetailsWindow_);
    candidateDetailPlots_->setObjectName(
        QStringLiteral("optimizationCandidateDetailPlots"));
    auto* candidateDetailViews = new QTabWidget(candidateDetailsWindow_);
    candidateDetailViews->setObjectName(
        QStringLiteral("optimizationCandidateDetailViews"));
    candidateDetailViews->setDocumentMode(true);
    candidateDetailViews->addTab(candidateDetailsTable_, tr("Frequency Table"));
    candidateDetailViews->addTab(candidateDetailPlots_, tr("SWR & Impedance Plots"));
    detailLayout->addWidget(candidateDetailLabel_);
    detailLayout->addWidget(candidateDetailViews, 1);
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
    connect(objectiveControl_, &QComboBox::currentIndexChanged,
        this, [this] { updateObjectiveControls(); });
    connect(targetFrequencyControl_, &QDoubleSpinBox::valueChanged, this, [this] {
        updateWorkload();
        updateReadiness();
    });
    for (auto* control : {swrWeightControl_, resistanceWeightControl_, resistanceTargetControl_,
             reactanceWeightControl_, reactanceTargetControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
            updateWorkload();
            updateReadiness();
        });
    }
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
        const auto* item = variablesTable_->item(row, 0);
        const auto index = item == nullptr ? -1 : variableControl_->findText(item->text());
        if (index >= 0) variableControl_->setCurrentIndex(index);
    });
    connect(runButton_, &QPushButton::clicked, this, [this] { startSweep(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelSweep(); });
    connect(applyBestButton_, &QPushButton::clicked, this, [this] {
        if (bestRow_ < 0 || static_cast<std::size_t>(bestRow_) >= candidates_.size()
            || !applyParameterCallback_) return;
        const auto symbol = selectedSymbol_;
        const auto value = candidates_[static_cast<std::size_t>(bestRow_)].value;
        if (applyParameterCallback_(symbol, value))
            statusLabel_->setText(tr("Applied %1 = %2 to the active model.")
                .arg(symbol).arg(value, 0, 'g', 15));
    });
    connect(evaluator_, &CandidateEvaluator::finished, this,
        [this](CandidateEvaluationResult result) {
            finishCurrentCandidate(std::move(result));
        });
    connect(returnToCurrentWorkButton_, &QPushButton::clicked, this, [this] {
        if (returnToCurrentWorkCallback_) returnToCurrentWorkCallback_();
    });
    updateObjectiveControls();
    updateFrequencyControls();
    updateSearchMethodControls();
}

void OptimizationWorkspace::setContext(QString source, QString sourceFile, QString backend,
    QString executable, int timeoutSeconds, bool modelValid)
{
    if (isRunning() || historicalSession_) return;
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
    candidates_.clear();
    candidateIndex_ = 0;
    bestScore_ = std::numeric_limits<double>::infinity();
    bestRow_ = -1;
    resultsTable_->setRowCount(0);
    candidatePlots_->clear();
    bestLabel_->setText(tr("No optimization results yet."));
    resetCandidateDetails();
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

void OptimizationWorkspace::setApplyParameterCallback(
    std::function<bool(QString, double)> callback)
{
    applyParameterCallback_ = std::move(callback);
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
    const auto adaptiveSession = sessionMetadata.value(
        QStringLiteral("searchMethod")).toString() == QStringLiteral("adaptive");
    adaptiveStopReason_ = sessionMetadata.value(
        QStringLiteral("stopDescription")).toString();
    searchMethodTabs_->setCurrentIndex(adaptiveSession ? 1 : 0);
    activeSearchMethod_ = adaptiveSession ? SearchMethod::Adaptive : SearchMethod::ParameterSweep;
    adaptiveMaximumEvaluationsControl_->setValue(sessionMetadata.value(
        QStringLiteral("candidateCount")).toInt(21));
    adaptiveParameterToleranceControl_->setValue(sessionMetadata.value(
        QStringLiteral("parameterTolerance")).toDouble(0.010));
    adaptiveScoreToleranceControl_->setValue(sessionMetadata.value(
        QStringLiteral("scoreTolerance")).toDouble(0.001));
    selectedSymbol_ = sessionMetadata.value(QStringLiteral("variable")).toString();
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
        QFile metadataFile(QDir(candidateRecords[index].directory).filePath(
            QStringLiteral("optimization.json")));
        const auto metadata = metadataFile.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(metadataFile.readAll()).object() : QJsonObject{};
        const auto value = metadata.value(QStringLiteral("value")).toDouble();
        if (selectedSymbol_.isEmpty()) selectedSymbol_ = metadata.value(QStringLiteral("variable")).toString();
        selectedValueSuffix_ = metadata.value(QStringLiteral("unit")).toString();
        if (!selectedValueSuffix_.isEmpty()) selectedValueSuffix_.prepend(' ');
        resultsTable_->setItem(row, ValueColumn, numericItem(value));
        QFile outputFile(QDir(candidateRecords[index].directory).filePath(QStringLiteral("model.out")));
        analysis::AnalysisResult result;
        if (outputFile.open(QIODevice::ReadOnly))
            result = analysis::NecOutputParser{}.parse(outputFile.readAll().toStdString());
        const auto objectiveId = metadata.value(QStringLiteral("objective")).toString();
        restoredObjective = {
            .kind = objectiveId == QStringLiteral("minimize-swr-at-frequency")
                ? analysis::OptimizationObjectiveKind::SwrAtFrequency
                : analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies,
            .referenceImpedance = metadata.value(
                QStringLiteral("referenceImpedance")).toDouble(50.0),
            .targetFrequencyMHz = metadata.value(
                QStringLiteral("targetFrequencyMHz")).toDouble(),
            .swrWeight = metadata.value(QStringLiteral("swrWeight")).toDouble(1.0),
            .resistanceWeight = metadata.value(
                QStringLiteral("resistanceWeight")).toDouble(),
            .resistanceTargetOhms = metadata.value(
                QStringLiteral("resistanceTargetOhms")).toDouble(
                    metadata.value(QStringLiteral("referenceImpedance")).toDouble(50.0)),
            .reactanceWeight = metadata.value(
                QStringLiteral("reactanceWeight")).toDouble(),
            .reactanceTargetOhms = metadata.value(
                QStringLiteral("reactanceTargetOhms")).toDouble(),
        };
        const auto evaluation = analysis::evaluateOptimizationObjective(
            result.feedpoints, restoredObjective);
        if (evaluation && evaluation->feedpoint) {
            resultsTable_->setItem(row, ScoreColumn, numericItem(evaluation->score));
            resultsTable_->setItem(row, SwrColumn, numericItem(evaluation->swr));
            resultsTable_->setItem(row, FrequencyColumn,
                numericItem(evaluation->feedpoint->frequencyMHz));
            resultsTable_->setItem(row, ResistanceColumn,
                numericItem(evaluation->feedpoint->impedance.real()));
            resultsTable_->setItem(row, ReactanceColumn,
                numericItem(evaluation->feedpoint->impedance.imag()));
            if (evaluation->score < bestScore_) {
                bestScore_ = evaluation->score;
                bestRow_ = row;
            }
        }
        candidates_.push_back(Candidate{
            value, row, candidateRecords[index], result.feedpoints, evaluation,
            metadata.value(QStringLiteral("refinementRound")).toInt(),
            metadata.value(QStringLiteral("trialRole")).toString()});
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
    statusLabel_->setText(adaptiveStopReason_.isEmpty()
        ? tr("Historical optimization session · %1").arg(session->status)
        : tr("Historical optimization session · %1 · %2")
            .arg(session->status, adaptiveStopReason_));
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
        variablesTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(definition.name)));
        auto* expressionItem = new QTableWidgetItem(
            QString::fromStdString(definition.expression));
        expressionItem->setToolTip(tr("Full expression: %1").arg(expressionItem->text()));
        variablesTable_->setItem(row, 1, expressionItem);
        auto* valueItem = numericItem(definition.value);
        valueItem->setToolTip(tr(
            "Resolved numeric SY value. No physical unit is inferred from where the symbol is used."));
        variablesTable_->setItem(row, 2, valueItem);
        variablesTable_->setItem(row, 3,
            new QTableWidgetItem(QString::number(definition.lineNumber)));
        variableControl_->addItem(QString::fromStdString(definition.name), definition.value);
    }
    statusLabel_->setText(resolution.ok()
        ? definitions_.empty() ? tr("Add SY declarations to enable parameter optimization.")
            : tr("Choose one resolved symbol and a bounded range.")
        : tr("Resolve the model's SY expression errors before optimizing."));
    updateBounds();
}

void OptimizationWorkspace::updateBounds()
{
    if (variableControl_->currentIndex() < 0) return;
    const auto value = variableControl_->currentData().toDouble();
    selectedValueSuffix_.clear();
    minimumControl_->setSuffix({});
    maximumControl_->setSuffix({});
    resultsTable_->horizontalHeaderItem(ValueColumn)->setText(tr("Value"));
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
    const auto singleFrequency = objective.kind
        == analysis::OptimizationObjectiveKind::SwrAtFrequency;
    targetFrequencyControl_->setEnabled(
        !isRunning() && !historicalSession_ && singleFrequency);
    objectiveControl_->setToolTip(objectiveControl_->currentData(Qt::ToolTipRole).toString());
    frequencyModeControl_->setToolTip(singleFrequency
        ? tr("The selected-frequency objective runs only the objective frequency.")
        : tr("Choose which frequencies are calculated for every candidate."));
    resultsTable_->horizontalHeaderItem(ScoreColumn)->setText(tr("Objective Score"));
    updateFrequencyControls();
}

void OptimizationWorkspace::updateFrequencyControls()
{
    const auto singleFrequency = selectedObjective().kind
        == analysis::OptimizationObjectiveKind::SwrAtFrequency;
    const auto explicitMode = !singleFrequency
        && selectedFrequencyMode() == FrequencyMode::Explicit;
    const auto continuousMode = !singleFrequency
        && selectedFrequencyMode() == FrequencyMode::Continuous;
    explicitFrequencyPanel_->setVisible(explicitMode);
    continuousFrequencyPanel_->setVisible(continuousMode);
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::updateSearchMethodControls()
{
    const auto adaptive = selectedSearchMethod() == SearchMethod::Adaptive;
    searchBudgetLabel_->setText(adaptive ? tr("Maximum evaluations") : tr("Candidate count"));
    pointsControl_->setVisible(!adaptive);
    adaptiveMaximumEvaluationsControl_->setVisible(adaptive);
    adaptiveParameterToleranceLabel_->setVisible(adaptive);
    adaptiveParameterToleranceControl_->setVisible(adaptive);
    adaptiveScoreToleranceLabel_->setVisible(adaptive);
    adaptiveScoreToleranceControl_->setVisible(adaptive);
    pointsControl_->setEnabled(!adaptive && !isRunning() && !historicalSession_);
    adaptiveMaximumEvaluationsControl_->setEnabled(adaptive && !isRunning()
        && !historicalSession_);
    adaptiveParameterToleranceControl_->setEnabled(adaptive && !isRunning()
        && !historicalSession_);
    adaptiveScoreToleranceControl_->setEnabled(adaptive && !isRunning()
        && !historicalSession_);
    if (!historicalSession_)
        runButton_->setText(adaptive ? tr("Run Adaptive Optimize") : tr("Run Parameter Sweep"));
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::updateSetupSummaries()
{
    const auto setup = currentStudySetup();
    const auto& frequencies = setup.frequenciesMHz;
    QString frequencySource;
    if (setup.objective.kind == analysis::OptimizationObjectiveKind::SwrAtFrequency) {
        frequencySource = tr("Selected objective frequency");
    } else {
        switch (setup.frequencySelection.mode) {
        case FrequencyMode::ModelSweep: frequencySource = tr("Model FR sweep"); break;
        case FrequencyMode::Explicit: frequencySource = tr("Selected frequencies"); break;
        case FrequencyMode::Continuous: frequencySource = tr("Custom continuous sweep"); break;
        }
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
    if (setup.objective.swrWeight > 0.0)
        criteria.append(tr("SWR %1").arg(formatDecimal(setup.objective.swrWeight)));
    if (setup.objective.resistanceWeight > 0.0)
        criteria.append(tr("R %1 → %2 Ω")
            .arg(formatDecimal(setup.objective.resistanceWeight))
            .arg(formatDecimal(setup.objective.resistanceTargetOhms)));
    if (setup.objective.reactanceWeight > 0.0)
        criteria.append(tr("X %1 → %2 Ω")
            .arg(formatDecimal(setup.objective.reactanceWeight))
            .arg(formatDecimal(setup.objective.reactanceTargetOhms)));
    objectiveSummaryLabel_->setText(tr("%1 · Reference Z %2 Ω\n%3")
        .arg(objectiveControl_->currentText())
        .arg(formatDecimal(setup.objective.referenceImpedance))
        .arg(criteria.isEmpty() ? tr("No enabled criteria") : criteria.join(tr(" · "))));
}

void OptimizationWorkspace::updateWorkload()
{
    const auto setup = currentStudySetup();
    const auto frequencyCount = setup.frequenciesMHz.size();
    const auto adaptive = setup.searchMethod == SearchMethod::Adaptive;
    const auto candidateCount = setup.candidateLimit;
    workloadLabel_->setText(adaptive
        ? tr("Up to %1 candidates × %2 %3 = up to %4 calculated points")
            .arg(candidateCount).arg(frequencyCount)
            .arg(frequencyCount == 1 ? tr("frequency") : tr("frequencies"))
            .arg(static_cast<qulonglong>(candidateCount * frequencyCount))
        : tr("%1 candidates × %2 %3 = %4 calculated points")
            .arg(candidateCount).arg(frequencyCount)
            .arg(frequencyCount == 1 ? tr("frequency") : tr("frequencies"))
            .arg(static_cast<qulonglong>(candidateCount * frequencyCount)));
    studySummaryLabel_->setText(tr("Variable: %1  |  Range: %2 to %3  |  Frequencies: %4  |  Goal: %5")
        .arg(setup.variable.isEmpty() ? tr("None") : setup.variable)
        .arg(formatDecimal(setup.minimum))
        .arg(formatDecimal(setup.maximum))
        .arg(frequencyCount)
        .arg(objectiveControl_->currentText()));
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
}

void OptimizationWorkspace::showCandidateDetails(int row)
{
    detailCandidateRow_ = row;
    updateCandidateDetails(row);
    candidateDetailsWindow_->show();
    candidateDetailsWindow_->raise();
    candidateDetailsWindow_->activateWindow();
}

void OptimizationWorkspace::updateCandidateDetails(int row)
{
    candidateDetailsTable_->setRowCount(0);
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()
        || candidates_[static_cast<std::size_t>(row)].feedpoints.empty()) {
        candidateDetailLabel_->setText(
            tr("Frequency details are unavailable because this candidate did not complete with impedance results."));
        candidateDetailPlots_->setResults({}, tr("this candidate"));
        return;
    }

    const auto& candidate = candidates_[static_cast<std::size_t>(row)];
    auto feedpoints = candidate.feedpoints;
    std::ranges::sort(feedpoints, {}, &analysis::FeedpointResult::frequencyMHz);
    const auto evaluation = analysis::evaluateOptimizationObjective(feedpoints, activeObjective_);
    candidateDetailsTable_->setRowCount(static_cast<int>(feedpoints.size()));
    for (std::size_t index = 0; index < feedpoints.size(); ++index) {
        const auto& feedpoint = feedpoints[index];
        const auto detailRow = static_cast<int>(index);
        candidateDetailsTable_->setItem(detailRow, 0, numericItem(feedpoint.frequencyMHz));
        candidateDetailsTable_->setItem(detailRow, 1, numericItem(
            analysis::standingWaveRatio(feedpoint.impedance, activeObjective_.referenceImpedance)));
        candidateDetailsTable_->setItem(detailRow, 2, numericItem(feedpoint.impedance.real()));
        candidateDetailsTable_->setItem(detailRow, 3, numericItem(feedpoint.impedance.imag()));
        if (evaluation && evaluation->feedpoint
            && std::abs(evaluation->feedpoint->frequencyMHz - feedpoint.frequencyMHz) < 1.0e-9) {
            for (auto column = 0; column < candidateDetailsTable_->columnCount(); ++column) {
                auto* item = candidateDetailsTable_->item(detailRow, column);
                auto font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
        }
    }
    candidateDetailLabel_->setText(tr("Candidate %1 = %2%3 · %4 frequencies · objective point bold")
        .arg(selectedSymbol_.isEmpty() ? tr("value") : selectedSymbol_)
        .arg(candidate.value, 0, 'f', 3)
        .arg(selectedValueSuffix_)
        .arg(feedpoints.size()));
    analysis::AnalysisResult detailResult;
    detailResult.feedpoints = feedpoints;
    detailResult.referenceImpedanceOhms = activeObjective_.referenceImpedance;
    candidateDetailPlots_->setResults(detailResult,
        tr("candidate %1 = %2%3")
            .arg(selectedSymbol_.isEmpty() ? tr("value") : selectedSymbol_)
            .arg(candidate.value, 0, 'f', 3)
            .arg(selectedValueSuffix_));
    if (evaluation && evaluation->feedpoint)
        candidateDetailPlots_->setSelectedFrequency(
            evaluation->feedpoint->frequencyMHz);
}

void OptimizationWorkspace::resetCandidateDetails()
{
    detailCandidateRow_ = -1;
    candidateDetailsTable_->setRowCount(0);
    candidateDetailLabel_->setText(
        tr("Double-click a completed candidate to view its frequency table and plots."));
    candidateDetailPlots_->setResults({}, tr("a selected candidate"));
}

void OptimizationWorkspace::updateCandidateRowToolTip(int row)
{
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()) return;
    const auto hasDetails = !candidates_[static_cast<std::size_t>(row)].feedpoints.empty();
    const auto* statusItem = resultsTable_->item(row, StatusColumn);
    const auto inProgress = statusItem != nullptr
        && (statusItem->text() == tr("Pending") || statusItem->text() == tr("Running"));
    const auto toolTip = hasDetails
        ? tr("Double-click to view the frequency table, SWR, and impedance plots for this candidate.")
        : inProgress
            ? tr("Frequency details will be available after this candidate completes.")
            : tr("Frequency details are unavailable because this candidate did not complete with impedance results.");
    for (auto column = 0; column < resultsTable_->columnCount(); ++column) {
        if (auto* item = resultsTable_->item(row, column)) item->setToolTip(toolTip);
    }
}

void OptimizationWorkspace::updateCandidatePlots()
{
    std::vector<CandidatePlotPoint> points;
    points.reserve(candidates_.size());
    for (const auto& candidate : candidates_) {
        if (!candidate.evaluation) continue;
        points.push_back({candidate.row, candidate.value, *candidate.evaluation});
    }
    candidatePlots_->setCandidates(selectedSymbol_, selectedValueSuffix_, points, bestRow_);
}

void OptimizationWorkspace::updateReadiness()
{
    const auto setup = currentStudySetup();
    const auto executable = QFileInfo(executable_);
    const auto hasFrequencies = !setup.frequenciesMHz.empty();
    const auto hasObjective = setup.objective.swrWeight + setup.objective.resistanceWeight
        + setup.objective.reactanceWeight > 0.0;
    const auto ready = !historicalSession_ && modelValid_ && variableControl_->count() > 0
        && !externalRunActive_
        && hasFrequencies && hasObjective
        && !isRunning() && analysis::isBackendRunnable(backend_.toStdString())
        && executable.exists() && executable.isFile() && executable.isExecutable();
    runButton_->setEnabled(ready);
    runButton_->setToolTip(hasObjective ? QString{}
        : tr("Set at least one objective weight above zero."));
    cancelButton_->setEnabled(isRunning());
    applyBestButton_->setEnabled(!historicalSession_ && modelValid_ && !isRunning()
        && bestRow_ >= 0 && static_cast<std::size_t>(bestRow_) < candidates_.size()
        && static_cast<bool>(applyParameterCallback_));
    const auto editable = !isRunning() && !historicalSession_;
    editFrequencyButton_->setEnabled(editable);
    editObjectiveButton_->setEnabled(editable);
    variablesTable_->setEnabled(editable);
    variableControl_->setEnabled(editable);
    objectiveControl_->setEnabled(editable);
    const auto singleFrequency = selectedObjective().kind
        == analysis::OptimizationObjectiveKind::SwrAtFrequency;
    frequencyModeControl_->setEnabled(editable && !singleFrequency);
    minimumControl_->setEnabled(editable);
    maximumControl_->setEnabled(editable);
    const auto adaptive = selectedSearchMethod() == SearchMethod::Adaptive;
    searchMethodTabs_->setEnabled(editable);
    pointsControl_->setEnabled(editable && !adaptive);
    adaptiveMaximumEvaluationsControl_->setEnabled(editable && adaptive);
    adaptiveParameterToleranceControl_->setEnabled(editable && adaptive);
    adaptiveScoreToleranceControl_->setEnabled(editable && adaptive);
    referenceImpedanceControl_->setEnabled(editable);
    for (auto* control : {swrWeightControl_, resistanceWeightControl_, resistanceTargetControl_,
             reactanceWeightControl_, reactanceTargetControl_}) control->setEnabled(editable);
    targetFrequencyControl_->setEnabled(editable
        && selectedObjective().kind == analysis::OptimizationObjectiveKind::SwrAtFrequency);
    const auto explicitMode = !singleFrequency
        && selectedFrequencyMode() == FrequencyMode::Explicit;
    frequencyTable_->setEnabled(editable && explicitMode);
    frequencyEntryControl_->setEnabled(editable && explicitMode);
    addFrequencyButton_->setEnabled(editable && explicitMode);
    removeFrequencyButton_->setEnabled(editable && explicitMode);
    pasteFrequencyButton_->setEnabled(editable && explicitMode);
    clearFrequencyButton_->setEnabled(
        editable && explicitMode && frequencyTable_->count() > 0);
    addAmateurBandButton_->setEnabled(editable && explicitMode);
    const auto continuousMode = !singleFrequency
        && selectedFrequencyMode() == FrequencyMode::Continuous;
    continuousStartControl_->setEnabled(editable && continuousMode);
    continuousStopControl_->setEnabled(editable && continuousMode);
    continuousStepControl_->setEnabled(editable && continuousMode);
}

void OptimizationWorkspace::startSweep()
{
    if (!runButton_->isEnabled() || minimumControl_->value() >= maximumControl_->value()) {
        statusLabel_->setText(tr("Minimum must be less than maximum."));
        return;
    }
    const auto setup = currentStudySetup();
    selectedSymbol_ = setup.variable;
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
    sessionRecord_->summary = activeSearchMethod_ == SearchMethod::Adaptive
        ? tr("Adaptive optimize %1 · %2 · up to %3 candidates × %4 frequencies")
            .arg(selectedSymbol_, objectiveName(activeObjective_.kind))
            .arg(candidateLimit).arg(activeFrequenciesMHz_.size())
        : tr("Parameter sweep %1 · %2 · %3 candidates × %4 frequencies")
            .arg(selectedSymbol_, objectiveName(activeObjective_.kind))
            .arg(candidateLimit).arg(activeFrequenciesMHz_.size());
    runStore_.save(*sessionRecord_);
    const auto sessionMetadata = QJsonObject{
        {QStringLiteral("version"), 4},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("minimum"), setup.minimum},
        {QStringLiteral("maximum"), setup.maximum},
        {QStringLiteral("candidateCount"), candidateLimit},
        {QStringLiteral("searchMethod"), activeSearchMethod_ == SearchMethod::Adaptive
            ? QStringLiteral("adaptive") : QStringLiteral("parameter-sweep")},
        {QStringLiteral("parameterTolerance"), setup.parameterTolerance},
        {QStringLiteral("scoreTolerance"), setup.scoreTolerance},
        {QStringLiteral("objective"), activeObjective_.kind
            == analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies
                ? QStringLiteral("worst-point-across-frequencies")
                : QStringLiteral("minimize-swr-at-frequency")},
        {QStringLiteral("referenceImpedance"), activeObjective_.referenceImpedance},
        {QStringLiteral("targetFrequencyMHz"), activeObjective_.targetFrequencyMHz},
        {QStringLiteral("swrWeight"), activeObjective_.swrWeight},
        {QStringLiteral("resistanceWeight"), activeObjective_.resistanceWeight},
        {QStringLiteral("resistanceTargetOhms"), activeObjective_.resistanceTargetOhms},
        {QStringLiteral("reactanceWeight"), activeObjective_.reactanceWeight},
        {QStringLiteral("reactanceTargetOhms"), activeObjective_.reactanceTargetOhms},
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
    adaptiveSearch_.reset();
    if (activeSearchMethod_ == SearchMethod::Adaptive) {
        adaptiveSearch_.emplace(analysis::AdaptiveSearchSettings{
            minimum, maximum, candidateLimit,
            setup.parameterTolerance, setup.scoreTolerance});
        for (const auto value : adaptiveSearch_->initialCandidates())
            appendCandidate(value, 0, QStringLiteral("initial"));
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
    adaptiveStopReason_.clear();
    cancelRequested_ = false;
    progress_->setRange(0, candidateLimit);
    progress_->setValue(0);
    progress_->setFormat(activeSearchMethod_ == SearchMethod::Adaptive
        ? tr("%v of up to %m evaluations") : tr("%v / %m candidates (%p%)"));
    bestLabel_->setText(activeSearchMethod_ == SearchMethod::Adaptive
        ? tr("Adaptive optimization in progress…") : tr("Sweep in progress…"));
    statusLabel_->setText(tr("Running %1 with %2 initial candidates × %3 %4 for %5.")
        .arg(activeSearchMethod_ == SearchMethod::Adaptive
            ? tr("adaptive optimization") : tr("parameter sweep"))
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
    const auto row = resultsTable_->rowCount();
    resultsTable_->insertRow(row);
    candidates_.push_back(Candidate{
        value, row, {}, {}, {}, refinementRound, std::move(trialRole)});
    auto* valueItem = numericItem(value);
    valueItem->setText(valueItem->text() + selectedValueSuffix_);
    resultsTable_->setItem(row, ValueColumn, valueItem);
    setCandidateStatus(row, tr("Pending"));
}

auto OptimizationWorkspace::prepareAdaptiveRound() -> bool
{
    if (!adaptiveSearch_) return false;
    const auto proposals = adaptiveSearch_->nextCandidates();
    const auto round = adaptiveSearch_->refinementRound();
    const auto bestValue = bestRow_ >= 0
        ? candidates_[static_cast<std::size_t>(bestRow_)].value : 0.0;
    for (const auto proposal : proposals) {
        appendCandidate(proposal, round,
            proposal < bestValue ? QStringLiteral("left") : QStringLiteral("right"));
    }
    if (!proposals.empty()) return true;
    switch (adaptiveSearch_->stopReason()) {
    case analysis::AdaptiveStopReason::MaximumEvaluations:
        adaptiveStopReason_ = tr("the maximum evaluation limit was reached");
        break;
    case analysis::AdaptiveStopReason::ParameterTolerance:
        adaptiveStopReason_ = tr(
            "no new midpoint could be placed at least %1 from the current best parameter value")
            .arg(adaptiveParameterToleranceControl_->value(), 0, 'f', DisplayDecimalPlaces);
        break;
    case analysis::AdaptiveStopReason::ScoreTolerance:
        adaptiveStopReason_ = tr(
            "two consecutive refinement rounds improved the best objective score by no more than %1")
            .arg(adaptiveScoreToleranceControl_->value(), 0, 'f', DisplayDecimalPlaces);
        break;
    case analysis::AdaptiveStopReason::NoSuccessfulCandidate:
        adaptiveStopReason_ = tr("no successful candidate was available to refine");
        break;
    case analysis::AdaptiveStopReason::None:
        adaptiveStopReason_ = tr("search completed");
        break;
    }
    return !proposals.empty();
}

void OptimizationWorkspace::startNextCandidate()
{
    if (!cancelRequested_ && candidateIndex_ >= candidates_.size()
        && activeSearchMethod_ == SearchMethod::Adaptive && prepareAdaptiveRound()) {
        progress_->setFormat(tr("Refinement round %1 · %v of up to %m evaluations")
            .arg(adaptiveSearch_->refinementRound()));
        statusLabel_->setText(tr("Refinement round %1: testing both sides around %2 = %3%4.")
            .arg(adaptiveSearch_->refinementRound())
            .arg(selectedSymbol_)
            .arg(candidates_[static_cast<std::size_t>(bestRow_)].value, 0, 'f', 3)
            .arg(selectedValueSuffix_));
    }
    if (cancelRequested_ || candidateIndex_ >= candidates_.size()) {
        finishSweep();
        return;
    }
    auto& candidate = candidates_[candidateIndex_];
    if (activeSearchMethod_ == SearchMethod::Adaptive) {
        statusLabel_->setText(candidate.refinementRound == 0
            ? tr("Initial search: evaluating %1 = %2%3.")
                .arg(selectedSymbol_).arg(candidate.value, 0, 'f', 3)
                .arg(selectedValueSuffix_)
            : tr("Refinement round %1: evaluating the %2 trial, %3 = %4%5.")
                .arg(candidate.refinementRound)
                .arg(candidate.trialRole == QStringLiteral("left")
                    ? tr("left") : tr("right"))
                .arg(selectedSymbol_).arg(candidate.value, 0, 'f', 3)
                .arg(selectedValueSuffix_));
    }
    candidate.record = runStore_.create(backend_, sourceFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : sourceFile_,
        QStringLiteral("optimization-candidate"), sessionRecord_ ? sessionRecord_->id : QString{});
    if (!writeCandidateMetadata(candidate)) {
        candidate.record.status = QStringLiteral("Failed");
        runStore_.save(candidate.record);
        setCandidateStatus(candidate.row, tr("Could not write candidate metadata"));
        if (activeSearchMethod_ == SearchMethod::Adaptive && adaptiveSearch_)
            adaptiveSearch_->record(candidate.value, std::nullopt);
        ++candidateIndex_;
        progress_->setValue(static_cast<int>(candidateIndex_));
        QTimer::singleShot(0, this, [this] { startNextCandidate(); });
        return;
    }

    candidate.record.status = QStringLiteral("Running");
    runStore_.save(candidate.record);
    setCandidateStatus(candidate.row, tr("Running"));
    evaluator_->start({
        .authoredSource = source_,
        .variableValues = {{selectedSymbol_.toStdString(), candidate.value}},
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
    candidate.evaluation.reset();

    if (result.status == CandidateEvaluationStatus::Completed && result.objective
        && result.objective->feedpoint) {
        const auto& evaluation = *result.objective;
        candidate.evaluation = evaluation;
        resultsTable_->setItem(candidate.row, ScoreColumn, numericItem(evaluation.score));
        resultsTable_->setItem(candidate.row, SwrColumn, numericItem(evaluation.swr));
        resultsTable_->setItem(candidate.row, FrequencyColumn,
            numericItem(evaluation.feedpoint->frequencyMHz));
        resultsTable_->setItem(candidate.row, ResistanceColumn,
            numericItem(evaluation.feedpoint->impedance.real()));
        resultsTable_->setItem(candidate.row, ReactanceColumn,
            numericItem(evaluation.feedpoint->impedance.imag()));
        setCandidateStatus(candidate.row, tr("Completed"));
        candidate.record.status = QStringLiteral("Completed");
        candidate.record.frequencyCount = result.frequencyCount;
        candidate.record.hasImpedance = true;
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
        case CandidateEvaluationStatus::NoImpedance: status = tr("No impedance results"); break;
        case CandidateEvaluationStatus::Completed: status = tr("Solver failed"); break;
        }
        setCandidateStatus(candidate.row, status);
        candidate.record.status = result.status == CandidateEvaluationStatus::Canceled
            ? QStringLiteral("Canceled")
            : result.status == CandidateEvaluationStatus::TimedOut
                ? QStringLiteral("Timed Out") : QStringLiteral("Failed");
    }
    if (activeSearchMethod_ == SearchMethod::Adaptive && adaptiveSearch_)
        adaptiveSearch_->record(candidate.value,
            candidate.evaluation
                ? std::optional<double>{candidate.evaluation->score} : std::nullopt);
    runStore_.save(candidate.record);
    ++candidateIndex_;
    progress_->setValue(static_cast<int>(candidateIndex_));
    if (sessionRecord_) {
        sessionRecord_->summary = tr("%1 %2 · %3/%4 candidates complete")
            .arg(activeSearchMethod_ == SearchMethod::Adaptive
                ? tr("Adaptive optimize") : tr("Parameter sweep"))
            .arg(selectedSymbol_).arg(candidateIndex_).arg(candidates_.size());
        runStore_.save(*sessionRecord_);
    }
    QTimer::singleShot(0, this, [this] { startNextCandidate(); });
}

void OptimizationWorkspace::finishSweep()
{
    if (cancelRequested_) {
        for (std::size_t index = candidateIndex_; index < candidates_.size(); ++index)
            setCandidateStatus(candidates_[index].row, tr("Skipped"));
        statusLabel_->setText(activeSearchMethod_ == SearchMethod::Adaptive
            ? tr("Adaptive optimization canceled.") : tr("Parameter sweep canceled."));
        progress_->setFormat(activeSearchMethod_ == SearchMethod::Adaptive
            ? tr("Canceled — %v of up to %m evaluations")
            : tr("Canceled — %v / %m candidates"));
    } else {
        statusLabel_->setText(activeSearchMethod_ == SearchMethod::Adaptive
            ? tr("Adaptive optimization complete: %1.").arg(adaptiveStopReason_)
            : tr("Parameter sweep complete."));
        if (activeSearchMethod_ == SearchMethod::Adaptive) {
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
        bestLabel_->setText(tr("Best candidate: %1 = %2%3, %4 = %5")
            .arg(selectedSymbol_)
            .arg(candidates_[static_cast<std::size_t>(bestRow_)].value, 0, 'f', 3)
            .arg(selectedValueSuffix_)
            .arg(objectiveName(activeObjective_.kind))
            .arg(bestScore_, 0, 'f', 3));
    } else {
        bestLabel_->setText(tr("No successful candidate produced impedance results."));
    }
    if (sessionRecord_) {
        sessionRecord_->candidateCount = static_cast<int>(candidates_.size());
        sessionRecord_->status = cancelRequested_ ? QStringLiteral("Canceled") : QStringLiteral("Completed");
        sessionRecord_->summary = activeSearchMethod_ == SearchMethod::Adaptive
                && !adaptiveStopReason_.isEmpty()
            ? tr("%1 · Stopped: %2").arg(bestLabel_->text(), adaptiveStopReason_)
            : bestLabel_->text();
        saveSessionCompletionMetadata();
        runStore_.save(*sessionRecord_);
        if (runsChangedCallback_) runsChangedCallback_();
        sessionRecord_.reset();
    }
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
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
    metadata.insert(QStringLiteral("refinementRounds"),
        adaptiveSearch_ ? adaptiveSearch_->refinementRound() : 0);
    metadata.insert(QStringLiteral("stopReason"), cancelRequested_
        ? QStringLiteral("canceled")
        : adaptiveSearch_ && adaptiveSearch_->stopReason()
                == analysis::AdaptiveStopReason::MaximumEvaluations
            ? QStringLiteral("maximum-evaluations")
        : adaptiveSearch_ && adaptiveSearch_->stopReason()
                == analysis::AdaptiveStopReason::ParameterTolerance
            ? QStringLiteral("parameter-tolerance")
        : adaptiveSearch_ && adaptiveSearch_->stopReason()
                == analysis::AdaptiveStopReason::ScoreTolerance
            ? QStringLiteral("score-tolerance")
        : adaptiveSearch_ && adaptiveSearch_->stopReason()
                == analysis::AdaptiveStopReason::NoSuccessfulCandidate
            ? QStringLiteral("no-successful-candidate")
            : QStringLiteral("completed"));
    metadata.insert(QStringLiteral("stopDescription"), cancelRequested_
        ? tr("canceled by the user") : adaptiveStopReason_);
    writeFile(path, QJsonDocument(metadata).toJson(QJsonDocument::Indented));
}

auto OptimizationWorkspace::writeCandidateMetadata(const Candidate& candidate) -> bool
{
    const QDir directory(candidate.record.directory);
    const auto frequencyMode = selectedFrequencyMode();
    const auto metadata = QJsonObject{
        {QStringLiteral("version"), 4},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("value"), candidate.value},
        {QStringLiteral("unit"), selectedValueSuffix_.trimmed()},
        {QStringLiteral("searchMethod"), activeSearchMethod_ == SearchMethod::Adaptive
            ? QStringLiteral("adaptive") : QStringLiteral("parameter-sweep")},
        {QStringLiteral("refinementRound"), candidate.refinementRound},
        {QStringLiteral("trialRole"), candidate.trialRole},
        {QStringLiteral("objective"), activeObjective_.kind
            == analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies
                ? QStringLiteral("worst-point-across-frequencies")
                : QStringLiteral("minimize-swr-at-frequency")},
        {QStringLiteral("referenceImpedance"), activeObjective_.referenceImpedance},
        {QStringLiteral("targetFrequencyMHz"), activeObjective_.targetFrequencyMHz},
        {QStringLiteral("swrWeight"), activeObjective_.swrWeight},
        {QStringLiteral("resistanceWeight"), activeObjective_.resistanceWeight},
        {QStringLiteral("resistanceTargetOhms"), activeObjective_.resistanceTargetOhms},
        {QStringLiteral("reactanceWeight"), activeObjective_.reactanceWeight},
        {QStringLiteral("reactanceTargetOhms"), activeObjective_.reactanceTargetOhms},
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
        if (candidate.trialRole == QStringLiteral("initial"))
            displayStatus = tr("Initial sample · %1").arg(status);
        else if (!candidate.trialRole.isEmpty())
            displayStatus = tr("Round %1 · %2 trial · %3")
                .arg(candidate.refinementRound)
                .arg(candidate.trialRole == QStringLiteral("left")
                    ? tr("left") : tr("right"))
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
        .kind = static_cast<analysis::OptimizationObjectiveKind>(
            objectiveControl_->currentData().toInt()),
        .referenceImpedance = referenceImpedanceControl_->value(),
        .targetFrequencyMHz = targetFrequencyControl_->value(),
        .swrWeight = swrWeightControl_->value(),
        .resistanceWeight = resistanceWeightControl_->value(),
        .resistanceTargetOhms = resistanceTargetControl_->value(),
        .reactanceWeight = reactanceWeightControl_->value(),
        .reactanceTargetOhms = reactanceTargetControl_->value(),
    };
}

auto OptimizationWorkspace::selectedFrequencyMode() const -> FrequencyMode
{
    return static_cast<FrequencyMode>(frequencyModeControl_->currentData().toInt());
}

auto OptimizationWorkspace::selectedSearchMethod() const -> SearchMethod
{
    return searchMethodTabs_->currentIndex() == 1
        ? SearchMethod::Adaptive : SearchMethod::ParameterSweep;
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
    return {
        .searchMethod = searchMethod,
        .variable = variableControl_->currentText(),
        .minimum = minimumControl_->value(),
        .maximum = maximumControl_->value(),
        .candidateLimit = searchMethod == SearchMethod::Adaptive
            ? adaptiveMaximumEvaluationsControl_->value() : pointsControl_->value(),
        .parameterTolerance = adaptiveParameterToleranceControl_->value(),
        .scoreTolerance = adaptiveScoreToleranceControl_->value(),
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
    return kind == analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies
        ? tr("Minimax score")
        : tr("Selected-frequency score");
}

}
