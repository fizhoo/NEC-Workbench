#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/AdaptiveSearch.h"
#include "analysis/DifferentialEvolutionSearch.h"
#include "analysis/NelderMeadSearch.h"
#include "analysis/OptimizationObjective.h"
#include "model/ModelSetup.h"
#include "nec/NecSymbolResolver.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/optimization/CandidateEvaluator.h"

#include <QString>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSplitter;
class QSpinBox;
class QTableWidget;
class QTabBar;
class QTabWidget;

namespace necwb::ui {

class CandidatePlotsView;
class DirectionalMetricsView;
class SweepPlotsView;

class OptimizationWorkspace final : public QWidget {
public:
    explicit OptimizationWorkspace(QWidget* parent = nullptr);
    ~OptimizationWorkspace() override;

    void setContext(QString source, QString sourceFile, QString backend,
        QString executable, int timeoutSeconds, bool modelValid);
    void setModelValid(bool valid);
    void setExternalRunActive(bool active);
    void setRunsChangedCallback(std::function<void()> callback);
    void setRunningChangedCallback(std::function<void()> callback);
    void setReturnToCurrentWorkCallback(std::function<void()> callback);
    void setParameterizationHelpCallback(std::function<void()> callback);
    void setApplyParameterCallback(
        std::function<bool(std::vector<std::pair<QString, double>>)> callback);
    void setApplyAndRunCallback(
        std::function<bool(std::vector<std::pair<QString, double>>)> callback);
    auto loadSession(const QString& sessionId) -> bool;
    void leaveHistoricalSession();
    [[nodiscard]] auto isRunning() const noexcept -> bool;
    void restoreCandidateDetailsWindow();
    void cancel();
    void cancelAndWait();

private:
    enum class FrequencyMode {
        ModelSweep,
        Explicit,
        Continuous
    };

    enum class SearchMethod {
        ParameterSweep,
        Adaptive,
        NelderMead,
        DifferentialEvolution
    };

    struct FrequencySelectionState {
        FrequencyMode mode{FrequencyMode::ModelSweep};
        std::vector<double> explicitFrequenciesMHz;
        double continuousStartMHz{};
        double continuousStopMHz{};
        double continuousStepMHz{};
    };

    struct StudySetup {
        struct VariableRange {
            QString name;
            double resolvedValue{};
            double minimum{};
            double maximum{};
            double tolerance{0.01};
        };

        SearchMethod searchMethod{SearchMethod::ParameterSweep};
        QString variable;
        double minimum{};
        double maximum{};
        int candidateLimit{};
        double parameterTolerance{};
        double scoreTolerance{};
        int populationSize{};
        int maximumGenerations{};
        double mutationFactor{};
        double crossoverRate{};
        int randomSeed{};
        std::vector<VariableRange> variables;
        FrequencySelectionState frequencySelection;
        std::vector<double> frequenciesMHz;
        analysis::OptimizationObjectiveSpec objective;
    };

    struct Candidate {
        double value{};
        std::vector<double> values;
        int row{};
        AnalysisRunRecord record;
        std::vector<analysis::FeedpointResult> feedpoints;
        std::vector<analysis::RadiationSample> radiation;
        std::optional<analysis::OptimizationObjectiveResult> evaluation;
        int refinementRound{};
        QString trialRole;
    };

