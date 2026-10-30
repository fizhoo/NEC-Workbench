#include "ui/optimization/OptimizationWorkspace.h"

#include "ui/DisplayFormat.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/optimization/OptimizationComparePanel.h"
#include "ui/optimization/OptimizationWorkspaceSupport.h"

#include "analysis/OptimizationPreset.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <limits>
#include <utility>

namespace necwb::ui {
using namespace detail;

namespace {

auto candidateReviewSizeKey() -> QString
{
    return QStringLiteral("optimization/candidateReviewSizeV1");
}

auto candidateReviewGeometryKey() -> QString
{
    return QStringLiteral("optimization/candidateReviewGeometryV1");
}

auto candidateReviewInitialSize(const QSize& savedSize) -> QSize
{
    constexpr QSize oldDefault{640, 460};
    constexpr QSize preferred{860, 620};
    const auto requested = savedSize.isValid() && savedSize != oldDefault
        ? savedSize : preferred;
    const auto* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) return requested;
    const auto available = screen->availableGeometry().size();
    const QSize maximum{qMax(1, qRound(available.width() * 0.90)),
        qMax(1, qRound(available.height() * 0.88))};
    return requested.boundedTo(maximum);
}

void saveCandidateReviewGeometry(const QDialog& dialog)
{
    QSettings settings;
    settings.setValue(candidateReviewGeometryKey(), dialog.saveGeometry());
    settings.setValue(candidateReviewSizeKey(), dialog.size());
    settings.sync();
}

class CandidateReviewDialog final : public QDialog {
public:
    using QDialog::QDialog;

protected:
    void closeEvent(QCloseEvent* event) override
    {
        saveCandidateReviewGeometry(*this);
        QDialog::closeEvent(event);
    }

    void hideEvent(QHideEvent* event) override
    {
        saveCandidateReviewGeometry(*this);
        QDialog::hideEvent(event);
    }
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

}

OptimizationWorkspace::OptimizationWorkspace(QWidget* parent)
    : QWidget(parent), bestScore_(std::numeric_limits<double>::infinity())
{
    evaluator_ = new CandidateEvaluator(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    buildHistoricalBanner();
    buildConfigurationPanel();
    auto* header = buildStudyHeader();
    auto* resultsPanel = buildResultsPanel();

    auto* workspaceSplitter = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter->setObjectName(QStringLiteral("optimizationWorkspaceSplitter"));
    workspaceSplitter->addWidget(configurationPanel_);
    workspaceSplitter->addWidget(resultsPanel);
    workspaceSplitter->setChildrenCollapsible(false);
    workspaceSplitter->setStretchFactor(0, 0);
    workspaceSplitter->setStretchFactor(1, 1);
    workspaceSplitter->setSizes({300, 780});

    layout->addWidget(historicalBanner_);
    layout->addLayout(header);
    layout->addWidget(workspaceSplitter, 1);

    connectObjectiveSignals();
    connectSearchSignals();
    connectFrequencySignals();
    connectResultSignals();
    applySelectedObjectivePreset();
    updateFrequencyControls();
    updateSearchMethodControls();
}

void OptimizationWorkspace::buildHistoricalBanner()
{
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
    connect(returnToCurrentWorkButton_, &QPushButton::clicked, this, [this] {
        if (returnToCurrentWorkCallback_) returnToCurrentWorkCallback_();
    });
}

void OptimizationWorkspace::buildConfigurationPanel()
{
    auto* setupContent = new QWidget(this);
    setupContent->setObjectName(QStringLiteral("optimizationSetupContent"));
    auto* setupLayout = new QVBoxLayout(setupContent);
    setupLayout->setContentsMargins(4, 4, 4, 4);
    setupLayout->setSpacing(4);
    setupLayout->addWidget(buildSearchSection(setupContent));
    setupLayout->addWidget(buildFrequencySection(setupContent));
    setupLayout->addWidget(buildObjectiveSection(setupContent));
    setupLayout->addStretch();

    auto* configurationScrollArea = new QScrollArea(this);
    configurationScrollArea->setObjectName(
        QStringLiteral("optimizationConfigurationScrollArea"));
    configurationScrollArea->setFrameShape(QFrame::NoFrame);
    configurationScrollArea->setWidgetResizable(true);
    configurationScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    configurationScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    configurationScrollArea->setWidget(setupContent);

    configurationPanel_ = new QWidget(this);
    configurationPanel_->setObjectName(QStringLiteral("optimizationConfigurationPanel"));
    auto* configurationLayout = new QVBoxLayout(configurationPanel_);
    configurationLayout->setContentsMargins(0, 0, 0, 0);
    configurationLayout->addWidget(configurationScrollArea, 1);
    configurationPanel_->setMinimumWidth(260);
    configurationPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
}

auto OptimizationWorkspace::buildSearchSection(QWidget* parent) -> QGroupBox*
{
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
        "Parameter Sweep varies the one checked parameter; the other methods change every "
        "checked parameter together. Double-click Minimum, Maximum, or Tolerance to edit it."));

