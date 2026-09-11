#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/AdaptiveSearch.h"
#include "analysis/OptimizationObjective.h"
#include "model/ModelSetup.h"
#include "nec/NecSymbolResolver.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/optimization/CandidateEvaluator.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
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
class SweepPlotsView;

class OptimizationWorkspace final : public QWidget {
public:
    explicit OptimizationWorkspace(QWidget* parent = nullptr);

    void setContext(QString source, QString sourceFile, QString backend,
        QString executable, int timeoutSeconds, bool modelValid);
    void setModelValid(bool valid);
    void setExternalRunActive(bool active);
    void setRunsChangedCallback(std::function<void()> callback);
    void setRunningChangedCallback(std::function<void()> callback);
    void setReturnToCurrentWorkCallback(std::function<void()> callback);
    void setApplyParameterCallback(std::function<bool(QString, double)> callback);
    auto loadSession(const QString& sessionId) -> bool;
    void leaveHistoricalSession();
    [[nodiscard]] auto isRunning() const noexcept -> bool;
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
        Adaptive
    };

    struct FrequencySelectionState {
        FrequencyMode mode{FrequencyMode::ModelSweep};
        std::vector<double> explicitFrequenciesMHz;
        double continuousStartMHz{};
        double continuousStopMHz{};
        double continuousStepMHz{};
    };

    struct Candidate {
        double value{};
        int row{};
        AnalysisRunRecord record;
        std::vector<analysis::FeedpointResult> feedpoints;
        std::optional<analysis::OptimizationObjectiveResult> evaluation;
    };

    void populateVariables(const nec::SymbolResolution& resolution);
    void updateBounds();
    void updateObjectiveControls();
    void updateFrequencyControls();
    void updateSearchMethodControls();
    void updateWorkload();
    void showCandidateDetails(int row);
    void updateCandidateDetails(int row);
    void resetCandidateDetails();
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
    void startSweep();
    void cancelSweep();
    void startNextCandidate();
    [[nodiscard]] auto prepareAdaptiveRound() -> bool;
    void appendCandidate(double value);
    void finishCurrentCandidate(CandidateEvaluationResult result);
    void finishSweep();
    auto writeCandidateMetadata(const Candidate& candidate) -> bool;
    void setCandidateStatus(int row, const QString& status);
    [[nodiscard]] auto selectedObjective() const -> analysis::OptimizationObjectiveSpec;
    [[nodiscard]] auto selectedFrequencyMode() const -> FrequencyMode;
    [[nodiscard]] auto selectedSearchMethod() const -> SearchMethod;
    [[nodiscard]] auto selectedFrequencyPlan() const -> analysis::FrequencyPlan;
    [[nodiscard]] auto explicitFrequencies() const -> std::vector<double>;
    [[nodiscard]] auto captureFrequencySelection() const -> FrequencySelectionState;
    void restoreFrequencySelection(const FrequencySelectionState& state);
    [[nodiscard]] auto objectiveName(analysis::OptimizationObjectiveKind kind) const -> QString;

    QTableWidget* variablesTable_{};
    QComboBox* variableControl_{};
    QComboBox* objectiveControl_{};
    QDoubleSpinBox* targetFrequencyControl_{};
    QTableWidget* objectiveCriteriaTable_{};
    QDoubleSpinBox* swrWeightControl_{};
    QDoubleSpinBox* resistanceWeightControl_{};
    QDoubleSpinBox* resistanceTargetControl_{};
    QDoubleSpinBox* reactanceWeightControl_{};
    QDoubleSpinBox* reactanceTargetControl_{};
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
    QDoubleSpinBox* referenceImpedanceControl_{};
    QLabel* studySummaryLabel_{};
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
    SweepPlotsView* candidateDetailPlots_{};
    QWidget* historicalBanner_{};
    QLabel* historicalBannerTitle_{};
    QPushButton* returnToCurrentWorkButton_{};
    CandidateEvaluator* evaluator_{};
    AnalysisRunStore runStore_;
    std::function<void()> runsChangedCallback_;
    std::function<void()> runningChangedCallback_;
    std::function<void()> returnToCurrentWorkCallback_;
    std::function<bool(QString, double)> applyParameterCallback_;
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
    QString selectedValueSuffix_;
    analysis::OptimizationObjectiveSpec activeObjective_;
    SearchMethod activeSearchMethod_{SearchMethod::ParameterSweep};
    int timeoutSeconds_{120};
    std::size_t candidateIndex_{};
    double bestScore_{};
    QString adaptiveStopReason_;
    std::optional<analysis::AdaptiveSearch> adaptiveSearch_;
    int bestRow_{-1};
    int detailCandidateRow_{-1};
    bool modelValid_{};
    bool externalRunActive_{};
    bool cancelRequested_{};
    bool historicalSession_{};
    bool contextInitialized_{};
};

}