    void populateVariables(const nec::SymbolResolution& resolution);
    [[nodiscard]] auto selectedAdaptiveVariables() const
        -> std::vector<StudySetup::VariableRange>;
    [[nodiscard]] auto candidateDescription(const Candidate& candidate) const -> QString;
    void updateBounds();
    void updateObjectiveControls();
    void updateFrequencyControls();
    void updateSearchMethodControls();
    void updateSetupSummaries();
    void updateWorkload();
    void showCandidateDetails(int row);
    void updateCandidateDetails(int row);
    void resetCandidateDetails();
    void updateCandidateActionState();
    [[nodiscard]] auto candidateParameterValues(int row) const
        -> std::vector<std::pair<QString, double>>;
    void applyCandidate(int row, bool runAfterApply);
    void updateCandidateRowToolTip(int row);
    void updateCandidatePlots();
    void updateReadiness();
    void populateModelFrequencies(const model::FrequencyDefinition& frequency);
    void setExplicitFrequencies(const std::vector<double>& frequenciesMHz);
    void addExplicitFrequency(double frequencyMHz);
    void removeSelectedFrequencies();
    void clearExplicitFrequencies();
    void chooseAmateurBands();
    void pasteExplicitFrequencies();
    void editFrequencySetup();
    void editObjectiveSetup();
    void restoreObjectiveSetup(const analysis::OptimizationObjectiveSpec& objective);
    void startSweep();
    void cancelSweep();
    void startNextCandidate();
    [[nodiscard]] auto prepareNextSearchCandidates() -> bool;
    void appendCandidate(double value, int refinementRound = 0,
        QString trialRole = {});
    void appendCandidate(std::vector<double> values, int refinementRound,
        QString trialRole);
    void finishCurrentCandidate(CandidateEvaluationResult result);
    void finishSweep();
    void saveSessionCompletionMetadata();
    auto writeCandidateMetadata(const Candidate& candidate) -> bool;
    void setCandidateStatus(int row, const QString& status);
    [[nodiscard]] auto selectedObjective() const -> analysis::OptimizationObjectiveSpec;
    [[nodiscard]] auto selectedFrequencyMode() const -> FrequencyMode;
    [[nodiscard]] auto selectedSearchMethod() const -> SearchMethod;
    [[nodiscard]] auto selectedFrequencyPlan() const -> analysis::FrequencyPlan;
    [[nodiscard]] auto explicitFrequencies() const -> std::vector<double>;
    [[nodiscard]] auto currentStudySetup() const -> StudySetup;
    [[nodiscard]] auto captureFrequencySelection() const -> FrequencySelectionState;
    void restoreFrequencySelection(const FrequencySelectionState& state);
    [[nodiscard]] auto objectiveName(analysis::OptimizationObjectiveKind kind) const -> QString;
    [[nodiscard]] static auto isMultivariable(SearchMethod method) noexcept -> bool;
    [[nodiscard]] auto searchMethodName(SearchMethod method) const -> QString;
    [[nodiscard]] static auto searchMethodId(SearchMethod method) -> QString;
    [[nodiscard]] auto currentSearchIteration() const noexcept -> int;
    [[nodiscard]] auto currentStopReasonId() const -> QString;
    [[nodiscard]] auto bestBoundaryDescription() const -> QString;