    pointsControl_ = new FocusWheelSpinBox(this);
    pointsControl_->setRange(2, 101);
    pointsControl_->setObjectName(QStringLiteral("optimizationCandidateCount"));
    pointsControl_->setValue(7);
    pointsControl_->setToolTip(tr(
        "Number of evenly spaced parameter values tested from Minimum through Maximum, "
        "including both endpoints. Each candidate value is evaluated at every selected frequency."));
    searchMethodControl_ = new QComboBox(this);
    searchMethodControl_->setObjectName(QStringLiteral("optimizationSearchMethod"));
    searchMethodControl_->addItem(tr("Parameter Sweep"),
        static_cast<int>(SearchMethod::ParameterSweep));
    searchMethodControl_->addItem(tr("Adaptive Optimize"),
        static_cast<int>(SearchMethod::Adaptive));
    searchMethodControl_->addItem(tr("Nelder-Mead"),
        static_cast<int>(SearchMethod::NelderMead));
    searchMethodControl_->addItem(tr("Differential Evolution"),
        static_cast<int>(SearchMethod::DifferentialEvolution));
    searchMethodControl_->setItemData(0, tr(
        "Exhaustively test evenly spaced values for one parameter."), Qt::ToolTipRole);
    searchMethodControl_->setItemData(1, tr(
        "Refine one or more parameters with transparent coordinate trials around the current best."),
        Qt::ToolTipRole);
    searchMethodControl_->setItemData(2, tr(
        "Optimize one or more continuous parameters with a bounded derivative-free simplex search."),
        Qt::ToolTipRole);
    searchMethodControl_->setItemData(3, tr(
        "Explore one or more continuous parameters with a seeded population-based global search."),
        Qt::ToolTipRole);

    adaptiveMaximumEvaluationsControl_ = new FocusWheelSpinBox(this);
    adaptiveMaximumEvaluationsControl_->setObjectName(
        QStringLiteral("optimizationAdaptiveMaximumEvaluations"));
    adaptiveMaximumEvaluationsControl_->setRange(5, 101);
    adaptiveMaximumEvaluationsControl_->setValue(21);
    adaptiveMaximumEvaluationsControl_->setToolTip(tr(
        "Maximum solver candidates. Adaptive starts near the center and boundaries; "
        "Nelder–Mead starts with one simplex vertex per selected parameter plus one."));
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

