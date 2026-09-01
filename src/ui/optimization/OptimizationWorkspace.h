#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/OptimizationObjective.h"
#include "model/ModelSetup.h"
#include "nec/NecSymbolResolver.h"
#include "ui/analysis/AnalysisRunStore.h"

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace necwb::ui {

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
    auto loadSession(const QString& sessionId) -> bool;
    void leaveHistoricalSession();
    [[nodiscard]] auto isRunning() const noexcept -> bool;
    void cancelAndWait();

private:
    enum class FrequencyMode {
        ModelSweep,
        Explicit
    };

    struct Candidate {
        double value{};
        int row{};
        AnalysisRunRecord record;
        std::vector<analysis::FeedpointResult> feedpoints;
    };

    void populateVariables(const nec::SymbolResolution& resolution);
    void updateBounds();
    void updateObjectiveControls();
    void updateFrequencyControls();
    void updateWorkload();
    void updateCandidateDetails();
    void updateReadiness();
    void populateModelFrequencies(const model::FrequencyDefinition& frequency);
    void setExplicitFrequencies(const std::vector<double>& frequenciesMHz);
    void addExplicitFrequency(double frequencyMHz);
    void pasteExplicitFrequencies();
    void startSweep();
    void cancelSweep();
    void startNextCandidate();
    void finishCurrentCandidate(bool processSucceeded, const QString& detail = {});
    void finishSweep();
    auto writeCandidateFiles(Candidate& candidate, const std::string& generatedDeck) -> bool;
    void setCandidateStatus(int row, const QString& status);
    [[nodiscard]] auto selectedObjective() const -> analysis::OptimizationObjectiveSpec;
    [[nodiscard]] auto selectedFrequencyMode() const -> FrequencyMode;
    [[nodiscard]] auto explicitFrequencies() const -> std::vector<double>;
    [[nodiscard]] auto objectiveName(analysis::OptimizationObjectiveKind kind) const -> QString;

    QTableWidget* variablesTable_{};
    QComboBox* variableControl_{};
    QComboBox* objectiveControl_{};
    QDoubleSpinBox* targetFrequencyControl_{};
    QComboBox* frequencyModeControl_{};
    QWidget* explicitFrequencyPanel_{};
    QTableWidget* frequencyTable_{};
    QDoubleSpinBox* frequencyEntryControl_{};
    QPushButton* addFrequencyButton_{};
    QPushButton* removeFrequencyButton_{};
    QPushButton* pasteFrequencyButton_{};
    QLabel* workloadLabel_{};
    QDoubleSpinBox* minimumControl_{};
    QDoubleSpinBox* maximumControl_{};
    QSpinBox* pointsControl_{};
    QDoubleSpinBox* referenceImpedanceControl_{};
    QPushButton* runButton_{};
    QPushButton* cancelButton_{};
    QProgressBar* progress_{};
    QLabel* statusLabel_{};
    QLabel* bestLabel_{};
    QTableWidget* resultsTable_{};
    QLabel* candidateDetailLabel_{};
    QTableWidget* candidateDetailsTable_{};
    QWidget* historicalBanner_{};
    QLabel* historicalBannerTitle_{};
    QPushButton* returnToCurrentWorkButton_{};
    QProcess* process_{};
    QTimer* timeout_{};
    QElapsedTimer elapsed_;
    AnalysisRunStore runStore_;
    std::function<void()> runsChangedCallback_;
    std::function<void()> runningChangedCallback_;
    std::function<void()> returnToCurrentWorkCallback_;
    std::vector<nec::SymbolDefinition> definitions_;
    std::vector<Candidate> candidates_;
    std::vector<double> modelFrequenciesMHz_;
    std::vector<double> activeFrequenciesMHz_;
    std::optional<AnalysisRunRecord> sessionRecord_;
    QString source_;
    QString sourceFile_;
    QString backend_;
    QString executable_;
    QString selectedSymbol_;
    QString selectedValueSuffix_;
    analysis::OptimizationObjectiveSpec activeObjective_;
    int timeoutSeconds_{120};
    std::size_t candidateIndex_{};
    double bestScore_{};
    int bestRow_{-1};
    bool modelValid_{};
    bool externalRunActive_{};
    bool cancelRequested_{};
    bool timedOut_{};
    bool historicalSession_{};
};

}