    QTableWidget* variablesTable_{};
    QWidget* parameterSettings_{};
    QLabel* variableLabel_{};
    QLabel* minimumLabel_{};
    QLabel* maximumLabel_{};
    QComboBox* variableControl_{};
    QComboBox* objectiveControl_{};
    QDoubleSpinBox* targetFrequencyControl_{};
    QTableWidget* objectiveCriteriaTable_{};
    QDoubleSpinBox* swrWeightControl_{};
    QDoubleSpinBox* resistanceWeightControl_{};
    QDoubleSpinBox* resistanceTargetControl_{};
    QDoubleSpinBox* reactanceWeightControl_{};
    QDoubleSpinBox* reactanceTargetControl_{};
    QDoubleSpinBox* forwardGainWeightControl_{};
    QDoubleSpinBox* frontToBackWeightControl_{};
    QDoubleSpinBox* frontToRearWeightControl_{};
    std::array<QComboBox*, 6> objectiveGoalControls_{};
    std::array<QDoubleSpinBox*, 6> objectiveValueControls_{};
    std::array<QComboBox*, 6> objectiveAggregationControls_{};
    QPushButton* resetObjectiveDefaultsButton_{};
    QDoubleSpinBox* forwardThetaControl_{};
    QDoubleSpinBox* forwardPhiControl_{};
    QComboBox* radiationComponentControl_{};
    QComboBox* frequencyModeControl_{};
    QWidget* explicitFrequencyPanel_{};
    QWidget* continuousFrequencyPanel_{};
    QListWidget* frequencyTable_{};
    QDoubleSpinBox* frequencyEntryControl_{};
    QPushButton* addFrequencyButton_{};
    QPushButton* removeFrequencyButton_{};
    QPushButton* pasteFrequencyButton_{};
    QPushButton* clearFrequencyButton_{};
    QPushButton* addAmateurBandButton_{};
    QDoubleSpinBox* continuousStartControl_{};
    QDoubleSpinBox* continuousStopControl_{};
    QDoubleSpinBox* continuousStepControl_{};
    QLabel* workloadLabel_{};
    QLabel* frequencySummaryLabel_{};
    QPushButton* editFrequencyButton_{};
    QDialog* frequencyDialog_{};
    QDoubleSpinBox* minimumControl_{};
    QDoubleSpinBox* maximumControl_{};
    QSpinBox* pointsControl_{};
    QTabBar* searchMethodTabs_{};
    QLabel* searchBudgetLabel_{};
    QLabel* adaptiveParameterToleranceLabel_{};
    QLabel* adaptiveScoreToleranceLabel_{};
    QSpinBox* adaptiveMaximumEvaluationsControl_{};
    QDoubleSpinBox* adaptiveParameterToleranceControl_{};
    QDoubleSpinBox* adaptiveScoreToleranceControl_{};
    QWidget* differentialEvolutionSettings_{};
    QSpinBox* differentialEvolutionPopulationControl_{};
    QSpinBox* differentialEvolutionGenerationControl_{};
    QDoubleSpinBox* differentialEvolutionMutationControl_{};
    QDoubleSpinBox* differentialEvolutionCrossoverControl_{};
    QDoubleSpinBox* differentialEvolutionScoreToleranceControl_{};
    QSpinBox* differentialEvolutionSeedControl_{};
    QDoubleSpinBox* referenceImpedanceControl_{};
    QLabel* studySummaryLabel_{};
    QLabel* objectiveSummaryLabel_{};
    QLabel* objectiveExplanationLabel_{};
    QLabel* targetFrequencyLabel_{};
    QLabel* directionalFrequencyNote_{};
    QPushButton* editObjectiveButton_{};
    QDialog* objectiveDialog_{};
    QPushButton* runButton_{};
    QPushButton* cancelButton_{};
    QPushButton* applyBestButton_{};
    QProgressBar* progress_{};
    QLabel* statusLabel_{};
    QLabel* bestLabel_{};
    QTableWidget* resultsTable_{};
    CandidatePlotsView* candidatePlots_{};
    QDialog* candidateDetailsWindow_{};
    QLabel* candidateDetailLabel_{};
    QTableWidget* candidateDetailsTable_{};
    QTabWidget* candidateDetailViews_{};
    SweepPlotsView* candidateDetailPlots_{};
    DirectionalMetricsView* candidateDirectionalPlots_{};
    QPushButton* applyCandidateButton_{};
    QPushButton* applyCandidateAndRunButton_{};
    QWidget* historicalBanner_{};
    QLabel* historicalBannerTitle_{};
    QPushButton* returnToCurrentWorkButton_{};
    CandidateEvaluator* evaluator_{};
    AnalysisRunStore runStore_;
    std::function<void()> runsChangedCallback_;
    std::function<void()> runningChangedCallback_;
    std::function<void()> returnToCurrentWorkCallback_;
    std::function<void()> parameterizationHelpCallback_;
    std::function<bool(std::vector<std::pair<QString, double>>)> applyParameterCallback_;
    std::function<bool(std::vector<std::pair<QString, double>>)> applyAndRunCallback_;
    std::vector<nec::SymbolDefinition> definitions_;
    std::vector<Candidate> candidates_;
    std::vector<double> modelFrequenciesMHz_;
    std::vector<double> activeFrequenciesMHz_;
    std::optional<AnalysisRunRecord> sessionRecord_;
    std::optional<FrequencySelectionState> activeFrequencySelection_;
    QString source_;
    QString sourceFile_;
    QString backend_;
    QString executable_;
    QString selectedSymbol_;
    std::vector<StudySetup::VariableRange> activeVariables_;
    QString selectedValueSuffix_;
    analysis::OptimizationObjectiveSpec activeObjective_;
    SearchMethod activeSearchMethod_{SearchMethod::ParameterSweep};
    int timeoutSeconds_{120};
    std::size_t candidateIndex_{};
    double bestScore_{};
    QString searchStopReason_;
    std::optional<analysis::AdaptiveVectorSearch> adaptiveVectorSearch_;
    std::optional<analysis::NelderMeadSearch> nelderMeadSearch_;
    std::optional<analysis::DifferentialEvolutionSearch> differentialEvolutionSearch_;
    int bestRow_{-1};
    int detailCandidateRow_{-1};
    bool modelValid_{};
    bool externalRunActive_{};
    bool cancelRequested_{};
    bool historicalSession_{};
    bool contextInitialized_{};
    bool preserveStudyOnNextContextUpdate_{};
};

}