    auto* variableSection = new QGroupBox(tr("Search"), parent);
    variableSection->setObjectName(QStringLiteral("optimizationVariableSection"));
    parameterSettings_ = new QWidget(variableSection);
    parameterSettings_->setObjectName(QStringLiteral("optimizationParameterSettings"));
    auto* variableSectionLayout = new QVBoxLayout(variableSection);
    variableSectionLayout->setContentsMargins(5, 7, 5, 5);
    variableSectionLayout->setSpacing(4);
    auto* searchMethodRow = new QHBoxLayout;
    searchMethodRow->setContentsMargins(0, 0, 0, 0);
    searchMethodRow->addWidget(new QLabel(tr("Method"), variableSection));
    searchMethodRow->addWidget(searchMethodControl_, 1);
    variableSectionLayout->addLayout(searchMethodRow);
    variableSectionLayout->addWidget(variablesTable_, 1);
    variableSectionLayout->addWidget(parameterSettings_);
    auto* parameterizationBar = new QWidget(variableSection);
    parameterizationBar->setObjectName(QStringLiteral("optimizationParameterizationHint"));
    auto* parameterizationLayout = new QHBoxLayout(parameterizationBar);
    parameterizationLayout->setContentsMargins(0, 0, 0, 0);
    parameterizationLayout->setSpacing(4);
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
    adaptiveScoreToleranceControl_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pointsControl_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    adaptiveMaximumEvaluationsControl_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    searchBudgetLabel_ = new QLabel(tr("Candidate count"), parameterSettings_);
    searchBudgetLabel_->setObjectName(QStringLiteral("optimizationSearchBudgetLabel"));
    searchBudgetLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    parameterGrid->addWidget(searchBudgetLabel_, 0, 0);
    parameterGrid->addWidget(pointsControl_, 0, 1);
    parameterGrid->addWidget(adaptiveMaximumEvaluationsControl_, 0, 1);
    adaptiveScoreToleranceLabel_ = gridLabel(tr("Score tolerance"),
        QStringLiteral("optimizationScoreToleranceLabel"));
    parameterGrid->addWidget(adaptiveScoreToleranceLabel_, 1, 0);
    parameterGrid->addWidget(adaptiveScoreToleranceControl_, 1, 1);

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
    parameterGrid->addWidget(differentialEvolutionSettings_, 0, 0, 2, 2);

    auto* resetSearchDefaultsButton = new QPushButton(tr("Reset Search Defaults"), parameterSettings_);
    resetSearchDefaultsButton->setObjectName(
        QStringLiteral("optimizationResetSearchDefaults"));
    resetSearchDefaultsButton->setToolTip(tr(
        "Restore the default sweep count or optimizer search settings without changing the variables, ranges, frequencies, or objective."));
    parameterGrid->addWidget(resetSearchDefaultsButton, 2, 0, 1, 2, Qt::AlignRight);
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
            adaptiveScoreToleranceControl_->setValue(0.001);
        } else {
            pointsControl_->setValue(7);
        }
    });
    connect(chooseParameterFields, &QPushButton::clicked, this, [this] {
        if (parameterizationHelpCallback_) parameterizationHelpCallback_();
    });
    return variableSection;
}

auto OptimizationWorkspace::buildFrequencySection(QWidget* parent) -> QGroupBox*
{
    frequencyModeControl_ = new QComboBox(this);
    frequencyModeControl_->setObjectName(QStringLiteral("optimizationFrequencyMode"));
    frequencyModeControl_->addItem(tr("Use Model FR Sweep"),
        static_cast<int>(FrequencyMode::ModelSweep));
    frequencyModeControl_->addItem(tr("Use Selected Frequencies"),
        static_cast<int>(FrequencyMode::Explicit));
    frequencyModeControl_->addItem(tr("Custom Continuous Sweep"),
        static_cast<int>(FrequencyMode::Continuous));
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

    auto* frequencySection = new QGroupBox(tr("Frequencies"), parent);
    frequencySection->setObjectName(QStringLiteral("optimizationFrequencySection"));
    auto* frequencyPageLayout = new QVBoxLayout(frequencySection);
    frequencyPageLayout->setContentsMargins(5, 7, 5, 5);
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
    return frequencySection;
}

auto OptimizationWorkspace::buildObjectiveSection(QWidget* parent) -> QGroupBox*
{
    referenceImpedanceControl_ = new FocusWheelDoubleSpinBox(this);
    referenceImpedanceControl_->setObjectName(
        QStringLiteral("optimizationReferenceImpedance"));
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
    criterionItem(3, tr("Peak Gain in Cut (dBi)"),
        tr("Highest gain found in the selected azimuth or elevation cut."));
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

    auto* objectiveSection = new QGroupBox(tr("Objective"), parent);
    objectiveSection->setObjectName(QStringLiteral("optimizationObjectiveSection"));
    auto* objectiveLayout = new QVBoxLayout(objectiveSection);
    objectiveLayout->setContentsMargins(5, 7, 5, 5);
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
    objectiveWarningLabel_ = new QLabel(objectiveSection);
    objectiveWarningLabel_->setObjectName(QStringLiteral("optimizationObjectiveWarning"));
    objectiveWarningLabel_->setWordWrap(true);
    objectiveWarningLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
    objectiveWarningLabel_->hide();
    objectiveLayout->addWidget(objectiveWarningLabel_);

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
    objectivePresetControl_ = new QComboBox(objectiveSettings);
    objectivePresetControl_->setObjectName(QStringLiteral("optimizationObjectivePreset"));
    for (const auto& definition : analysis::optimizationPresetDefinitions())
        objectivePresetControl_->addItem(optimizationPresetName(definition.id),
            static_cast<int>(definition.id));
    objectivePresetControl_->setCurrentIndex(objectivePresetControl_->findData(
        static_cast<int>(analysis::OptimizationPresetId::MinimizeSwr)));
    objectiveForm->addRow(tr("Goal"), objectivePresetControl_);
    presetSwrLimitControl_ = new FocusWheelDoubleSpinBox(objectiveSettings);
    presetSwrLimitControl_->setObjectName(QStringLiteral("optimizationPresetSwrLimit"));
    presetSwrLimitControl_->setRange(1.0, 1000.0);
    presetSwrLimitControl_->setDecimals(DisplayDecimalPlaces);
    presetSwrLimitControl_->setValue(2.0);
    presetSwrLimitLabel_ = new QLabel(tr("Target SWR"), objectiveSettings);
    objectiveForm->addRow(presetSwrLimitLabel_, presetSwrLimitControl_);
    objectiveForm->addRow(tr("Reference impedance"), referenceImpedanceControl_);
    directionSetupPanel_ = new QWidget(objectiveSettings);
    directionSetupPanel_->setObjectName(QStringLiteral("optimizationDirectionSetupPanel"));
    auto* directionSetupLayout = new QHBoxLayout(directionSetupPanel_);
    directionSetupLayout->setContentsMargins(0, 0, 0, 0);
    directionSetupSummaryLabel_ = new QLabel(directionSetupPanel_);
    directionSetupSummaryLabel_->setObjectName(
        QStringLiteral("optimizationDirectionSetupSummary"));
    directionSetupSummaryLabel_->setWordWrap(true);
    directionSetupButton_ = new QPushButton(tr("Choose…"), directionSetupPanel_);
    directionSetupButton_->setObjectName(QStringLiteral("optimizationChooseDirection"));
    findMaximumGainButton_ = new QPushButton(tr("Find Maximum Gain…"), directionSetupPanel_);
    findMaximumGainButton_->setObjectName(QStringLiteral("optimizationFindMaximumGain"));
    findMaximumGainButton_->setToolTip(tr(
        "Runs a temporary radiation-pattern calculation to find the model's maximum-gain direction and use it as the default optimization pattern slice. It does not start optimization or modify the NEC model."));
    directionSetupLayout->addWidget(directionSetupSummaryLabel_, 1);
    directionSetupLayout->addWidget(findMaximumGainButton_);
    directionSetupLayout->addWidget(directionSetupButton_);
    directionSetupLabel_ = new QLabel(tr("Directional setup"), objectiveSettings);
    objectiveForm->addRow(directionSetupLabel_, directionSetupPanel_);
    radiationComponentLabel_ = new QLabel(tr("Radiation component"), objectiveSettings);
    objectiveForm->addRow(radiationComponentLabel_, radiationComponentControl_);
    latestPatternPeakPanel_ = new QWidget(objectiveSettings);
    latestPatternPeakPanel_->setObjectName(
        QStringLiteral("optimizationLatestPatternPeakPanel"));
    auto* latestPatternPeakLayout = new QHBoxLayout(latestPatternPeakPanel_);
    latestPatternPeakLayout->setContentsMargins(0, 0, 0, 0);
    useLatestPatternPeakButton_ = new QPushButton(
        tr("Use Latest Pattern Peak"), latestPatternPeakPanel_);
    useLatestPatternPeakButton_->setObjectName(
        QStringLiteral("optimizationUseLatestPatternPeak"));
    useLatestPatternPeakButton_->setToolTip(tr(
        "Set the forward theta and phi from the latest active-model radiation pattern."));
    useLatestPatternPeakButton_->hide();
    latestPatternPeakLabel_ = new QLabel(latestPatternPeakPanel_);
    latestPatternPeakLabel_->setObjectName(
        QStringLiteral("optimizationLatestPatternPeakStatus"));
    latestPatternPeakLabel_->setWordWrap(true);
    latestPatternPeakLayout->addWidget(useLatestPatternPeakButton_);
    latestPatternPeakLayout->addWidget(latestPatternPeakLabel_, 1);
    latestPatternPeakFieldLabel_ = new QLabel(tr("Pattern peak"), objectiveSettings);
    objectiveForm->addRow(latestPatternPeakFieldLabel_, latestPatternPeakPanel_);
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
    return objectiveSection;
}

auto OptimizationWorkspace::buildStudyHeader() -> QHBoxLayout*
{
    studySummaryLabel_ = new QLabel(this);
    studySummaryLabel_->setObjectName(QStringLiteral("optimizationStudySummary"));
    studySummaryLabel_->setWordWrap(true);
    auto* buttons = new QHBoxLayout;
    runButton_ = new QPushButton(tr("Run Parameter Sweep"), this);
    runButton_->setObjectName(QStringLiteral("optimizationRun"));
    cancelButton_ = new QPushButton(tr("Stop"), this);
    cancelButton_->setEnabled(false);
    buttons->addWidget(studySummaryLabel_, 1);
    buttons->addWidget(runButton_);
    buttons->addWidget(cancelButton_);
    return buttons;
}

auto OptimizationWorkspace::buildResultsPanel() -> QWidget*
{
    applyBestButton_ = new QPushButton(tr("Apply Best to Model"), this);
    applyBestButton_->setObjectName(QStringLiteral("optimizationApplyBest"));
    applyBestButton_->setEnabled(false);
    applyBestButton_->setToolTip(tr(
        "Replace the selected SY expression with the best numeric value. This model edit is undoable."));
    progress_ = new QProgressBar(this);
    progress_->setTextVisible(true);

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
    resultsTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    resultsTable_->setToolTip(tr(
        "Double-click a completed candidate to inspect it and compare it with the optimization baseline. "
        "Right-click for apply and run actions."));
    resultsTable_->verticalHeader()->hide();
    resultsTable_->verticalHeader()->setDefaultSectionSize(21);
    resultsTable_->horizontalHeader()->setSortIndicatorShown(false);
    for (auto column = static_cast<int>(ValueColumn);
         column <= static_cast<int>(FrontToRearColumn); ++column) {
        resultsTable_->horizontalHeaderItem(column)->setToolTip(
            tr("Click to sort completed candidates by this column."));
    }
    resultsTable_->horizontalHeaderItem(StatusColumn)->setToolTip(
        tr("Candidate status is informational and is not sortable."));
    resultsTable_->horizontalHeaderItem(RunColumn)->setToolTip(
        tr("Run identifier is informational and is not sortable."));
    resultsTable_->horizontalHeader()->setSectionResizeMode(StatusColumn, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(RunColumn, QHeaderView::ResizeToContents);

    candidatePlots_ = new CandidatePlotsView(this);
    candidatePlots_->setCandidateActivatedCallback([this](int candidateIndex) {
        const auto row = tableRowForCandidateIndex(candidateIndex);
        if (row < 0) return;
        resultsTable_->selectRow(row);
        showCandidateDetails(candidateIndex);
    });
    buildCandidateDetailsWindow();
    resetCandidateDetails();

    auto* resultsPanel = new QWidget(this);
    resultsPanel->setObjectName(QStringLiteral("optimizationResultsPanel"));
    auto* resultsLayout = new QVBoxLayout(resultsPanel);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(4);
    auto* resultStatusRow = new QHBoxLayout;
    resultStatusRow->addWidget(bestLabel_, 1);
    resultStatusRow->addWidget(applyBestButton_);
    resultsLayout->addWidget(progress_);
    resultsLayout->addWidget(statusLabel_);
    resultsLayout->addLayout(resultStatusRow);
    auto* candidateHint = new QLabel(tr(
        "Double-click a completed candidate to open Candidate Results. While it is open, select another row to update it. Right-click to apply or run."),
        resultsPanel);
    candidateHint->setObjectName(QStringLiteral("optimizationCandidateHint"));
    candidateHint->setWordWrap(true);
    resultsLayout->addWidget(candidateHint);
    auto* resultsSplitter = new QSplitter(Qt::Vertical, resultsPanel);
    resultsSplitter->setObjectName(QStringLiteral("optimizationResultsSplitter"));
    resultsSplitter->addWidget(resultsTable_);
    resultsSplitter->addWidget(candidatePlots_);
    resultsSplitter->setChildrenCollapsible(false);
    resultsSplitter->setStretchFactor(0, 4);
    resultsSplitter->setStretchFactor(1, 1);
    resultsSplitter->setSizes({500, 160});
    resultsLayout->addWidget(resultsSplitter, 1);
    resultsPanel->setMinimumWidth(400);
    return resultsPanel;
}

void OptimizationWorkspace::buildCandidateDetailsWindow()
{
    candidateDetailsWindow_ = new CandidateReviewDialog(nullptr, Qt::Window);
    candidateDetailsWindow_->setObjectName(
        QStringLiteral("optimizationCandidateDetailsWindow"));
    candidateDetailsWindow_->setWindowTitle(tr("Candidate Results"));
    candidateDetailsWindow_->setModal(false);
    candidateDetailsWindow_->setAttribute(Qt::WA_QuitOnClose, false);
    QSettings candidateReviewSettings;
    const auto geometryRestored = candidateDetailsWindow_->restoreGeometry(
        candidateReviewSettings.value(candidateReviewGeometryKey()).toByteArray());
    if (!geometryRestored) {
        candidateDetailsWindow_->resize(candidateReviewInitialSize(
            candidateReviewSettings.value(candidateReviewSizeKey()).toSize()));
    }
    auto* detailLayout = new QVBoxLayout(candidateDetailsWindow_);
    detailLayout->setContentsMargins(4, 4, 4, 4);
    detailLayout->setSpacing(4);
    candidateDetailLabel_ = new QLabel(candidateDetailsWindow_);
    candidateDetailLabel_->setObjectName(QStringLiteral("optimizationCandidateDetailLabel"));
    candidateDetailLabel_->setWordWrap(false);
    candidateDetailsTable_ = new QTableWidget(candidateDetailsWindow_);
    candidateDetailsTable_->setObjectName(QStringLiteral("optimizationCandidateDetails"));
    candidateDetailsTable_->setColumnCount(9);
    candidateDetailsTable_->setHorizontalHeaderLabels(
        {tr("Frequency (MHz)"), tr("SWR"), tr("R (Ω)"), tr("X (Ω)"),
            tr("Gain (dBi)"), tr("F/B (dB)"), tr("F/R (dB)"),
            tr("Theta (°)"), tr("Phi (°)")});
    candidateDetailsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    candidateDetailsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    candidateDetailsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    candidateDetailsTable_->verticalHeader()->hide();
    candidateDetailsTable_->verticalHeader()->setDefaultSectionSize(21);
    candidateDetailsTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    candidateDetailsTable_->horizontalHeader()->setStretchLastSection(false);
    candidateDetailsTable_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    comparePanel_ = new OptimizationComparePanel(candidateDetailsWindow_);
    comparePanel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    candidateDetailViews_ = new QTabWidget(candidateDetailsWindow_);
    candidateDetailViews_->setObjectName(
        QStringLiteral("optimizationCandidateDetailViews"));
    candidateDetailViews_->setDocumentMode(true);
    candidateDetailViews_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    candidateDetailViews_->addTab(comparePanel_, tr("Results"));
    candidateDetailViews_->addTab(candidateDetailsTable_, tr("Frequency Table"));
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
}

void OptimizationWorkspace::connectObjectiveSignals()
{
    connect(objectivePresetControl_, &QComboBox::currentIndexChanged,
        this, [this] { applySelectedObjectivePreset(); });
    connect(presetSwrLimitControl_, &QDoubleSpinBox::valueChanged, this, [this] {
        const auto preset = selectedObjectivePreset();
        if (preset == analysis::OptimizationPresetId::SwrLimit
            || preset == analysis::OptimizationPresetId::SwrAndGain)
            applySelectedObjectivePreset();
    });
    connect(referenceImpedanceControl_, &QDoubleSpinBox::valueChanged, this, [this] {
        if (selectedObjectivePreset() == analysis::OptimizationPresetId::Custom)
            updateObjectiveControls();
        else
            applySelectedObjectivePreset();
    });
    for (auto* control : {swrWeightControl_, resistanceWeightControl_,
             reactanceWeightControl_, forwardGainWeightControl_,
             frontToBackWeightControl_,
             frontToRearWeightControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
            markObjectiveCustom();
            updateWorkload();
            updateReadiness();
        });
    }
    for (auto* control : {forwardGainWeightControl_, frontToBackWeightControl_,
             frontToRearWeightControl_}) {
        connect(control, &QDoubleSpinBox::valueChanged,
            this, [this] { updateDirectionalSeedState(); });
    }
    for (auto row = 0; row < 6; ++row) {
        connect(objectiveValueControls_[row], &QDoubleSpinBox::valueChanged,
            this, [this] {
                markObjectiveCustom();
                updateObjectiveControls();
            });
        connect(objectiveGoalControls_[row], &QComboBox::currentIndexChanged,
            this, [this, row] {
                markObjectiveCustom();
                objectiveGoalControls_[row]->setToolTip(
                    objectiveGoalControls_[row]->currentData(Qt::ToolTipRole).toString());
                configureAggregationControl(objectiveAggregationControls_[row],
                    selectedGoal(objectiveGoalControls_[row]));
                updateObjectiveControls();
            });
        connect(objectiveAggregationControls_[row], &QComboBox::currentIndexChanged,
            this, [this, row] {
                markObjectiveCustom();
                objectiveAggregationControls_[row]->setToolTip(
                    objectiveAggregationControls_[row]
                        ->currentData(Qt::ToolTipRole).toString());
                updateObjectiveControls();
            });
    }
    connect(resetObjectiveDefaultsButton_, &QPushButton::clicked, this, [this] {
        if (selectedObjectivePreset() != analysis::OptimizationPresetId::Custom) {
            applySelectedObjectivePreset();
            return;
        }
        auto defaults = analysis::OptimizationObjectiveSpec{};
        defaults.referenceImpedance = referenceImpedanceControl_->value();
        defaults.directionalEvaluationMode =
            analysis::DirectionalEvaluationMode::StrongestInAzimuthCut;
        directionSetupConfigured_ = false;
        applyingObjectivePreset_ = true;
        restoreObjectiveSetup(defaults);
        applyingObjectivePreset_ = false;
        updateObjectiveControls();
        updateWorkload();
        updateReadiness();
    });
    connect(radiationComponentControl_, &QComboBox::currentIndexChanged, this, [this] {
        if (!applyingObjectivePreset_ && directionalPresetRequiresSetup()) {
            directionSetupReady_ = false;
            (void)seedDirectionFromLatestPattern();
        }
        updateLatestPatternPeakControls();
        updateWorkload();
        updateReadiness();
    });
    connect(useLatestPatternPeakButton_, &QPushButton::clicked,
        this, [this] { useLatestPatternPeak(); });
    connect(directionSetupButton_, &QPushButton::clicked,
        this, [this] { chooseDirectionStrategy(); });
    connect(findMaximumGainButton_, &QPushButton::clicked, this, [this] {
        const auto setup = currentStudySetup();
        if (setup.frequenciesMHz.empty() || !findMaximumGainCallback_) return;
        maximumGainSearchRequested_ = true;
        updateLatestPatternPeakControls();
        if (!findMaximumGainCallback_(setup.frequenciesMHz.front())) {
            maximumGainSearchRequested_ = false;
            updateLatestPatternPeakControls();
        }
    });
    connect(editObjectiveButton_, &QPushButton::clicked,
        this, [this] { editObjectiveSetup(); });
}

void OptimizationWorkspace::connectSearchSignals()
{
    connect(pointsControl_, &QSpinBox::valueChanged, this, [this] { updateWorkload(); });
    connect(searchMethodControl_, &QComboBox::currentIndexChanged,
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
    connect(variablesTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        // Parameter Sweep varies one parameter, so checking a row there replaces the previous choice.
        if (item != nullptr && item->column() == 0 && item->checkState() == Qt::Checked
            && selectedSearchMethod() == SearchMethod::ParameterSweep) {
            const QSignalBlocker blocker(variablesTable_);
            for (auto row = 0; row < variablesTable_->rowCount(); ++row) {
                auto* useItem = variablesTable_->item(row, 0);
                if (useItem != nullptr && useItem != item) useItem->setCheckState(Qt::Unchecked);
            }
        }
        updateWorkload();
        updateReadiness();
    });
    connect(runButton_, &QPushButton::clicked, this, [this] { requestOptimizationStart(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelSweep(); });
    connect(evaluator_, &CandidateEvaluator::finished, this,
        [this](CandidateEvaluationResult result) {
            finishCurrentCandidate(std::move(result));
        });
}

void OptimizationWorkspace::connectFrequencySignals()
{
    connect(frequencyModeControl_, &QComboBox::currentIndexChanged,
        this, [this] { updateFrequencyControls(); });
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
    auto* deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), frequencyTable_);
    connect(deleteShortcut, &QShortcut::activated,
        this, [this] { removeSelectedFrequencies(); });
    auto* backspaceShortcut = new QShortcut(
        QKeySequence(Qt::Key_Backspace), frequencyTable_);
    connect(backspaceShortcut, &QShortcut::activated,
        this, [this] { removeSelectedFrequencies(); });
    connect(addAmateurBandButton_, &QPushButton::clicked,
        this, [this] { chooseAmateurBands(); });
}

void OptimizationWorkspace::connectResultSignals()
{
    connect(resultsTable_, &QTableWidget::cellDoubleClicked,
        this, [this](int row, int) {
            const auto candidateIndex = candidateIndexAtTableRow(row);
            if (candidateIndex >= 0) showCandidateDetails(candidateIndex);
        });
    connect(resultsTable_, &QTableWidget::customContextMenuRequested,
        this, [this](const QPoint& position) { showCandidateContextMenu(position); });
    connect(resultsTable_, &QTableWidget::currentCellChanged, this,
        [this](int currentRow, int, int, int) {
            updateComparisonCandidate();
            if (!candidateDetailsWindow_->isVisible()) return;
            const auto candidateIndex = candidateIndexAtTableRow(currentRow);
            if (candidateIndex < 0
                || static_cast<std::size_t>(candidateIndex) >= candidates_.size()) return;
            const auto& candidate = candidates_[static_cast<std::size_t>(candidateIndex)];
            if (candidate.evaluation || candidate.availableMeasurements
                || !candidate.feedpoints.empty()) {
                updateCandidateDetails(candidateIndex);
            }
        });
    connect(resultsTable_->horizontalHeader(), &QHeaderView::sectionClicked,
        this, [this](int column) { sortCandidateTable(column); });
    connect(applyBestButton_, &QPushButton::clicked, this, [this] {
        applyCandidate(bestRow_, false);
    });
    connect(applyCandidateButton_, &QPushButton::clicked,
        this, [this] { applyCandidate(detailCandidateRow_, false); });
    connect(applyCandidateAndRunButton_, &QPushButton::clicked,
        this, [this] { applyCandidate(detailCandidateRow_, true); });
}

OptimizationWorkspace::~OptimizationWorkspace()
{
    saveCandidateReviewGeometry(*candidateDetailsWindow_);
    delete candidateDetailsWindow_;
}

}
